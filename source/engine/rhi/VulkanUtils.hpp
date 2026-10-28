#pragma once

#include "rhi/GpuEnums.hpp"

#include <vulkan/vulkan.h>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include <vk_mem_alloc.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cstdint>
#include <string>
#include <vector>

namespace mengine::rhi {

// Throws std::runtime_error naming the operation when result is not VK_SUCCESS.
void CheckVk(VkResult result, const char* operation);
const char* VkResultName(VkResult result);

bool IsDepthFormat(VkFormat format);
bool HasStencil(VkFormat format);
bool IsSrgbFormat(VkFormat format);
// Bytes per texel for the uncompressed formats the engine uploads; 0 for anything else.
uint32_t FormatTexelSize(VkFormat format);
bool IsBlockCompressedFormat(VkFormat format); // BC1 ... BC7
// Tightly packed bytes of one width x height level (BC formats in whole 4x4 blocks); 0 for unsupported formats.
VkDeviceSize FormatLevelSize(VkFormat format, uint32_t width, uint32_t height);
uint32_t ComputeMipCount(uint32_t width, uint32_t height);

VkAttachmentLoadOp ToVkLoadOp(RenderPassOperation operation);

// Loads a .spv asset as 32-bit words. Throws when the file is missing or not word sized.
std::vector<uint32_t> LoadSpirvAsset(const std::string& assetPath);

} // namespace mengine::rhi
