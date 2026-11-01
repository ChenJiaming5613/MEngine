#include "render/passes/SkyboxPass.h"

#include "render/RenderView.h"
#include "rhi/CommandBuffer.h"
#include "rhi/GfxDevice.h"
#include "rhi/VulkanUtils.h"

namespace mengine::render {

void SkyboxPass::Init(rhi::GfxDevice& device, rhi::DescriptorSetLayoutHandle frameLayout)
{
    m_device = &device;
    m_frameLayout = frameLayout;
    m_vertexCode = rhi::LoadSpirvAsset("shaders/Skybox.VertexMain.spv");
    m_fragmentCode = rhi::LoadSpirvAsset("shaders/Skybox.FragmentMain.spv");
}

void SkyboxPass::Shutdown()
{
    if (m_device != nullptr)
    {
        m_device->DestroyPipeline(m_pipeline);
        *this = SkyboxPass{};
    }
}

void SkyboxPass::Setup(FrameGraphBuilder& builder)
{
    builder.ColorAttachment(kResourceHdrColor, rhi::RenderPassOperation::Load);
    builder.DepthAttachment(kResourceSceneDepth, rhi::RenderPassOperation::Load);
}

void SkyboxPass::OnCompiled(const FrameGraphResources& resources)
{
    const rhi::RenderPassOutput& output = resources.GetRenderPassOutput();
    if (m_pipeline.IsValid() && IsRenderPassCompatible(output, m_output))
    {
        return;
    }
    m_device->DestroyPipeline(m_pipeline);
    m_output = output;

    rhi::PipelineCreateInfo createInfo;
    createInfo.name = "Skybox";
    createInfo.shaders.AddStage(VK_SHADER_STAGE_VERTEX_BIT, m_vertexCode.data(), m_vertexCode.size() * sizeof(uint32_t))
        .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, m_fragmentCode.data(), m_fragmentCode.size() * sizeof(uint32_t));
    createInfo.rasterization.cullMode = VK_CULL_MODE_NONE;
    // Only pixels still at the cleared far depth pass.
    createInfo.depthStencil = {.depthTestEnable = true, .depthWriteEnable = false, .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};
    createInfo.renderPass = output;
    createInfo.AddDescriptorSetLayout(m_frameLayout);
    m_pipeline = m_device->CreatePipeline(createInfo);
}

void SkyboxPass::Execute(FrameGraphContext& context)
{
    rhi::CommandBuffer& commandBuffer = context.commandBuffer;
    const uint32_t frameOffset = context.view.frameOffset;
    commandBuffer.BindPipeline(m_pipeline);
    commandBuffer.BindDescriptorSet(context.view.frameSet, 0, {&frameOffset, 1});
    commandBuffer.Draw(3);
}

} // namespace mengine::render
