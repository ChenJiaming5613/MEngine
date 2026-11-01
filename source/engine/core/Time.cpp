#include "core/Time.h"

#include <SDL3/SDL_timer.h>

namespace mengine {

void Timer::Reset()
{
    m_lastCounter = SDL_GetPerformanceCounter();
}

double Timer::Tick()
{
    const uint64_t now = SDL_GetPerformanceCounter();
    if (m_lastCounter == 0)
    {
        m_lastCounter = now;
    }
    const double deltaSeconds = static_cast<double>(now - m_lastCounter) / static_cast<double>(SDL_GetPerformanceFrequency());
    m_lastCounter = now;
    return deltaSeconds;
}

} // namespace mengine
