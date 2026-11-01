#pragma once

#include <cstdint>

namespace mengine {

class FrameClock
{
public:
    void Reset();

    // Seconds since the previous Tick() or Reset().
    double Tick();

private:
    uint64_t m_lastCounter = 0;
};

} // namespace mengine
