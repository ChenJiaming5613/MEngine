#include "render/passes/ShadowPass.hpp"

#include "render/Scene.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/GpuDevice.hpp"
#include "rhi/VulkanUtils.hpp"

#include <string>

namespace mengine::render {

void ShadowPass::Init(rhi::GpuDevice& device, rhi::DescriptorSetLayoutHandle frameLayout,
    rhi::DescriptorSetLayoutHandle materialLayout)
{
    m_device = &device;
    m_frameLayout = frameLayout;
    m_materialLayout = materialLayout;
    m_vertexCode = rhi::LoadSpirvAsset("shaders/Shadow.VertexMain.spv");
    m_vertexMaskCode = rhi::LoadSpirvAsset("shaders/Shadow.VertexMainMask.spv");
    m_fragmentMaskCode = rhi::LoadSpirvAsset("shaders/Shadow.FragmentMask.spv");

    // D16 is guaranteed to support sampling and depth attachments; an orthographic projection keeps depth linear.
    m_texture = device.CreateTexture({
        .width = kSize,
        .height = kSize,
        .format = kFormat,
        .flags = rhi::kTextureFlagRenderTarget,
        .name = "ShadowMap",
    });

    rhi::RenderPassCreation passCreation{.name = "Shadow"};
    passCreation.SetDepthStencilTexture(m_texture).SetOperations(rhi::RenderPassOperation::DontCare, rhi::RenderPassOperation::Clear);
    passCreation.sampleDepth = true;
    m_renderPass = device.CreateRenderPass(passCreation);
    m_output = device.AccessRenderPass(m_renderPass)->output;

    // With linear filtering every compare tap is a hardware 2x2 PCF; otherwise the 3x3 taps alone soften the edge.
    const VkFilter filter = device.SupportsLinearFilter(kFormat) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    m_sampler = device.CreateSampler({
        .minFilter = filter,
        .magFilter = filter,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0f,
        .compareEnable = true,
        .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .name = "ShadowSampler",
    });
}

void ShadowPass::Shutdown()
{
    if (m_device == nullptr)
    {
        return;
    }
    for (rhi::PipelineHandle& pipeline : m_pipelines)
    {
        m_device->DestroyPipeline(pipeline);
        pipeline = {};
    }
    m_device->DestroyRenderPass(m_renderPass);
    m_device->DestroySampler(m_sampler);
    m_device->DestroyTexture(m_texture);
    *this = ShadowPass{};
}

rhi::PipelineHandle ShadowPass::Pipeline(uint32_t key)
{
    rhi::PipelineHandle& pipeline = m_pipelines[key];
    if (pipeline.IsValid())
    {
        return pipeline;
    }

    const bool alphaMask = (key & kPipelineKeyAlphaMask) != 0;
    const std::string name = "Shadow#" + std::to_string(key);
    rhi::PipelineCreation creation;
    creation.name = name.c_str();
    creation.vertexInput.AddStream({.binding = kVertexStreamPosition, .stride = 3 * sizeof(float)})
        .AddAttribute({.location = 0, .binding = kVertexStreamPosition, .format = VK_FORMAT_R32G32B32_SFLOAT});
    if (alphaMask)
    {
        creation.shaders
            .AddStage(VK_SHADER_STAGE_VERTEX_BIT, m_vertexMaskCode.data(), m_vertexMaskCode.size() * sizeof(uint32_t))
            .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, m_fragmentMaskCode.data(), m_fragmentMaskCode.size() * sizeof(uint32_t));
        creation.vertexInput.AddStream({.binding = kVertexStreamTexCoord, .stride = 2 * sizeof(float)})
            .AddAttribute({.location = 2, .binding = kVertexStreamTexCoord, .format = VK_FORMAT_R32G32_SFLOAT});
    }
    else
    {
        // Depth only: no fragment shader keeps the fast early depth path.
        creation.shaders.AddStage(VK_SHADER_STAGE_VERTEX_BIT, m_vertexCode.data(), m_vertexCode.size() * sizeof(uint32_t));
    }

    // lightViewProjection flips Y like the camera, so the forward pass's winding rules apply unchanged.
    creation.rasterization.cullMode = (key & kPipelineKeyDoubleSided) != 0 ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    creation.rasterization.frontFace =
        (key & kPipelineKeyMirrored) != 0 ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    creation.rasterization.depthBiasEnable = true;
    creation.rasterization.depthBiasConstantFactor = 1.25f;
    creation.rasterization.depthBiasSlopeFactor = 1.75f;
    creation.depthStencil = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};
    creation.renderPass = m_output;
    creation.AddDescriptorSetLayout(m_frameLayout).AddDescriptorSetLayout(m_materialLayout);
    creation.pushConstantSize = sizeof(DrawConstants);
    creation.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;

    pipeline = m_device->CreatePipeline(creation);
    return pipeline;
}

void ShadowPass::WarmPipelines(const Scene& scene)
{
    for (const SceneDraw& draw : scene.draws)
    {
        Pipeline(draw.pipelineKey & kKeyMask);
    }
}

void ShadowPass::Record(rhi::CommandBuffer& commandBuffer, const Scene& scene, bool drawScene,
    rhi::DescriptorSetHandle frameSet, uint32_t frameOffset)
{
    commandBuffer.SetClearDepth(1.0f);
    commandBuffer.BindPass(m_renderPass);
    if (drawScene)
    {
        uint32_t boundKey = ~0u;
        uint32_t boundMaterial = ~0u;
        for (const SceneDraw& draw : scene.draws)
        {
            const uint32_t key = draw.pipelineKey & kKeyMask;
            const bool alphaMask = (key & kPipelineKeyAlphaMask) != 0;
            if (key != boundKey)
            {
                commandBuffer.BindPipeline(Pipeline(key));
                commandBuffer.BindDescriptorSet(frameSet, 0, {&frameOffset, 1});
                boundKey = key;
                boundMaterial = ~0u;
            }
            if (alphaMask && draw.material != boundMaterial)
            {
                commandBuffer.BindDescriptorSet(scene.materials[draw.material].descriptorSet, 1);
                boundMaterial = draw.material;
            }

            const ScenePrimitive& primitive = scene.primitives[draw.primitive];
            commandBuffer.BindVertexBuffer(
                primitive.vertexBuffers[kVertexStreamPosition], kVertexStreamPosition, primitive.vertexOffsets[kVertexStreamPosition]);
            if (alphaMask)
            {
                commandBuffer.BindVertexBuffer(
                    primitive.vertexBuffers[kVertexStreamTexCoord], kVertexStreamTexCoord, primitive.vertexOffsets[kVertexStreamTexCoord]);
            }
            commandBuffer.BindIndexBuffer(primitive.indexBuffer, primitive.indexOffset, primitive.indexType);
            commandBuffer.PushConstants(&draw.constants, sizeof(DrawConstants));
            commandBuffer.DrawIndexed(primitive.indexCount);
        }
    }
    commandBuffer.EndPass();
}

} // namespace mengine::render
