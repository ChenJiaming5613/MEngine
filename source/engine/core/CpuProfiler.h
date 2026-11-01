#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mengine {

// One closed scope of a profiled frame; CPU and GPU profilers both report these, in push order.
struct ScopeTiming
{
    std::string name;
    uint32_t depth = 0;
    uint64_t nanoseconds = 0;
};

// Nested wall clock scopes on one thread (the thread that records the frame).
class CpuProfiler
{
public:
    // Discards an unfinished frame.
    void BeginFrame(uint64_t frame);
    // `name` must stay valid until EndFrame.
    void Push(const char* name);
    void Pop();
    // Publishes the frame's closed scopes to Timings().
    void EndFrame();

    const std::vector<ScopeTiming>& Timings() const { return m_timings; }
    // Frame number passed to the BeginFrame of Timings(); ~0 before the first EndFrame.
    uint64_t TimingsFrame() const { return m_timingsFrame; }
    // Resolution of the underlying counter (100 ns for QueryPerformanceCounter on most Windows machines).
    static double TickNanoseconds();

private:
    struct Scope
    {
        const char* name = nullptr;
        uint32_t depth = 0;
        uint64_t startTicks = 0;
        uint64_t endTicks = 0;
    };

    std::vector<Scope> m_scopes;
    std::vector<uint32_t> m_openScopes;
    uint64_t m_frame = 0;
    bool m_frameOpen = false;

    std::vector<ScopeTiming> m_timings;
    uint64_t m_timingsFrame = ~0ull;
};

} // namespace mengine
