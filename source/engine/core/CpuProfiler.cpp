#include "core/CpuProfiler.hpp"

#include <SDL3/SDL_timer.h>

namespace mengine {

namespace {

uint64_t TicksToNanoseconds(uint64_t ticks)
{
    static const uint64_t frequency = SDL_GetPerformanceFrequency();
    // Split so ticks * 1e9 cannot overflow for long scopes.
    const uint64_t seconds = ticks / frequency;
    const uint64_t remainder = ticks % frequency;
    return seconds * 1'000'000'000ull + remainder * 1'000'000'000ull / frequency;
}

} // namespace

void CpuProfiler::BeginFrame(uint64_t frame)
{
    m_frame = frame;
    m_frameOpen = true;
    m_scopes.clear();
    m_openScopes.clear();
}

void CpuProfiler::Push(const char* name)
{
    if (!m_frameOpen)
    {
        return;
    }
    m_openScopes.push_back(static_cast<uint32_t>(m_scopes.size()));
    m_scopes.push_back(Scope{
        .name = name,
        .depth = static_cast<uint32_t>(m_openScopes.size() - 1),
        .startTicks = SDL_GetPerformanceCounter(),
    });
}

void CpuProfiler::Pop()
{
    if (!m_frameOpen || m_openScopes.empty())
    {
        return;
    }
    m_scopes[m_openScopes.back()].endTicks = SDL_GetPerformanceCounter();
    m_openScopes.pop_back();
}

void CpuProfiler::EndFrame()
{
    if (!m_frameOpen)
    {
        return;
    }
    m_frameOpen = false;
    // Unbalanced pushes end with the frame.
    const uint64_t now = SDL_GetPerformanceCounter();
    for (const uint32_t open : m_openScopes)
    {
        m_scopes[open].endTicks = now;
    }

    m_timings.resize(m_scopes.size());
    for (size_t index = 0; index < m_scopes.size(); ++index)
    {
        const Scope& scope = m_scopes[index];
        ScopeTiming& timing = m_timings[index];
        timing.name = scope.name;
        timing.depth = scope.depth;
        timing.nanoseconds = TicksToNanoseconds(scope.endTicks - scope.startTicks);
    }
    m_timingsFrame = m_frame;
}

double CpuProfiler::TickNanoseconds()
{
    return 1e9 / static_cast<double>(SDL_GetPerformanceFrequency());
}

} // namespace mengine
