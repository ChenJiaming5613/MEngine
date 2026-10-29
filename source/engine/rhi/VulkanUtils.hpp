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
#include <stdexcept>
#include <string>
#include <vector>

namespace mengine::rhi {

class VulkanError : public std::runtime_error
{
public:
    VulkanError(VkResult result, const std::string& message) : std::runtime_error(message), m_result(result) {}
    VkResult Result() const { return m_result; }
    // Out of memory is survivable by dropping the resource; anything else (device lost, ...) leaves the device unusable.
    bool IsRecoverable() const;

private:
    VkResult m_result;
};

// Throws VulkanError naming the operation when result is not VK_SUCCESS.
void CheckVk(VkResult result, const char* operation);
// For catch blocks that degrade on bad assets: rethrows the exception being handled when it is an unrecoverable
// VulkanError.
void RethrowIfDeviceUnusable(const std::exception& exception);
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

// Resource state translation, after Raptor's util_to_vk_access_flags / util_to_vk_image_layout /
// util_determine_pipeline_stage_flags. Stages come from the states rather than the access mask, so pixel and non-pixel
// shader reads stay apart.
VkAccessFlags ToVkAccessFlags(ResourceStates states);
// UNDEFINED for kResourceStateUndefined, which is only valid as the old state of an image barrier.
VkImageLayout ToVkImageLayout(ResourceStates states);
// TOP_OF_PIPE when no stage applies (kResourceStateUndefined).
VkPipelineStageFlags ToVkPipelineStages(ResourceStates states);
// Aspects an image barrier on this format must name: depth formats with stencil need both without
// separateDepthStencilLayouts (Vulkan 1.2).
VkImageAspectFlags BarrierAspectOf(VkFormat format);

// Loads a .spv asset as 32-bit words. Throws when the file is missing or not word sized.
std::vector<uint32_t> LoadSpirvAsset(const std::string& assetPath);

} // namespace mengine::rhi
