#pragma once

#include "core/Handle.hpp"
#include "rhi/GpuEnums.hpp"
#include "rhi/VulkanUtils.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace mengine::rhi {

// Handles and Creation descriptions follow Raptor (Mastering Graphics Programming with Vulkan, chapter 1):
// a Creation struct describes the resource, GpuDevice turns it into pooled Vulkan objects addressed by handle.

struct BufferTag;
struct TextureTag;
struct SamplerTag;
struct DescriptorSetLayoutTag;
struct DescriptorSetTag;
struct PipelineTag;
struct RenderPassTag;

using BufferHandle = Handle<BufferTag>;
using TextureHandle = Handle<TextureTag>;
using SamplerHandle = Handle<SamplerTag>;
using DescriptorSetLayoutHandle = Handle<DescriptorSetLayoutTag>;
using DescriptorSetHandle = Handle<DescriptorSetTag>;
using PipelineHandle = Handle<PipelineTag>;
using RenderPassHandle = Handle<RenderPassTag>;

constexpr uint32_t kMaxImageOutputs = 4;
constexpr uint32_t kMaxDescriptorSetLayouts = 4;
constexpr uint32_t kMaxShaderStages = 3;
constexpr uint32_t kMaxDescriptorsPerSet = 16;
constexpr uint32_t kMaxVertexStreams = 8;
constexpr uint32_t kMaxVertexAttributes = 8;
constexpr uint32_t kMaxSpecializationConstants = 8;
constexpr uint32_t kMaxBarriersPerBatch = 8;

// ---------------------------------------------------------------------------------------------------------------------
// Creation descriptions

struct BufferCreation
{
    // Buffers written by the GPU need the usages of every later use: STORAGE_BUFFER for compute writes, INDIRECT_BUFFER
    // for indirect draws and dispatches, TRANSFER_DST for CommandBuffer::FillBuffer.
    VkBufferUsageFlags usage = 0;
    ResourceUsage resourceUsage = ResourceUsage::Immutable;
    VkDeviceSize size = 0;
    // Immutable buffers are filled through a staging upload; dynamic buffers are written through their mapping.
    const void* initialData = nullptr;
    const char* name = nullptr;
};

struct TextureCreation
{
    uint32_t width = 1;
    uint32_t height = 1;
    // 0 means the full chain down to 1x1.
    uint32_t mipLevels = 1;
    TextureType type = TextureType::Texture2D;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint8_t flags = kTextureFlagNone;
    // Level-major and tightly packed, faces back to back inside each level (the KTX2 level layout).
    // Holds level 0 only with kTextureFlagGenerateMips, otherwise every level.
    const void* initialData = nullptr;
    size_t initialDataSize = 0;
    const char* name = nullptr;
};

struct SamplerCreation
{
    VkFilter minFilter = VK_FILTER_LINEAR;
    VkFilter magFilter = VK_FILTER_LINEAR;
    VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    float maxLod = VK_LOD_CLAMP_NONE;
    // 0 disables anisotropic filtering; larger values are clamped to the device limit.
    float maxAnisotropy = 0.0f;
    // Depth comparison (shadow maps): sampling returns compareOp(reference, texel) instead of the texel.
    bool compareEnable = false;
    VkCompareOp compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    const char* name = nullptr;
};

// A pipeline is a compute pipeline when its only stage is VK_SHADER_STAGE_COMPUTE_BIT; its vertex input, raster, depth,
// blend and render pass state are then ignored.
struct ShaderStage
{
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
    const uint32_t* code = nullptr;
    size_t codeSize = 0; // bytes
};

struct ShaderStateCreation
{
    std::array<ShaderStage, kMaxShaderStages> stages{};
    uint32_t stageCount = 0;

    ShaderStateCreation& AddStage(VkShaderStageFlagBits stage, const uint32_t* code, size_t codeSizeBytes);
};

struct DescriptorBinding
{
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    uint32_t index = 0;
    uint32_t count = 1;
    // Compute layouts must name VK_SHADER_STAGE_COMPUTE_BIT.
    VkShaderStageFlags stages = VK_SHADER_STAGE_ALL_GRAPHICS;
};

struct DescriptorSetLayoutCreation
{
    std::array<DescriptorBinding, kMaxDescriptorsPerSet> bindings{};
    uint32_t bindingCount = 0;
    uint32_t setIndex = 0;
    const char* name = nullptr;

    DescriptorSetLayoutCreation& AddBinding(const DescriptorBinding& binding);
};

struct DescriptorResource
{
    uint32_t binding = 0;
    BufferHandle buffer;
    VkDeviceSize bufferRange = VK_WHOLE_SIZE;
    TextureHandle texture;
    // Invalid means the texture's linked sampler, or the device default.
    SamplerHandle sampler;
};

struct DescriptorSetCreation
{
    DescriptorSetLayoutHandle layout;
    std::array<DescriptorResource, kMaxDescriptorsPerSet> resources{};
    uint32_t resourceCount = 0;
    const char* name = nullptr;

    // For UNIFORM_BUFFER_DYNAMIC bindings range must be the size of one element of the dynamic buffer.
    DescriptorSetCreation& AddBuffer(BufferHandle buffer, uint32_t binding, VkDeviceSize range = VK_WHOLE_SIZE);
    DescriptorSetCreation& AddTexture(TextureHandle texture, uint32_t binding, SamplerHandle sampler = {});
};

struct VertexStream
{
    uint32_t binding = 0;
    uint32_t stride = 0;
    VkVertexInputRate inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
};

struct VertexAttribute
{
    uint32_t location = 0;
    uint32_t binding = 0;
    uint32_t offset = 0;
    VkFormat format = VK_FORMAT_R32G32B32_SFLOAT;
};

struct VertexInputCreation
{
    std::array<VertexStream, kMaxVertexStreams> streams{};
    uint32_t streamCount = 0;
    std::array<VertexAttribute, kMaxVertexAttributes> attributes{};
    uint32_t attributeCount = 0;

    VertexInputCreation& AddStream(const VertexStream& stream);
    VertexInputCreation& AddAttribute(const VertexAttribute& attribute);
};

struct RasterizationCreation
{
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    // Static depth bias (no depthBiasClamp, which needs a device feature).
    bool depthBiasEnable = false;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
};

struct DepthStencilCreation
{
    bool depthTestEnable = false;
    bool depthWriteEnable = false;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
};

// Applied to every color output of the pipeline.
struct BlendStateCreation
{
    bool blendEnable = false;
    VkBlendFactor sourceColor = VK_BLEND_FACTOR_SRC_ALPHA;
    VkBlendFactor destinationColor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendOp colorOperation = VK_BLEND_OP_ADD;
    VkBlendFactor sourceAlpha = VK_BLEND_FACTOR_ONE;
    VkBlendFactor destinationAlpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendOp alphaOperation = VK_BLEND_OP_ADD;
    VkColorComponentFlags writeMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
};

// Describes the attachments of a render pass. Pipelines are built against it, and the device caches one VkRenderPass per
// distinct output.
struct RenderPassOutput
{
    std::array<VkFormat, kMaxImageOutputs> colorFormats{};
    uint32_t colorCount = 0;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    RenderPassOperation colorOperation = RenderPassOperation::Clear;
    RenderPassOperation depthOperation = RenderPassOperation::Clear;
    // Depth is only kept when a later pass loads it; mobile tilers then skip the write back.
    bool storeDepth = false;
    // Depth is stored and ends in SHADER_READ_ONLY for sampling by later passes (shadow maps).
    bool sampleDepth = false;
    // Swapchain outputs end in PRESENT_SRC, offscreen color outputs in SHADER_READ_ONLY for sampling by later passes.
    bool presentable = false;

    RenderPassOutput& AddColor(VkFormat format);
    RenderPassOutput& SetDepth(VkFormat format);
    RenderPassOutput& SetOperations(RenderPassOperation color, RenderPassOperation depth);
    uint64_t Hash() const;
};

struct RenderPassCreation
{
    std::array<TextureHandle, kMaxImageOutputs> outputTextures{};
    uint32_t renderTargetCount = 0;
    TextureHandle depthStencilTexture;
    RenderPassOperation colorOperation = RenderPassOperation::Clear;
    RenderPassOperation depthOperation = RenderPassOperation::Clear;
    bool storeDepth = false;
    bool sampleDepth = false;
    const char* name = nullptr;

    RenderPassCreation& AddRenderTexture(TextureHandle texture);
    RenderPassCreation& SetDepthStencilTexture(TextureHandle texture);
    RenderPassCreation& SetOperations(RenderPassOperation color, RenderPassOperation depth);
};

struct SpecializationConstant
{
    uint32_t id = 0;
    uint32_t value = 0;
};

struct PipelineCreation
{
    ShaderStateCreation shaders;
    VertexInputCreation vertexInput;
    RasterizationCreation rasterization;
    DepthStencilCreation depthStencil;
    BlendStateCreation blend;
    RenderPassOutput renderPass;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    std::array<DescriptorSetLayoutHandle, kMaxDescriptorSetLayouts> descriptorSetLayouts{};
    uint32_t descriptorSetLayoutCount = 0;
    uint32_t pushConstantSize = 0;
    // Ignored by compute pipelines, which always use VK_SHADER_STAGE_COMPUTE_BIT.
    VkShaderStageFlags pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // 32-bit constants applied to every stage.
    std::array<SpecializationConstant, kMaxSpecializationConstants> specializationConstants{};
    uint32_t specializationConstantCount = 0;

    const char* name = nullptr;

    PipelineCreation& AddDescriptorSetLayout(DescriptorSetLayoutHandle layout);
    PipelineCreation& AddSpecializationConstant(uint32_t id, uint32_t value);
};

// ---------------------------------------------------------------------------------------------------------------------
// Barriers, after Raptor's ExecutionBarrier / ImageBarrier / MemoryBarrier. One ExecutionBarrier is recorded as a single
// vkCmdPipelineBarrier whose stage masks are the union of its entries.

struct ImageBarrier
{
    TextureHandle texture;
    ResourceStates newState = kResourceStateUndefined;
    // Defaults to the texture's tracked state. Required for a partial range, whose subresources may differ.
    std::optional<ResourceStates> oldState;
    uint32_t baseMipLevel = 0;
    uint32_t mipLevelCount = VK_REMAINING_MIP_LEVELS;
    uint32_t baseArrayLayer = 0;
    uint32_t arrayLayerCount = VK_REMAINING_ARRAY_LAYERS;
};

struct BufferBarrier
{
    BufferHandle buffer;
    ResourceStates oldState = kResourceStateUndefined;
    ResourceStates newState = kResourceStateUndefined;
    VkDeviceSize offset = 0;
    VkDeviceSize size = VK_WHOLE_SIZE;
};

// A global memory barrier (Raptor's MemoryBarrier, renamed: <windows.h> defines a MemoryBarrier macro). Covers every
// buffer and the memory of every image, without layout changes.
struct GlobalBarrier
{
    ResourceStates oldState = kResourceStateUndefined;
    ResourceStates newState = kResourceStateUndefined;
};

struct ExecutionBarrier
{
    std::array<ImageBarrier, kMaxBarriersPerBatch> images{};
    uint32_t imageCount = 0;
    std::array<BufferBarrier, kMaxBarriersPerBatch> buffers{};
    uint32_t bufferCount = 0;
    std::optional<GlobalBarrier> global;

    // Whole texture from its tracked state; the tracked state becomes newState.
    ExecutionBarrier& AddImage(TextureHandle texture, ResourceStates newState);
    // A partial range leaves the tracked state alone; see GpuDevice::SetTextureState.
    ExecutionBarrier& AddImage(const ImageBarrier& barrier);
    ExecutionBarrier& AddBuffer(BufferHandle buffer, ResourceStates oldState, ResourceStates newState);
    ExecutionBarrier& AddBuffer(const BufferBarrier& barrier);
    ExecutionBarrier& SetGlobal(ResourceStates oldState, ResourceStates newState);
    bool IsEmpty() const { return imageCount == 0 && bufferCount == 0 && !global.has_value(); }
};

// ---------------------------------------------------------------------------------------------------------------------
// Pooled resources

struct Buffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    ResourceUsage resourceUsage = ResourceUsage::Immutable;
    // Persistently mapped for dynamic buffers, null otherwise.
    void* mappedData = nullptr;
    std::string name;
};

struct Texture
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 1;
    uint32_t layerCount = 1;
    TextureType type = TextureType::Texture2D;
    uint8_t flags = kTextureFlagNone;
    // State of every subresource as of the last recorded command, as Raptor's Texture::state. Kept by uploads, render
    // passes (BindPass/EndPass) and CommandBuffer barriers; uploaded and plain textures start as kResourceStateShaderResource,
    // render targets as kResourceStateUndefined.
    ResourceStates state = kResourceStateUndefined;
    SamplerHandle sampler;
    std::string name;
};

struct Sampler
{
    VkSampler sampler = VK_NULL_HANDLE;
    std::string name;
};

struct DescriptorSetLayout
{
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    std::array<DescriptorBinding, kMaxDescriptorsPerSet> bindings{};
    uint32_t bindingCount = 0;
    uint32_t setIndex = 0;
};

struct DescriptorSet
{
    VkDescriptorSet set = VK_NULL_HANDLE;
    DescriptorSetLayoutHandle layout;
};

struct Pipeline
{
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    VkShaderStageFlags pushConstantStages = 0;
    uint32_t pushConstantSize = 0;
    std::string name;
};

struct RenderPass
{
    // Owned by the device's render pass cache.
    VkRenderPass renderPass = VK_NULL_HANDLE;
    // Null for the swapchain pass, whose framebuffers belong to the Swapchain.
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    RenderPassOutput output;
    std::array<TextureHandle, kMaxImageOutputs> outputTextures{};
    uint32_t renderTargetCount = 0;
    TextureHandle depthStencilTexture;
    uint32_t width = 0;
    uint32_t height = 0;
    bool isSwapchain = false;
    std::string name;
};

} // namespace mengine::rhi
