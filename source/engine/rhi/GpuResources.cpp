#include "rhi/GpuResources.hpp"

#include "core/Assert.hpp"

namespace mengine::rhi {

namespace {

constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void HashCombine(uint64_t& hash, uint64_t value)
{
    for (int byte = 0; byte < 8; ++byte)
    {
        hash ^= (value >> (byte * 8)) & 0xffu;
        hash *= kFnvPrime;
    }
}

} // namespace

ShaderStateCreation& ShaderStateCreation::AddStage(VkShaderStageFlagBits stage, const uint32_t* code, size_t codeSizeBytes)
{
    MENGINE_ASSERT(stageCount < kMaxShaderStages);
    if (stageCount < kMaxShaderStages)
    {
        stages[stageCount++] = ShaderStage{.stage = stage, .code = code, .codeSize = codeSizeBytes};
    }
    return *this;
}

DescriptorSetLayoutCreation& DescriptorSetLayoutCreation::AddBinding(const DescriptorBinding& binding)
{
    MENGINE_ASSERT(bindingCount < kMaxDescriptorsPerSet);
    if (bindingCount < kMaxDescriptorsPerSet)
    {
        bindings[bindingCount++] = binding;
    }
    return *this;
}

DescriptorSetCreation& DescriptorSetCreation::AddBuffer(BufferHandle buffer, uint32_t binding, VkDeviceSize range)
{
    MENGINE_ASSERT(resourceCount < kMaxDescriptorsPerSet);
    if (resourceCount < kMaxDescriptorsPerSet)
    {
        resources[resourceCount++] = DescriptorResource{.binding = binding, .buffer = buffer, .bufferRange = range};
    }
    return *this;
}

DescriptorSetCreation& DescriptorSetCreation::AddTexture(TextureHandle texture, uint32_t binding, SamplerHandle sampler)
{
    MENGINE_ASSERT(resourceCount < kMaxDescriptorsPerSet);
    if (resourceCount < kMaxDescriptorsPerSet)
    {
        resources[resourceCount++] = DescriptorResource{.binding = binding, .texture = texture, .sampler = sampler};
    }
    return *this;
}

VertexInputCreation& VertexInputCreation::AddStream(const VertexStream& stream)
{
    MENGINE_ASSERT(streamCount < kMaxVertexStreams);
    if (streamCount < kMaxVertexStreams)
    {
        streams[streamCount++] = stream;
    }
    return *this;
}

VertexInputCreation& VertexInputCreation::AddAttribute(const VertexAttribute& attribute)
{
    MENGINE_ASSERT(attributeCount < kMaxVertexAttributes);
    if (attributeCount < kMaxVertexAttributes)
    {
        attributes[attributeCount++] = attribute;
    }
    return *this;
}

RenderPassOutput& RenderPassOutput::AddColor(VkFormat format)
{
    MENGINE_ASSERT(colorCount < kMaxImageOutputs);
    if (colorCount < kMaxImageOutputs)
    {
        colorFormats[colorCount++] = format;
    }
    return *this;
}

RenderPassOutput& RenderPassOutput::SetDepth(VkFormat format)
{
    depthFormat = format;
    return *this;
}

RenderPassOutput& RenderPassOutput::SetOperations(RenderPassOperation color, RenderPassOperation depth)
{
    colorOperation = color;
    depthOperation = depth;
    return *this;
}

uint64_t RenderPassOutput::Hash() const
{
    uint64_t hash = kFnvOffsetBasis;
    HashCombine(hash, colorCount);
    for (uint32_t index = 0; index < colorCount; ++index)
    {
        HashCombine(hash, static_cast<uint64_t>(colorFormats[index]));
    }
    HashCombine(hash, static_cast<uint64_t>(depthFormat));
    HashCombine(hash, static_cast<uint64_t>(colorOperation));
    HashCombine(hash, static_cast<uint64_t>(depthOperation));
    HashCombine(hash, storeDepth ? 1u : 0u);
    HashCombine(hash, sampleDepth ? 1u : 0u);
    HashCombine(hash, presentable ? 1u : 0u);
    return hash;
}

RenderPassCreation& RenderPassCreation::AddRenderTexture(TextureHandle texture)
{
    MENGINE_ASSERT(renderTargetCount < kMaxImageOutputs);
    if (renderTargetCount < kMaxImageOutputs)
    {
        outputTextures[renderTargetCount++] = texture;
    }
    return *this;
}

RenderPassCreation& RenderPassCreation::SetDepthStencilTexture(TextureHandle texture)
{
    depthStencilTexture = texture;
    return *this;
}

RenderPassCreation& RenderPassCreation::SetOperations(RenderPassOperation color, RenderPassOperation depth)
{
    colorOperation = color;
    depthOperation = depth;
    return *this;
}

PipelineCreation& PipelineCreation::AddDescriptorSetLayout(DescriptorSetLayoutHandle layout)
{
    MENGINE_ASSERT(descriptorSetLayoutCount < kMaxDescriptorSetLayouts);
    if (descriptorSetLayoutCount < kMaxDescriptorSetLayouts)
    {
        descriptorSetLayouts[descriptorSetLayoutCount++] = layout;
    }
    return *this;
}

PipelineCreation& PipelineCreation::AddSpecializationConstant(uint32_t id, uint32_t value)
{
    MENGINE_ASSERT(specializationConstantCount < kMaxSpecializationConstants);
    if (specializationConstantCount < kMaxSpecializationConstants)
    {
        specializationConstants[specializationConstantCount++] = SpecializationConstant{.id = id, .value = value};
    }
    return *this;
}

} // namespace mengine::rhi
