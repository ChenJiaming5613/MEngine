#include "render/passes/ForwardPbrPass.hpp"

#include "render/Scene.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/GpuDevice.hpp"
#include "rhi/VulkanUtils.hpp"

#include <string>

namespace mengine::render {

void ForwardPbrPass::Init(rhi::GpuDevice& device, const rhi::RenderPassOutput& output,
    rhi::DescriptorSetLayoutHandle frameLayout, rhi::DescriptorSetLayoutHandle materialLayout)
{
    m_device = &device;
    m_output = output;
    m_frameLayout = frameLayout;
    m_materialLayout = materialLayout;
    m_vertexCode = rhi::LoadSpirvAsset("shaders/Pbr.VertexMain.spv");
    m_vertexTangentCode = rhi::LoadSpirvAsset("shaders/Pbr.VertexMainTangent.spv");
    m_fragmentCode = rhi::LoadSpirvAsset("shaders/Pbr.FragmentMain.spv");
}

void ForwardPbrPass::Shutdown()
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
    m_device = nullptr;
}

rhi::PipelineHandle ForwardPbrPass::Pipeline(uint32_t key)
{
    rhi::PipelineHandle& pipeline = m_pipelines[key];
    if (pipeline.IsValid())
    {
        return pipeline;
    }

    const bool tangents = (key & kPipelineKeyTangents) != 0;
    const std::vector<uint32_t>& vertexCode = tangents ? m_vertexTangentCode : m_vertexCode;
    const std::string name = "ForwardPbr#" + std::to_string(key);

    rhi::PipelineCreation creation;
    creation.name = name.c_str();
    creation.shaders.AddStage(VK_SHADER_STAGE_VERTEX_BIT, vertexCode.data(), vertexCode.size() * sizeof(uint32_t))
        .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, m_fragmentCode.data(), m_fragmentCode.size() * sizeof(uint32_t));

    creation.vertexInput.AddStream({.binding = kVertexStreamPosition, .stride = 3 * sizeof(float)})
        .AddStream({.binding = kVertexStreamNormal, .stride = 3 * sizeof(float)})
        .AddStream({.binding = kVertexStreamTexCoord, .stride = 2 * sizeof(float)})
        .AddAttribute({.location = 0, .binding = kVertexStreamPosition, .format = VK_FORMAT_R32G32B32_SFLOAT})
        .AddAttribute({.location = 1, .binding = kVertexStreamNormal, .format = VK_FORMAT_R32G32B32_SFLOAT})
        .AddAttribute({.location = 2, .binding = kVertexStreamTexCoord, .format = VK_FORMAT_R32G32_SFLOAT});
    if (tangents)
    {
        creation.vertexInput.AddStream({.binding = kVertexStreamTangent, .stride = 4 * sizeof(float)})
            .AddAttribute({.location = 3, .binding = kVertexStreamTangent, .format = VK_FORMAT_R32G32B32A32_SFLOAT});
    }

    // The projection flips Y, which keeps glTF's counter-clockwise front faces counter-clockwise on screen.
    creation.rasterization.cullMode = (key & kPipelineKeyDoubleSided) != 0 ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    creation.rasterization.frontFace =
        (key & kPipelineKeyMirrored) != 0 ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    creation.depthStencil = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};
    creation.renderPass = m_output;
    creation.AddDescriptorSetLayout(m_frameLayout).AddDescriptorSetLayout(m_materialLayout);
    creation.pushConstantSize = sizeof(DrawConstants);
    creation.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
    creation.AddSpecializationConstant(0, (key & kPipelineKeyAlphaMask) != 0 ? 1u : 0u);

    pipeline = m_device->CreatePipeline(creation);
    return pipeline;
}

void ForwardPbrPass::WarmPipelines(const Scene& scene)
{
    for (const SceneDraw& draw : scene.draws)
    {
        Pipeline(draw.pipelineKey);
    }
}

void ForwardPbrPass::Record(rhi::CommandBuffer& commandBuffer, const Scene& scene, rhi::DescriptorSetHandle frameSet,
    uint32_t frameOffset)
{
    uint32_t boundKey = ~0u;
    uint32_t boundMaterial = ~0u;
    for (const SceneDraw& draw : scene.draws)
    {
        if (draw.pipelineKey != boundKey)
        {
            commandBuffer.BindPipeline(Pipeline(draw.pipelineKey));
            commandBuffer.BindDescriptorSet(frameSet, 0, {&frameOffset, 1});
            boundKey = draw.pipelineKey;
            boundMaterial = ~0u;
        }
        if (draw.material != boundMaterial)
        {
            commandBuffer.BindDescriptorSet(scene.materials[draw.material].descriptorSet, 1);
            boundMaterial = draw.material;
        }

        const ScenePrimitive& primitive = scene.primitives[draw.primitive];
        const uint32_t streamCount = primitive.hasTangents ? kVertexStreamCount : kVertexStreamTangent;
        for (uint32_t stream = 0; stream < streamCount; ++stream)
        {
            commandBuffer.BindVertexBuffer(primitive.vertexBuffers[stream], stream, primitive.vertexOffsets[stream]);
        }
        commandBuffer.BindIndexBuffer(primitive.indexBuffer, primitive.indexOffset, primitive.indexType);
        commandBuffer.PushConstants(&draw.constants, sizeof(DrawConstants));
        commandBuffer.DrawIndexed(primitive.indexCount);
    }
}

} // namespace mengine::render
