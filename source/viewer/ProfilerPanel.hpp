#pragma once

#include "core/CpuProfiler.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace mengine {

namespace rhi {
class GpuProfiler;
} // namespace rhi

// Per pass CPU (recording) and GPU time of the frame graph: the latest frame and a rolling average, in nanoseconds.
// CPU and GPU columns always describe the same frame: CPU results wait until the GPU results of their frame resolve.
class ProfilerPanel
{
public:
    // Every frame, open or not, so averages are ready when the window opens.
    void Update(const CpuProfiler& cpuProfiler, const rhi::GpuProfiler& gpuProfiler);
    void Draw();

    bool IsOpen() const { return m_open; }
    void SetOpen(bool open) { m_open = open; }
    // (x, y) in window points, against the window drawn last frame.
    bool Contains(float x, float y) const;

private:
    enum class Unit
    {
        Nanoseconds,
        Microseconds,
        Milliseconds,
    };

    class RollingStats
    {
    public:
        void Reset(size_t window);
        void Add(uint64_t nanoseconds);
        size_t Count() const { return m_count; }
        uint64_t Last() const { return m_last; }
        double Average() const;
        uint64_t Min() const;
        uint64_t Max() const;

    private:
        std::vector<uint64_t> m_samples;
        size_t m_next = 0;
        size_t m_count = 0;
        uint64_t m_sum = 0;
        uint64_t m_last = 0;
    };

    struct Row
    {
        std::string name;
        uint32_t depth = 0;
        RollingStats cpu;
        RollingStats gpu;
        uint64_t cpuFrame = ~0ull;
        uint64_t gpuFrame = ~0ull;
        uint64_t orderFrame = ~0ull;
    };

    struct PendingCpuFrame
    {
        uint64_t frame = 0;
        std::vector<ScopeTiming> timings;
    };

    void AddFrame(uint64_t frame, const std::vector<ScopeTiming>* cpuTimings, const std::vector<ScopeTiming>* gpuTimings);
    void AddTimings(uint64_t frame, const std::vector<ScopeTiming>& timings, bool gpu, std::vector<size_t>& order);
    void ResetStats();
    void DrawControls();
    void DrawTable();
    void DrawValue(const RollingStats& stats, bool current, bool average) const;

    std::vector<Row> m_rows;
    std::unordered_map<std::string, size_t> m_rowByPath;
    // Rows of the latest frame in scope order, then rows not seen in it.
    std::vector<size_t> m_order;
    std::deque<PendingCpuFrame> m_pendingCpu;
    uint64_t m_lastCpuFrame = ~0ull;
    uint64_t m_lastGpuFrame = ~0ull;
    uint64_t m_latestFrame = ~0ull;

    bool m_gpuSupported = false;
    double m_timestampPeriod = 0.0;
    int m_window = 120;
    Unit m_unit = Unit::Milliseconds;
    bool m_paused = false;

    bool m_open = false;
    float m_windowMin[2] = {0.0f, 0.0f};
    float m_windowMax[2] = {-1.0f, -1.0f};
};

} // namespace mengine
