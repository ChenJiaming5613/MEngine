#pragma once

#include "Cubemap.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iblbaker {

// Reads level 0 of a little endian KTX 1.1 cube map (no array) with GL_RGB / GL_RGBA and GL_HALF_FLOAT / GL_FLOAT
// texels, such as Vulkan-glTF-PBR's environments. Its face order and top-first rows are exactly Cubemap's layout.
std::optional<Cubemap> DecodeKtx1Cube(const std::vector<uint8_t>& bytes, std::string* error);

bool IsKtx1(const std::vector<uint8_t>& bytes);

} // namespace iblbaker
