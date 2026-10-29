#pragma once

#include <cstdint>

namespace iblbaker {

// Numerical checks of the bake against identities from the notes (docs/UE4-RealShading-2013.md, 6.4). Prints each
// check's largest deviation and where it occurred.
bool RunSelfTest(uint32_t samples, uint32_t lutSize, uint32_t threads);

} // namespace iblbaker
