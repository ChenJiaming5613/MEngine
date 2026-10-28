#pragma once

#include "Cubemap.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iblbaker {

// Reads level 0 of a KTX 1.1 cube map (6 faces, no array, little endian) with GL_RGB or GL_RGBA and GL_HALF_FLOAT or
// GL_FLOAT texels, such as the environments of Vulkan-glTF-PBR. Faces are stored in Vulkan order and rows top first,
// which is exactly the layout of Cubemap. Returns nullopt and sets `error` otherwise.
std::optional<Cubemap> DecodeKtx1Cube(const std::vector<uint8_t>& bytes, std::string* error);

bool IsKtx1(const std::vector<uint8_t>& bytes);

} // namespace iblbaker
