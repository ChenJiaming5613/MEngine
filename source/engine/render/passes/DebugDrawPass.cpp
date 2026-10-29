#include "render/passes/DebugDrawPass.hpp"

#include "render/DebugDraw.hpp"
#include "render/RenderTypes.hpp"
#include "render/RenderView.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/GpuDevice.hpp"
#include "rhi/VulkanUtils.hpp"

#include <algorithm>
#include <cstddef>

namespace mengine::render {

void DebugDrawPass::Init(rhi::GpuDevice& device, rhi::DescriptorSetLayoutHandle frameLayout, const DebugDraw& lines)
{
    m_device = &device;
    m_lines = &lines;
    m_frameLayout = frameLayout;
    m_vertexCode = rhi::LoadSpirvAsset("shaders/DebugLine.VertexMain.spv");
    m_fragmentCode = rhi::LoadSpirvAsset("shaders/DebugLine.FragmentMain.spv");
}

void DebugDrawPass::Shutdown()
{
    if (m_device != nullptr)
    {
        m_device->DestroyPipeline(m_pipeline);
        *this = DebugDrawPass{};
    }
}

void DebugDrawPass::Setup(FrameGraphBuilder& builder)
{
    builder.ColorAttachment(kResourceHdrColor, rhi::RenderPassOperation::Load);
    builder.DepthAttachment(kResourceSceneDepth, rhi::RenderPassOperation::Load);
}

bool DebugDrawPass::IsActive() const
{
    return m_lines != nullptr && m_lines->VertexCount() > 0;
}

void DebugDrawPass::OnCompiled(const FrameGraphResources& resources)
{
    const rhi::RenderPassOutput& output = resources.GetRenderPassOutput();
    if (m_pipeline.IsValid() && IsRenderPassCompatible(output, m_output))
    {
        return;
    }
    m_device->DestroyPipeline(m_pipeline);
    m_output = output;

    rhi::PipelineCreation creation;
    creation.name = "DebugLine";
    creation.shaders.AddStage(VK_SHADER_STAGE_VERTEX_BIT, m_vertexCode.data(), m_vertexCode.size() * sizeof(uint32_t))
        .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, m_fragmentCode.data(), m_fragmentCode.size() * sizeof(uint32_t));
    creation.vertexInput.AddStream({.binding = 0, .stride = sizeof(DebugVertex)})
        .AddAttribute({.location = 0, .binding = 0, .offset = offsetof(DebugVertex, position), .format = VK_FORMAT_R32G32B32_SFLOAT})
        .AddAttribute({.location = 1, .binding = 0, .offset = offsetof(DebugVertex, color), .format = VK_FORMAT_R8G8B8A8_UNORM});
    creation.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    creation.rasterization.cullMode = VK_CULL_MODE_NONE;
    // Hidden behind the scene, never occluding later lines.
    creation.depthStencil = {.depthTestEnable = true, .depthWriteEnable = false, .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};
    creation.renderPass = output;
    creation.AddDescriptorSetLayout(m_frameLayout);
    creation.pushConstantSize = sizeof(DebugLineConstants);
    creation.pushConstantStages = VK_SHADER_STAGE_FRAGMENT_BIT;
    m_pipeline = m_device->CreatePipeline(creation);
}

void DebugDrawPass::Execute(FrameGraphContext& context)
{
    const DebugLineConstants constants{.exposure = std::max(context.view.settings->exposure, 1e-4f)};
    rhi::CommandBuffer& commandBuffer = context.commandBuffer;
    const uint32_t frameOffset = context.view.frameOffset;
    commandBuffer.BindPipeline(m_pipeline);
    commandBuffer.BindDescriptorSet(context.view.frameSet, 0, {&frameOffset, 1});
    commandBuffer.PushConstants(&constants, sizeof(constants));
    commandBuffer.BindVertexBuffer(m_lines->Buffer(), 0, m_lines->BufferOffset());
    commandBuffer.Draw(m_lines->VertexCount());
}

} // namespace mengine::render
