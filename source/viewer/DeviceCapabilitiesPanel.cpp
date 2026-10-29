#include "DeviceCapabilitiesPanel.hpp"

#include "rhi/GpuDevice.hpp"

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
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace mengine {

namespace {

using rhi::CapabilityCheck;
using rhi::DeviceCapabilities;
using rhi::DeviceTier;

constexpr double kHeapRefreshSeconds = 0.5;

constexpr ImU32 kYes = IM_COL32(0x5C, 0xC2, 0x6E, 255);
constexpr ImU32 kNo = IM_COL32(0xE0, 0x48, 0x48, 255);
constexpr ImU32 kMuted = IM_COL32(0x7A, 0x7A, 0x7A, 255);
constexpr ImU32 kAccent = IM_COL32(0xF5, 0xD1, 0x42, 255);
constexpr ImU32 kSecondary = IM_COL32(0xE0, 0xE0, 0xE0, 200);

ImU32 TierColor(DeviceTier tier)
{
    switch (tier)
    {
    case DeviceTier::T0: return IM_COL32(0x1F, 0x3A, 0x93, 255);
    case DeviceTier::T1: return IM_COL32(0x1B, 0x5E, 0x20, 255);
    case DeviceTier::T2: return IM_COL32(0x6A, 0x1B, 0x9A, 255);
    }
    return kMuted;
}

ImVec4 ToVec4(ImU32 color)
{
    return ImGui::ColorConvertU32ToFloat4(color);
}

// One text line high.
void StatusMark(bool met)
{
    const float size = ImGui::GetTextLineHeight();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float thickness = std::max(1.5f, size * 0.12f);
    const float pad = size * 0.2f;
    if (met)
    {
        drawList->AddLine(ImVec2(position.x + pad, position.y + size * 0.55f), ImVec2(position.x + size * 0.42f, position.y + size - pad),
            kYes, thickness);
        drawList->AddLine(ImVec2(position.x + size * 0.42f, position.y + size - pad), ImVec2(position.x + size - pad, position.y + pad),
            kYes, thickness);
    }
    else
    {
        drawList->AddLine(ImVec2(position.x + pad, position.y + pad), ImVec2(position.x + size - pad, position.y + size - pad), kNo,
            thickness);
        drawList->AddLine(ImVec2(position.x + size - pad, position.y + pad), ImVec2(position.x + pad, position.y + size - pad), kNo,
            thickness);
    }
    ImGui::Dummy(ImVec2(size, size));
}

// "-" for values that do not apply.
void OptionalMark(std::optional<bool> value)
{
    if (value.has_value())
    {
        StatusMark(*value);
    }
    else
    {
        ImGui::TextColored(ToVec4(kMuted), "-");
    }
}

// `filled` false draws only the outline.
void Pill(const char* text, ImU32 color, bool filled = true)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 padding(style.FramePadding.x, style.FramePadding.y * 0.5f);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + textSize.x + padding.x * 2.0f, min.y + textSize.y + padding.y * 2.0f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float rounding = (max.y - min.y) * 0.5f;
    if (filled)
    {
        drawList->AddRectFilled(min, max, color, rounding);
        drawList->AddRectFilledMultiColor(ImVec2(min.x + rounding * 0.5f, min.y), ImVec2(max.x - rounding * 0.5f, (min.y + max.y) * 0.5f),
            IM_COL32(255, 255, 255, 28), IM_COL32(255, 255, 255, 28), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    }
    else
    {
        drawList->AddRect(min, max, color, rounding, 1.5f);
    }
    drawList->AddText(ImVec2(min.x + padding.x, min.y + padding.y), filled ? IM_COL32_WHITE : kMuted, text);
    ImGui::Dummy(ImVec2(max.x - min.x, max.y - min.y));
}

std::string FormatBytes(VkDeviceSize bytes)
{
    char text[32];
    const double value = static_cast<double>(bytes);
    if (bytes >= (VkDeviceSize{1} << 30))
    {
        std::snprintf(text, sizeof(text), "%.2f GB", value / static_cast<double>(1u << 30));
    }
    else if (bytes >= (VkDeviceSize{1} << 20))
    {
        std::snprintf(text, sizeof(text), "%.1f MB", value / static_cast<double>(1u << 20));
    }
    else if (bytes >= 1024)
    {
        std::snprintf(text, sizeof(text), "%.1f KB", value / 1024.0);
    }
    else
    {
        std::snprintf(text, sizeof(text), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return text;
}

template <typename Flags, size_t N>
std::string FlagNames(Flags flags, const std::pair<Flags, const char*> (&names)[N], const char* separator = " | ")
{
    std::string text;
    for (const auto& [bit, name] : names)
    {
        if ((flags & bit) != 0)
        {
            text += text.empty() ? "" : separator;
            text += name;
        }
    }
    return text.empty() ? "-" : text;
}

std::string QueueFlagNames(VkQueueFlags flags)
{
    static const std::pair<VkQueueFlags, const char*> kNames[] = {
        {VK_QUEUE_GRAPHICS_BIT, "Graphics"},
        {VK_QUEUE_COMPUTE_BIT, "Compute"},
        {VK_QUEUE_TRANSFER_BIT, "Transfer"},
        {VK_QUEUE_SPARSE_BINDING_BIT, "Sparse"},
        {VK_QUEUE_PROTECTED_BIT, "Protected"},
    };
    return FlagNames(flags, kNames);
}

std::string MemoryPropertyNames(VkMemoryPropertyFlags flags)
{
    static const std::pair<VkMemoryPropertyFlags, const char*> kNames[] = {
        {VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "DeviceLocal"},
        {VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, "HostVisible"},
        {VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "HostCoherent"},
        {VK_MEMORY_PROPERTY_HOST_CACHED_BIT, "HostCached"},
        {VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, "Lazy"},
        {VK_MEMORY_PROPERTY_PROTECTED_BIT, "Protected"},
    };
    return FlagNames(flags, kNames);
}

std::string ShaderStageNames(VkShaderStageFlags flags)
{
    static const std::pair<VkShaderStageFlags, const char*> kNames[] = {
        {VK_SHADER_STAGE_VERTEX_BIT, "VS"},
        {VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, "TCS"},
        {VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, "TES"},
        {VK_SHADER_STAGE_GEOMETRY_BIT, "GS"},
        {VK_SHADER_STAGE_FRAGMENT_BIT, "FS"},
        {VK_SHADER_STAGE_COMPUTE_BIT, "CS"},
        {VK_SHADER_STAGE_TASK_BIT_EXT, "TS"},
        {VK_SHADER_STAGE_MESH_BIT_EXT, "MS"},
    };
    return FlagNames(flags, kNames, " ");
}

std::string SubgroupOperationNames(VkSubgroupFeatureFlags flags)
{
    static const std::pair<VkSubgroupFeatureFlags, const char*> kNames[] = {
        {VK_SUBGROUP_FEATURE_BASIC_BIT, "Basic"},
        {VK_SUBGROUP_FEATURE_VOTE_BIT, "Vote"},
        {VK_SUBGROUP_FEATURE_ARITHMETIC_BIT, "Arithmetic"},
        {VK_SUBGROUP_FEATURE_BALLOT_BIT, "Ballot"},
        {VK_SUBGROUP_FEATURE_SHUFFLE_BIT, "Shuffle"},
        {VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT, "ShuffleRelative"},
        {VK_SUBGROUP_FEATURE_CLUSTERED_BIT, "Clustered"},
        {VK_SUBGROUP_FEATURE_QUAD_BIT, "Quad"},
    };
    return FlagNames(flags, kNames, " ");
}

std::string SampleCountNames(VkSampleCountFlags flags)
{
    std::string text;
    for (uint32_t count = 1; count <= 64; count *= 2)
    {
        if ((flags & count) != 0)
        {
            text += text.empty() ? "" : " ";
            text += std::to_string(count);
        }
    }
    return text.empty() ? "-" : text;
}

bool ContainsInsensitive(std::string_view text, const char* pattern)
{
    const std::string_view needle(pattern);
    if (needle.empty())
    {
        return true;
    }
    const auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != text.end();
}

void SectionRow(const char* title, int columns)
{
    ImGui::TableNextRow();
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(0x2B, 0x2D, 0x31, 255));
    ImGui::TableSetColumnIndex(0);
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    for (int column = 1; column < columns; ++column)
    {
        ImGui::TableSetColumnIndex(column);
    }
}

constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH
    | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;

struct FeatureRow
{
    const char* name;
    bool supported;
    std::optional<bool> enabled; // nullopt: MEngine does not enable it at all
    const char* tier;            // "" when not a tier requirement
};

#define MENGINE_CORE_FEATURES(X)                                                                                         \
    X(robustBufferAccess) X(fullDrawIndexUint32) X(imageCubeArray) X(independentBlend) X(geometryShader)                \
    X(tessellationShader) X(sampleRateShading) X(dualSrcBlend) X(logicOp) X(multiDrawIndirect)                         \
    X(drawIndirectFirstInstance) X(depthClamp) X(depthBiasClamp) X(fillModeNonSolid) X(depthBounds) X(wideLines)       \
    X(largePoints) X(alphaToOne) X(multiViewport) X(samplerAnisotropy) X(textureCompressionETC2)                       \
    X(textureCompressionASTC_LDR) X(textureCompressionBC) X(occlusionQueryPrecise) X(pipelineStatisticsQuery)          \
    X(vertexPipelineStoresAndAtomics) X(fragmentStoresAndAtomics) X(shaderTessellationAndGeometryPointSize)            \
    X(shaderImageGatherExtended) X(shaderStorageImageExtendedFormats) X(shaderStorageImageMultisample)                 \
    X(shaderStorageImageReadWithoutFormat) X(shaderStorageImageWriteWithoutFormat)                                     \
    X(shaderUniformBufferArrayDynamicIndexing) X(shaderSampledImageArrayDynamicIndexing)                               \
    X(shaderStorageBufferArrayDynamicIndexing) X(shaderStorageImageArrayDynamicIndexing) X(shaderClipDistance)         \
    X(shaderCullDistance) X(shaderFloat64) X(shaderInt64) X(shaderInt16) X(shaderResourceResidency)                    \
    X(shaderResourceMinLod) X(sparseBinding) X(sparseResidencyBuffer) X(sparseResidencyImage2D)                        \
    X(sparseResidencyImage3D) X(sparseResidency2Samples) X(sparseResidency4Samples) X(sparseResidency8Samples)         \
    X(sparseResidency16Samples) X(sparseResidencyAliased) X(variableMultisampleRate) X(inheritedQueries)

const char* CoreFeatureTier(std::string_view name)
{
    if (name == "imageCubeArray" || name == "multiDrawIndirect")
    {
        return "T1";
    }
    if (name == "sparseBinding" || name == "sparseResidencyBuffer" || name == "sparseResidencyImage2D")
    {
        return "T2";
    }
    return "";
}

std::vector<FeatureRow> CoreFeatureRows(const VkPhysicalDeviceFeatures& supported, const VkPhysicalDeviceFeatures& enabled)
{
    std::vector<FeatureRow> rows;
#define MENGINE_CORE_FEATURE_ROW(member)                                                                                  \
    rows.push_back({#member, supported.member == VK_TRUE, enabled.member == VK_TRUE, CoreFeatureTier(#member)});
    MENGINE_CORE_FEATURES(MENGINE_CORE_FEATURE_ROW)
#undef MENGINE_CORE_FEATURE_ROW
    return rows;
}

std::vector<FeatureRow> Vulkan11FeatureRows(const DeviceCapabilities& caps)
{
    return {
        {"multiview", caps.multiview, std::nullopt, ""},
        {"shaderDrawParameters", caps.shaderDrawParameters, std::nullopt, ""},
        {"storageBuffer16BitAccess", caps.storageBuffer16BitAccess, std::nullopt, ""},
        {"uniformAndStorageBuffer16BitAccess", caps.uniformAndStorageBuffer16BitAccess, std::nullopt, ""},
        {"samplerYcbcrConversion", caps.samplerYcbcrConversion, std::nullopt, ""},
    };
}

std::vector<FeatureRow> ExtensionFeatureRows(const DeviceCapabilities& caps)
{
    const DeviceCapabilities::DescriptorIndexing& indexing = caps.descriptorIndexing;
    return {
        {"descriptorIndexing: runtimeDescriptorArray", indexing.runtimeDescriptorArray, std::nullopt, "T1"},
        {"descriptorIndexing: partiallyBound", indexing.partiallyBound, std::nullopt, "T1"},
        {"descriptorIndexing: sampledImageUpdateAfterBind", indexing.sampledImageUpdateAfterBind, std::nullopt, "T1"},
        {"descriptorIndexing: sampledImageArrayNonUniformIndexing", indexing.sampledImageArrayNonUniformIndexing, std::nullopt,
            "T1"},
        {"descriptorIndexing: variableDescriptorCount", indexing.variableDescriptorCount, std::nullopt, ""},
        {"descriptorIndexing: storageBufferUpdateAfterBind", indexing.storageBufferUpdateAfterBind, std::nullopt, ""},
        {"drawIndirectCount", caps.drawIndirectCount, std::nullopt, "T1"},
        {"timelineSemaphore", caps.timelineSemaphore, std::nullopt, "T1"},
        {"samplerFilterMinmax", caps.samplerFilterMinmax, std::nullopt, "T1"},
        {"createRenderPass2", caps.createRenderPass2, std::nullopt, "T1"},
        {"meshShader (EXT): taskShader", caps.meshShading.extTask, std::nullopt, "T2"},
        {"meshShader (EXT): meshShader", caps.meshShading.extMesh, std::nullopt, "T2"},
        {"meshShader (NV)", caps.meshShading.nvExtension, std::nullopt, "T2"},
        {"fragmentShadingRate: pipeline", caps.fragmentShadingRate.pipeline, std::nullopt, "T2"},
        {"fragmentShadingRate: primitive", caps.fragmentShadingRate.primitive, std::nullopt, "T2"},
        {"fragmentShadingRate: attachment", caps.fragmentShadingRate.attachment, std::nullopt, "T2"},
        {"accelerationStructure", caps.accelerationStructure, std::nullopt, "T2"},
        {"rayTracingPipeline", caps.rayTracingPipeline, std::nullopt, "T2"},
        {"rayQuery", caps.rayQuery, std::nullopt, "T2"},
        {"bufferDeviceAddress", caps.bufferDeviceAddress, std::nullopt, "T2"},
        {"memoryBudget", caps.memoryBudget, std::nullopt, ""},
        {"synchronization2", caps.synchronization2, std::nullopt, ""},
        {"dynamicRendering (not used)", caps.dynamicRendering, std::nullopt, ""},
        {"shaderFloat16", caps.shaderFloat16, std::nullopt, ""},
        {"shaderInt8", caps.shaderInt8, std::nullopt, ""},
        {"hostQueryReset", caps.hostQueryReset, std::nullopt, ""},
        {"calibratedTimestamps", caps.calibratedTimestamps, std::nullopt, ""},
        {"portabilitySubset", caps.portabilitySubset, std::nullopt, ""},
    };
}

void TierTag(const char* tier)
{
    if (tier[0] == '\0')
    {
        return;
    }
    // Lighter than the badge colors, for text on the dark table.
    const ImU32 color = tier[1] == '2' ? IM_COL32(0xC0, 0x84, 0xFC, 255) : IM_COL32(0x5C, 0xC2, 0x6E, 255);
    ImGui::TextColored(ToVec4(color), "%s", tier);
}

void CheckTable(const char* id, const std::vector<CapabilityCheck>& checks)
{
    if (!ImGui::BeginTable(id, 3, kTableFlags))
    {
        return;
    }
    ImGui::TableSetupColumn("##mark", ImGuiTableColumnFlags_WidthFixed, ImGui::GetTextLineHeight());
    ImGui::TableSetupColumn("Requirement", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    for (const CapabilityCheck& check : checks)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        StatusMark(check.met);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(check.name);
        ImGui::TableSetColumnIndex(2);
        if (!check.detail.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
            ImGui::TextWrapped("%s", check.detail.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndTable();
}

size_t MetCount(const std::vector<CapabilityCheck>& checks)
{
    return static_cast<size_t>(std::count_if(checks.begin(), checks.end(), [](const CapabilityCheck& check) { return check.met; }));
}

} // namespace

bool DeviceCapabilitiesPanel::Contains(float x, float y) const
{
    return x >= m_windowMin[0] && y >= m_windowMin[1] && x <= m_windowMax[0] && y <= m_windowMax[1];
}

void DeviceCapabilitiesPanel::Draw(const rhi::GpuDevice& device)
{
    m_windowMin[0] = m_windowMin[1] = 0.0f;
    m_windowMax[0] = m_windowMax[1] = -1.0f;
    if (!m_open)
    {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.52f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.46f, io.DisplaySize.y * 0.36f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 200.0f), ImVec2(FLT_MAX, FLT_MAX));
    const bool visible = ImGui::Begin("Device Capabilities", &m_open);
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    m_windowMin[0] = position.x;
    m_windowMin[1] = position.y;
    m_windowMax[0] = position.x + size.x;
    m_windowMax[1] = position.y + size.y;
    if (!visible)
    {
        ImGui::End();
        return;
    }

    const DeviceCapabilities& caps = device.GetCapabilities();
    DrawHeader(device);

    if (ImGui::BeginTabBar("Tabs", ImGuiTabBarFlags_FittingPolicyScroll))
    {
        const auto tab = [&](const char* label, auto&& body) {
            if (ImGui::BeginTabItem(label))
            {
                if (ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f)))
                {
                    body();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        };
        tab("Overview", [&] { DrawOverview(caps); });
        tab("Features", [&] { DrawFeatures(device); });
        tab("Extensions", [&] { DrawExtensions(caps); });
        tab("Limits", [&] { DrawLimits(caps); });
        tab("Memory", [&] { DrawMemory(device); });
        tab("Queues", [&] { DrawQueues(caps); });
        tab("Formats", [&] { DrawFormats(caps); });
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void DeviceCapabilitiesPanel::DrawHeader(const rhi::GpuDevice& device)
{
    const DeviceCapabilities& caps = device.GetCapabilities();
    const VkPhysicalDeviceProperties& properties = caps.properties;

    char badge[48];
    std::snprintf(badge, sizeof(badge), "%s  %s", rhi::DeviceTierName(caps.tier), rhi::DeviceTierDescription(caps.tier));
    Pill(badge, TierColor(caps.tier));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(properties.deviceName);

    // Reached tiers filled.
    ImGui::SameLine(0.0f, ImGui::GetFontSize() * 1.5f);
    for (const DeviceTier tier : {DeviceTier::T0, DeviceTier::T1, DeviceTier::T2})
    {
        Pill(rhi::DeviceTierName(tier), TierColor(tier), tier <= caps.tier);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s: %s", rhi::DeviceTierName(tier), rhi::DeviceTierDescription(tier));
        }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    }
    ImGui::NewLine();

    ImGui::PushStyleColor(ImGuiCol_Text, kSecondary);
    ImGui::Text("%s  |  %s (0x%04X:0x%04X)  |  Vulkan %s (loader %s)  |  Driver %s", rhi::DeviceTypeName(properties.deviceType),
        rhi::VendorName(properties.vendorID), properties.vendorID, properties.deviceID,
        rhi::ApiVersionString(properties.apiVersion).c_str(), rhi::ApiVersionString(caps.instanceVersion).c_str(),
        rhi::DriverVersionString(properties.vendorID, properties.driverVersion).c_str());
    if (!caps.driverName.empty())
    {
        ImGui::Text("%s %s  |  conformance %u.%u.%u.%u", caps.driverName.c_str(), caps.driverInfo.c_str(),
            caps.conformanceVersion.major, caps.conformanceVersion.minor, caps.conformanceVersion.subminor,
            caps.conformanceVersion.patch);
    }
    ImGui::PopStyleColor();
}

void DeviceCapabilitiesPanel::DrawOverview(const DeviceCapabilities& caps)
{
    ImGui::TextWrapped("MEngine runs at Vulkan 1.1 and creates the device with VK_KHR_swapchain only; the tiers say which "
                       "render paths this GPU could use (docs/Roadmap.md, section 1).");

    const std::vector<CapabilityCheck> t1 = caps.TierChecks(DeviceTier::T1);
    const std::vector<CapabilityCheck> t2 = caps.TierChecks(DeviceTier::T2);
    const std::vector<CapabilityCheck> baseline = caps.BaselineChecks();

    char title[96];
    std::snprintf(title, sizeof(title), "T1 Extensions  (%zu / %zu)", MetCount(t1), t1.size());
    ImGui::SeparatorText(title);
    CheckTable("T1", t1);

    std::snprintf(title, sizeof(title), "T2 Desktop  (%zu / %zu, also needs T1)", MetCount(t2), t2.size());
    ImGui::SeparatorText(title);
    CheckTable("T2", t2);

    std::snprintf(title, sizeof(title), "T0 optional  (%zu / %zu, not guaranteed on the baseline)", MetCount(baseline),
        baseline.size());
    ImGui::SeparatorText(title);
    CheckTable("T0", baseline);

    ImGui::SeparatorText("Highlights");
    const VkPhysicalDeviceLimits& limits = caps.properties.limits;
    ImGui::Text("Subgroup: %u lanes (%s)", caps.subgroupSize, SubgroupOperationNames(caps.subgroupOperations).c_str());
    ImGui::Text("Push constants: %u B   Bound sets: %u   Max 2D image: %u", limits.maxPushConstantsSize,
        limits.maxBoundDescriptorSets, limits.maxImageDimension2D);
    ImGui::Text("Timestamp period: %.3f ns   Max anisotropy: %.0f", limits.timestampPeriod, limits.maxSamplerAnisotropy);
}

void DeviceCapabilitiesPanel::DrawFeatures(const rhi::GpuDevice& device)
{
    const DeviceCapabilities& caps = device.GetCapabilities();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    ImGui::InputTextWithHint("##filter", "Filter", m_filter, sizeof(m_filter));
    ImGui::SameLine();
    ImGui::Checkbox("Only supported", &m_onlySupported);

    if (!ImGui::BeginTable("Features", 4, kTableFlags))
    {
        return;
    }
    ImGui::TableSetupColumn("Feature", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Supported", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Tier", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();

    const auto section = [&](const char* title, const std::vector<FeatureRow>& rows) {
        bool header = false;
        for (const FeatureRow& row : rows)
        {
            if ((m_onlySupported && !row.supported) || !ContainsInsensitive(row.name, m_filter))
            {
                continue;
            }
            if (!header)
            {
                SectionRow(title, 4);
                header = true;
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (row.supported)
            {
                ImGui::TextUnformatted(row.name);
            }
            else
            {
                ImGui::TextColored(ToVec4(kMuted), "%s", row.name);
            }
            ImGui::TableSetColumnIndex(1);
            StatusMark(row.supported);
            ImGui::TableSetColumnIndex(2);
            OptionalMark(row.enabled);
            ImGui::TableSetColumnIndex(3);
            TierTag(row.tier);
        }
    };
    section("Vulkan 1.0 core", CoreFeatureRows(caps.features, device.GetEnabledFeatures()));
    section("Vulkan 1.1 core", Vulkan11FeatureRows(caps));
    section("Extensions", ExtensionFeatureRows(caps));
    ImGui::EndTable();
}

void DeviceCapabilitiesPanel::DrawExtensions(const DeviceCapabilities& caps)
{
    ImGui::RadioButton("Device", &m_extensionList, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Instance", &m_extensionList, 1);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    ImGui::InputTextWithHint("##filter", "Filter", m_filter, sizeof(m_filter));

    const std::vector<rhi::ExtensionInfo>& extensions = m_extensionList == 1 ? caps.instanceExtensions : caps.deviceExtensions;
    const size_t enabled = static_cast<size_t>(
        std::count_if(extensions.begin(), extensions.end(), [](const rhi::ExtensionInfo& extension) { return extension.enabled; }));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ToVec4(kSecondary), "%zu supported, %zu enabled", extensions.size(), enabled);

    if (!ImGui::BeginTable("Extensions", 3, kTableFlags))
    {
        return;
    }
    ImGui::TableSetupColumn("Extension", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (const rhi::ExtensionInfo& extension : extensions)
    {
        if (!ContainsInsensitive(extension.name, m_filter))
        {
            continue;
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (extension.enabled)
        {
            ImGui::TextColored(ToVec4(kAccent), "%s", extension.name.c_str());
        }
        else
        {
            ImGui::TextUnformatted(extension.name.c_str());
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%u", extension.specVersion);
        ImGui::TableSetColumnIndex(2);
        if (extension.enabled)
        {
            StatusMark(true);
        }
    }
    ImGui::EndTable();
}

void DeviceCapabilitiesPanel::DrawLimits(const DeviceCapabilities& caps)
{
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    ImGui::InputTextWithHint("##filter", "Filter", m_filter, sizeof(m_filter));
    if (!ImGui::BeginTable("Limits", 2, kTableFlags))
    {
        return;
    }
    ImGui::TableSetupColumn("Limit", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableHeadersRow();

    const VkPhysicalDeviceLimits& limits = caps.properties.limits;
    const char* pendingSection = nullptr;
    const auto section = [&](const char* title) { pendingSection = title; };
    const auto row = [&](const char* name, const std::string& value) {
        if (!ContainsInsensitive(name, m_filter) && !ContainsInsensitive(value, m_filter))
        {
            return;
        }
        if (pendingSection != nullptr)
        {
            SectionRow(pendingSection, 2);
            pendingSection = nullptr;
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(name);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(value.c_str());
    };
    const auto u = [](uint64_t value) { return std::to_string(value); };
    const auto f = [](float value) {
        char text[32];
        std::snprintf(text, sizeof(text), "%g", value);
        return std::string(text);
    };
    const auto u3 = [](const uint32_t (&values)[3]) {
        return std::to_string(values[0]) + " x " + std::to_string(values[1]) + " x " + std::to_string(values[2]);
    };
    const auto yesNo = [](bool value) { return std::string(value ? "yes" : "no"); };

    section("Images");
    row("maxImageDimension2D", u(limits.maxImageDimension2D));
    row("maxImageDimension3D", u(limits.maxImageDimension3D));
    row("maxImageDimensionCube", u(limits.maxImageDimensionCube));
    row("maxImageArrayLayers", u(limits.maxImageArrayLayers));
    row("maxSamplerAnisotropy", f(limits.maxSamplerAnisotropy));
    row("maxSamplerLodBias", f(limits.maxSamplerLodBias));
    row("framebufferColorSampleCounts", SampleCountNames(limits.framebufferColorSampleCounts));
    row("framebufferDepthSampleCounts", SampleCountNames(limits.framebufferDepthSampleCounts));
    row("maxFramebufferWidth x Height", u(limits.maxFramebufferWidth) + " x " + u(limits.maxFramebufferHeight));
    row("maxColorAttachments", u(limits.maxColorAttachments));

    section("Descriptors and buffers");
    row("maxBoundDescriptorSets", u(limits.maxBoundDescriptorSets));
    row("maxPushConstantsSize", u(limits.maxPushConstantsSize) + " B");
    row("maxPerStageDescriptorSamplers", u(limits.maxPerStageDescriptorSamplers));
    row("maxPerStageDescriptorSampledImages", u(limits.maxPerStageDescriptorSampledImages));
    row("maxPerStageDescriptorUniformBuffers", u(limits.maxPerStageDescriptorUniformBuffers));
    row("maxPerStageDescriptorStorageBuffers", u(limits.maxPerStageDescriptorStorageBuffers));
    row("maxPerStageResources", u(limits.maxPerStageResources));
    row("maxDescriptorSetSampledImages", u(limits.maxDescriptorSetSampledImages));
    row("maxPerSetDescriptors", u(caps.maxPerSetDescriptors));
    row("maxUniformBufferRange", FormatBytes(limits.maxUniformBufferRange));
    row("maxStorageBufferRange", FormatBytes(limits.maxStorageBufferRange));
    row("minUniformBufferOffsetAlignment", u(limits.minUniformBufferOffsetAlignment));
    row("minStorageBufferOffsetAlignment", u(limits.minStorageBufferOffsetAlignment));
    row("maxMemoryAllocationCount", u(limits.maxMemoryAllocationCount));
    row("maxMemoryAllocationSize", FormatBytes(caps.maxMemoryAllocationSize));
    row("bufferImageGranularity", u(limits.bufferImageGranularity));
    row("nonCoherentAtomSize", u(limits.nonCoherentAtomSize));

    section("Vertex and draw");
    row("maxVertexInputAttributes", u(limits.maxVertexInputAttributes));
    row("maxVertexInputBindings", u(limits.maxVertexInputBindings));
    row("maxDrawIndexedIndexValue", u(limits.maxDrawIndexedIndexValue));
    row("maxDrawIndirectCount", u(limits.maxDrawIndirectCount));
    row("maxViewports", u(limits.maxViewports));
    row("lineWidthRange", f(limits.lineWidthRange[0]) + " - " + f(limits.lineWidthRange[1]));

    section("Compute");
    row("maxComputeWorkGroupCount", u3(limits.maxComputeWorkGroupCount));
    row("maxComputeWorkGroupSize", u3(limits.maxComputeWorkGroupSize));
    row("maxComputeWorkGroupInvocations", u(limits.maxComputeWorkGroupInvocations));
    row("maxComputeSharedMemorySize", FormatBytes(limits.maxComputeSharedMemorySize));

    section("Subgroups (1.1)");
    row("subgroupSize", u(caps.subgroupSize));
    row("supportedStages", ShaderStageNames(caps.subgroupStages));
    row("supportedOperations", SubgroupOperationNames(caps.subgroupOperations));
    row("quadOperationsInAllStages", yesNo(caps.subgroupQuadOperationsInAllStages));
    row("maxMultiviewViewCount", u(caps.maxMultiviewViewCount));

    section("Timing");
    row("timestampPeriod", f(limits.timestampPeriod) + " ns");
    row("timestampComputeAndGraphics", yesNo(limits.timestampComputeAndGraphics == VK_TRUE));

    if (caps.descriptorIndexing.extension)
    {
        section("Descriptor indexing");
        row("maxUpdateAfterBindDescriptorsInAllPools", u(caps.descriptorIndexing.maxUpdateAfterBindDescriptorsInAllPools));
        row("maxPerStageUpdateAfterBindSampledImages", u(caps.descriptorIndexing.maxPerStageUpdateAfterBindSampledImages));
        row("maxSetUpdateAfterBindSampledImages", u(caps.descriptorIndexing.maxSetUpdateAfterBindSampledImages));
    }
    if (caps.timelineSemaphore)
    {
        section("Timeline semaphores");
        row("maxTimelineSemaphoreValueDifference", u(caps.maxTimelineSemaphoreValueDifference));
    }
    if (caps.samplerFilterMinmax)
    {
        section("Sampler filter min/max");
        row("filterMinmaxSingleComponentFormats", yesNo(caps.filterMinmaxSingleComponentFormats));
    }
    if (caps.meshShading.extMesh)
    {
        section("Mesh shading (EXT)");
        row("maxMeshOutputVertices", u(caps.meshShading.maxMeshOutputVertices));
        row("maxMeshOutputPrimitives", u(caps.meshShading.maxMeshOutputPrimitives));
        row("maxMeshWorkGroupInvocations", u(caps.meshShading.maxMeshWorkGroupInvocations));
        row("maxTaskWorkGroupInvocations", u(caps.meshShading.maxTaskWorkGroupInvocations));
    }
    if (caps.rayTracingPipeline)
    {
        section("Ray tracing pipeline");
        row("shaderGroupHandleSize", u(caps.shaderGroupHandleSize));
        row("maxRayRecursionDepth", u(caps.maxRayRecursionDepth));
    }
    ImGui::EndTable();
}

void DeviceCapabilitiesPanel::DrawMemory(const rhi::GpuDevice& device)
{
    const DeviceCapabilities& caps = device.GetCapabilities();
    const double now = ImGui::GetTime();
    if (m_heapsTime < 0.0 || now - m_heapsTime >= kHeapRefreshSeconds)
    {
        m_heaps = device.QueryHeapUsage();
        m_heapsTime = now;
    }

    if (caps.memoryBudget)
    {
        ImGui::TextColored(ToVec4(kSecondary), "Driver budget from VK_EXT_memory_budget, refreshed every %.1f s",
            kHeapRefreshSeconds);
    }
    else
    {
        ImGui::TextColored(ToVec4(kSecondary), "No VK_EXT_memory_budget: only MEngine's own allocations (VMA) are shown");
    }

    if (ImGui::BeginTable("Heaps", 5, kTableFlags))
    {
        ImGui::TableSetupColumn("Heap", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("Process usage / budget", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("MEngine (VMA)", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableHeadersRow();
        for (size_t index = 0; index < m_heaps.size(); ++index)
        {
            const rhi::HeapUsage& heap = m_heaps[index];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%zu", index);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(FormatBytes(heap.size).c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0 ? "DeviceLocal" : "Host");
            ImGui::TableSetColumnIndex(3);
            if (heap.budget > 0)
            {
                const float fraction = static_cast<float>(static_cast<double>(heap.usage) / static_cast<double>(heap.budget));
                const std::string overlay = FormatBytes(heap.usage) + " / " + FormatBytes(heap.budget);
                const ImU32 color = fraction > 0.9f ? kNo : fraction > 0.7f ? IM_COL32(0xFF, 0x9A, 0x4D, 255) : TierColor(DeviceTier::T0);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ToVec4(color));
                ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0.0f), overlay.c_str());
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::TextColored(ToVec4(kMuted), "-");
            }
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%s in %u allocs", FormatBytes(heap.allocationBytes).c_str(), heap.allocationCount);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Blocks: %s", FormatBytes(heap.blockBytes).c_str());
            }
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Memory types");
    if (ImGui::BeginTable("Types", 3, kTableFlags))
    {
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Heap", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        for (uint32_t index = 0; index < caps.memory.memoryTypeCount; ++index)
        {
            const VkMemoryType& type = caps.memory.memoryTypes[index];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%u", index);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", type.heapIndex);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(MemoryPropertyNames(type.propertyFlags).c_str());
        }
        ImGui::EndTable();
    }
}

void DeviceCapabilitiesPanel::DrawQueues(const DeviceCapabilities& caps)
{
    ImGui::Text("Dedicated compute queue: %s   Dedicated transfer queue: %s", caps.dedicatedComputeQueue ? "yes" : "no",
        caps.dedicatedTransferQueue ? "yes" : "no");
    if (!ImGui::BeginTable("Queues", 6, kTableFlags))
    {
        return;
    }
    ImGui::TableSetupColumn("Family", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Capabilities", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Queues", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Timestamp bits", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Present", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (size_t index = 0; index < caps.queueFamilies.size(); ++index)
    {
        const rhi::QueueFamilyInfo& family = caps.queueFamilies[index];
        const bool used = index == caps.graphicsQueueFamily;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%zu", index);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(QueueFlagNames(family.flags).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%u", family.queueCount);
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%u", family.timestampValidBits);
        ImGui::TableSetColumnIndex(4);
        StatusMark(family.present);
        ImGui::TableSetColumnIndex(5);
        if (used)
        {
            ImGui::TextColored(ToVec4(kAccent), "MEngine");
        }
    }
    ImGui::EndTable();
}

void DeviceCapabilitiesPanel::DrawFormats(const DeviceCapabilities& caps)
{
    struct Column
    {
        const char* name;
        VkFormatFeatureFlags bits;
        const char* tooltip;
    };
    static const Column kColumns[] = {
        {"Sampled", VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, "SAMPLED_IMAGE"},
        {"Linear", VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, "SAMPLED_IMAGE_FILTER_LINEAR"},
        {"Color", VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT, "COLOR_ATTACHMENT"},
        {"Blend", VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT, "COLOR_ATTACHMENT_BLEND"},
        {"Depth", VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, "DEPTH_STENCIL_ATTACHMENT"},
        {"Storage", VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, "STORAGE_IMAGE"},
        {"Atomic", VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT, "STORAGE_IMAGE_ATOMIC"},
        {"Blit", VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT, "BLIT_SRC and BLIT_DST"},
    };
    constexpr int kColumnCount = static_cast<int>(std::size(kColumns)) + 1;

    ImGui::TextColored(ToVec4(kSecondary), "Optimal tiling features of the formats MEngine uses or may use");
    if (!ImGui::BeginTable("Formats", kColumnCount, kTableFlags | ImGuiTableFlags_SizingFixedFit))
    {
        return;
    }
    ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    for (const Column& column : kColumns)
    {
        ImGui::TableSetupColumn(column.name, ImGuiTableColumnFlags_WidthFixed);
    }
    ImGui::TableHeadersRow();
    for (const rhi::FormatSupport& format : caps.formats)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (format.optimalTiling == 0)
        {
            ImGui::TextColored(ToVec4(kMuted), "%s", format.name);
        }
        else
        {
            ImGui::TextUnformatted(format.name);
        }
        for (int column = 0; column < kColumnCount - 1; ++column)
        {
            ImGui::TableSetColumnIndex(column + 1);
            const VkFormatFeatureFlags bits = kColumns[column].bits;
            if ((format.optimalTiling & bits) == bits)
            {
                StatusMark(true);
            }
            else if (format.optimalTiling != 0)
            {
                ImGui::TextColored(ToVec4(kMuted), "-");
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s: %s", format.name, kColumns[column].tooltip);
            }
        }
    }
    ImGui::EndTable();
}

} // namespace mengine
