#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mengine::asset {

struct Image8
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba; // width * height * 4
};

struct ImageFloat
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<float> rgba; // width * height * 4, linear
};

// PNG / JPEG, always expanded to RGBA8. Thread safe.
std::optional<Image8> DecodeImageRgba8(std::span<const uint8_t> encoded, std::string* error);

// Radiance .hdr, expanded to RGBA32F (alpha = 1). Thread safe.
std::optional<ImageFloat> DecodeImageHdr(std::span<const uint8_t> encoded, std::string* error);

} // namespace mengine::asset
