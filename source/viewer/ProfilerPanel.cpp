#include "ProfilerPanel.hpp"

#include "rhi/GpuProfiler.hpp"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <imgui.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <cfloat>
#include <cinttypes>
#include <cmath>
#include <cstdio>

namespace mengine {

namespace {

constexpr int kMaxWindow = 1000;
// GPU results arrive kMaxFramesInFlight frames late; frames the GPU never reports (no scopes, failed readback) are
// published CPU only once this many are waiting.
constexpr size_t kMaxPendingFrames = 8;

constexpr ImU32 kMuted = IM_COL32(0x7A, 0x7A, 0x7A, 255);

// Exact to the nanosecond in every unit.
void FormatDuration(char* out, size_t size, double nanoseconds, int unit)
{
    switch (unit)
    {
    case 0:
    {
        const uint64_t value = static_cast<uint64_t>(std::llround(nanoseconds));
        char digits[32];
        const int length = std::snprintf(digits, sizeof(digits), "%" PRIu64, value);
        size_t written = 0;
        for (int index = 0; index < length && written + 2 < size; ++index)
        {
            if (index > 0 && (length - index) % 3 == 0)
            {
                out[written++] = ',';
            }
            out[written++] = digits[index];
        }
        std::snprintf(out + written, size - written, " ns");
        break;
    }
    case 1: std::snprintf(out, size, "%.3f us", nanoseconds * 1e-3); break;
    default: std::snprintf(out, size, "%.6f ms", nanoseconds * 1e-6); break;
    }
}

} // namespace

void ProfilerPanel::RollingStats::Reset(size_t window)
{
    m_samples.assign(window, 0);
    m_next = 0;
    m_count = 0;
    m_sum = 0;
    m_last = 0;
}

void ProfilerPanel::RollingStats::Add(uint64_t nanoseconds)
{
    if (m_samples.empty())
    {
        return;
    }
    if (m_count == m_samples.size())
    {
        m_sum -= m_samples[m_next];
    }
    else
    {
        ++m_count;
    }
    m_samples[m_next] = nanoseconds;
    m_next = (m_next + 1) % m_samples.size();
    m_sum += nanoseconds;
    m_last = nanoseconds;
}

double ProfilerPanel::RollingStats::Average() const
{
    return m_count > 0 ? static_cast<double>(m_sum) / static_cast<double>(m_count) : 0.0;
}

uint64_t ProfilerPanel::RollingStats::Min() const
{
    return m_count > 0 ? *std::min_element(m_samples.begin(), m_samples.begin() + static_cast<ptrdiff_t>(m_count)) : 0;
}

uint64_t ProfilerPanel::RollingStats::Max() const
{
    return m_count > 0 ? *std::max_element(m_samples.begin(), m_samples.begin() + static_cast<ptrdiff_t>(m_count)) : 0;
}

void ProfilerPanel::Update(const CpuProfiler& cpuProfiler, const rhi::GpuProfiler& gpuProfiler)
{
    m_gpuSupported = gpuProfiler.IsSupported();
    m_timestampPeriod = gpuProfiler.TimestampPeriodNanoseconds();
    if (m_paused)
    {
        return;
    }

    if (cpuProfiler.TimingsFrame() != ~0ull && cpuProfiler.TimingsFrame() != m_lastCpuFrame)
    {
        m_lastCpuFrame = cpuProfiler.TimingsFrame();
        m_pendingCpu.push_back(PendingCpuFrame{.frame = m_lastCpuFrame, .timings = cpuProfiler.Timings()});
    }

    const uint64_t gpuFrame = m_gpuSupported ? gpuProfiler.TimingsFrame() : ~0ull;
    if (gpuFrame != ~0ull && gpuFrame != m_lastGpuFrame)
    {
        m_lastGpuFrame = gpuFrame;
        while (!m_pendingCpu.empty() && m_pendingCpu.front().frame < gpuFrame)
        {
            AddFrame(m_pendingCpu.front().frame, &m_pendingCpu.front().timings, nullptr);
            m_pendingCpu.pop_front();
        }
        if (!m_pendingCpu.empty() && m_pendingCpu.front().frame == gpuFrame)
        {
            AddFrame(gpuFrame, &m_pendingCpu.front().timings, &gpuProfiler.Timings());
            m_pendingCpu.pop_front();
        }
        else
        {
            AddFrame(gpuFrame, nullptr, &gpuProfiler.Timings());
        }
    }

    const size_t maxPending = m_gpuSupported ? kMaxPendingFrames : 0;
    while (m_pendingCpu.size() > maxPending)
    {
        AddFrame(m_pendingCpu.front().frame, &m_pendingCpu.front().timings, nullptr);
        m_pendingCpu.pop_front();
    }
}

void ProfilerPanel::AddFrame(uint64_t frame, const std::vector<ScopeTiming>* cpuTimings, const std::vector<ScopeTiming>* gpuTimings)
{
    std::vector<size_t> order;
    if (cpuTimings != nullptr)
    {
        AddTimings(frame, *cpuTimings, false, order);
    }
    if (gpuTimings != nullptr)
    {
        AddTimings(frame, *gpuTimings, true, order);
    }
    for (const size_t index : m_order)
    {
        if (m_rows[index].orderFrame != frame)
        {
            order.push_back(index);
        }
    }
    m_order = std::move(order);
    m_latestFrame = frame;
}

void ProfilerPanel::AddTimings(uint64_t frame, const std::vector<ScopeTiming>& timings, bool gpu, std::vector<size_t>& order)
{
    // Rows are keyed by their path, so a pass keeps its history when its render pass group changes.
    std::vector<std::string> paths;
    for (const ScopeTiming& timing : timings)
    {
        paths.resize(std::min<size_t>(timing.depth, paths.size()));
        std::string path = paths.empty() ? timing.name : paths.back() + '/' + timing.name;

        auto [found, inserted] = m_rowByPath.try_emplace(path, m_rows.size());
        if (inserted)
        {
            Row& row = m_rows.emplace_back();
            row.name = timing.name;
            row.depth = timing.depth;
            row.cpu.Reset(static_cast<size_t>(m_window));
            row.gpu.Reset(static_cast<size_t>(m_window));
        }
        const size_t index = found->second;
        Row& row = m_rows[index];
        (gpu ? row.gpu : row.cpu).Add(timing.nanoseconds);
        (gpu ? row.gpuFrame : row.cpuFrame) = frame;
        if (row.orderFrame != frame)
        {
            row.orderFrame = frame;
            order.push_back(index);
        }
        paths.push_back(std::move(path));
    }
}

void ProfilerPanel::ResetStats()
{
    for (Row& row : m_rows)
    {
        row.cpu.Reset(static_cast<size_t>(m_window));
        row.gpu.Reset(static_cast<size_t>(m_window));
    }
}

bool ProfilerPanel::Contains(float x, float y) const
{
    return x >= m_windowMin[0] && y >= m_windowMin[1] && x <= m_windowMax[0] && y <= m_windowMax[1];
}

void ProfilerPanel::Draw()
{
    m_windowMin[0] = m_windowMin[1] = 0.0f;
    m_windowMax[0] = m_windowMax[1] = -1.0f;
    if (!m_open)
    {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.40f, io.DisplaySize.y * 0.55f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.58f, io.DisplaySize.y * 0.42f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 160.0f), ImVec2(FLT_MAX, FLT_MAX));
    const bool visible = ImGui::Begin("Profiler", &m_open);
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    m_windowMin[0] = position.x;
    m_windowMin[1] = position.y;
    m_windowMax[0] = position.x + size.x;
    m_windowMax[1] = position.y + size.y;
    if (visible)
    {
        DrawControls();
        DrawTable();
    }
    ImGui::End();
}

void ProfilerPanel::DrawControls()
{
    static const char* const kUnitNames[] = {"ns", "us", "ms"};
    int unit = static_cast<int>(m_unit);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
    if (ImGui::Combo("Unit", &unit, kUnitNames, IM_ARRAYSIZE(kUnitNames)))
    {
        m_unit = static_cast<Unit>(unit);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    if (ImGui::SliderInt("Average Frames", &m_window, 1, kMaxWindow, "%d", ImGuiSliderFlags_Logarithmic))
    {
        m_window = std::clamp(m_window, 1, kMaxWindow);
        ResetStats();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Pause", &m_paused))
    {
        // Results that arrived while paused belong to frames whose partner may already be gone.
        m_pendingCpu.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        m_rows.clear();
        m_rowByPath.clear();
        m_order.clear();
        m_pendingCpu.clear();
        m_latestFrame = ~0ull;
    }

    if (m_latestFrame != ~0ull)
    {
        ImGui::Text("Frame %" PRIu64, m_latestFrame);
        ImGui::SameLine();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    if (m_gpuSupported)
    {
        ImGui::Text("CPU clock %.0f ns, GPU timestamp %.2f ns per tick", CpuProfiler::TickNanoseconds(), m_timestampPeriod);
    }
    else
    {
        ImGui::Text("CPU clock %.0f ns per tick; GPU timestamps are not supported", CpuProfiler::TickNanoseconds());
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("CPU: time spent recording the pass's commands.\n"
                          "GPU: top of pipe to bottom of pipe timestamps around the pass.\n"
                          "Both columns show the same frame. Values are exact to 1 ns,\n"
                          "but the clocks only advance in steps of the tick sizes shown.");
    }
}

void ProfilerPanel::DrawValue(const RollingStats& stats, bool current, bool average) const
{
    char text[48];
    if (average ? stats.Count() == 0 : !current)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kMuted), "-");
        return;
    }
    FormatDuration(text, sizeof(text), average ? stats.Average() : static_cast<double>(stats.Last()), static_cast<int>(m_unit));
    // Right aligned so digits line up.
    const float offset = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text).x;
    if (offset > 0.0f)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
    }
    ImGui::TextUnformatted(text);
    if (average && ImGui::IsItemHovered())
    {
        char minText[48];
        char maxText[48];
        FormatDuration(minText, sizeof(minText), static_cast<double>(stats.Min()), static_cast<int>(m_unit));
        FormatDuration(maxText, sizeof(maxText), static_cast<double>(stats.Max()), static_cast<int>(m_unit));
        ImGui::SetTooltip("Min %s\nMax %s\nover %zu frames", minText, maxText, stats.Count());
    }
}

void ProfilerPanel::DrawTable()
{
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("Timings", 5, kFlags))
    {
        return;
    }
    const float valueWidth = ImGui::CalcTextSize("0,000,000,000 ns").x;
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed, valueWidth);
    ImGui::TableSetupColumn("CPU Avg", ImGuiTableColumnFlags_WidthFixed, valueWidth);
    ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthFixed, valueWidth);
    ImGui::TableSetupColumn("GPU Avg", ImGuiTableColumnFlags_WidthFixed, valueWidth);
    ImGui::TableHeadersRow();

    for (const size_t index : m_order)
    {
        const Row& row = m_rows[index];
        const bool cpuCurrent = row.cpuFrame == m_latestFrame;
        const bool gpuCurrent = row.gpuFrame == m_latestFrame;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        // Not Indent: Indent(0) indents by the default spacing.
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetFontSize() * static_cast<float>(row.depth));
        if (cpuCurrent || gpuCurrent)
        {
            ImGui::TextUnformatted(row.name.c_str());
        }
        else
        {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kMuted), "%s (not run)", row.name.c_str());
        }

        ImGui::TableSetColumnIndex(1);
        DrawValue(row.cpu, cpuCurrent, false);
        ImGui::TableSetColumnIndex(2);
        DrawValue(row.cpu, cpuCurrent, true);
        ImGui::TableSetColumnIndex(3);
        DrawValue(row.gpu, gpuCurrent, false);
        ImGui::TableSetColumnIndex(4);
        DrawValue(row.gpu, gpuCurrent, true);
    }
    ImGui::EndTable();
}

} // namespace mengine
