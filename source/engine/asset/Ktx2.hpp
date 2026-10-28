#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mengine::asset {

// VkFormat values, kept here so the asset library stays free of Vulkan headers.
constexpr uint32_t kVkFormatR8G8B8A8Unorm = 37;
constexpr uint32_t kVkFormatR8G8B8A8Srgb = 43;
constexpr uint32_t kVkFormatR16G16Sfloat = 83;
constexpr uint32_t kVkFormatR16G16B16A16Sfloat = 97;

// Minimal uncompressed KTX2: 2D or cube, no array layers, no supercompression.
struct Ktx2Image
{
    uint32_t vkFormat = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t faceCount = 1; // 1 or 6, faces in Vulkan order +X, -X, +Y, -Y, +Z, -Z
    // levels[level] holds every face of that level back to back (face 0 first), tightly packed rows.
    std::vector<std::vector<uint8_t>> levels;
};

// 0 for unsupported formats.
uint32_t Ktx2BytesPerTexel(uint32_t vkFormat);

inline uint32_t MipExtent(uint32_t baseExtent, uint32_t level)
{
    const uint32_t extent = baseExtent >> level;
    return extent > 0 ? extent : 1;
}

// Returns an empty vector and fills *error when the image is inconsistent.
std::vector<uint8_t> EncodeKtx2(const Ktx2Image& image, std::string* error);
std::optional<Ktx2Image> DecodeKtx2(std::span<const uint8_t> bytes, std::string* error);

} // namespace mengine::asset
