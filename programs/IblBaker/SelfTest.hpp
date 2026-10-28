#pragma once

#include <cstdint>

namespace iblbaker {

// Numerical checks of the bake against identities from the notes (see docs/UE4-RealShading-2013.md, 6.4).
// Prints one line per check, including the largest deviation and where it occurred. Returns true when all pass.
bool RunSelfTest(uint32_t samples, uint32_t lutSize, uint32_t threads);

} // namespace iblbaker
