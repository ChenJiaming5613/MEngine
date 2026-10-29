#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace mengine::asset {

// BC7 software decoder (Khronos Data Format Specification 1.3, section 20.4) for devices without
// textureCompressionBC. Decodes one level to tightly packed RGBA8 (values as stored, no color space conversion).
// blocks holds ceil(width / 4) * ceil(height / 4) 16 byte blocks, row-major; a short input yields black. Thread safe.
std::vector<uint8_t> DecodeBc7(std::span<const uint8_t> blocks, uint32_t width, uint32_t height);

} // namespace mengine::asset
