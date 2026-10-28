#include "render/passes/SkyboxPass.hpp"

#include "rhi/CommandBuffer.hpp"
#include "rhi/GpuDevice.hpp"
#include "rhi/VulkanUtils.hpp"

#include <vector>

namespace mengine::render {

void SkyboxPass::Init(rhi::GpuDevice& device, const rhi::RenderPassOutput& output, rhi::DescriptorSetLayoutHandle frameLayout)
{
    m_device = &device;
    const std::vector<uint32_t> vertexCode = rhi::LoadSpirvAsset("shaders/Skybox.VertexMain.spv");
    const std::vector<uint32_t> fragmentCode = rhi::LoadSpirvAsset("shaders/Skybox.FragmentMain.spv");

    rhi::PipelineCreation creation;
    creation.name = "Skybox";
    creation.shaders.AddStage(VK_SHADER_STAGE_VERTEX_BIT, vertexCode.data(), vertexCode.size() * sizeof(uint32_t))
        .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentCode.data(), fragmentCode.size() * sizeof(uint32_t));
    creation.rasterization.cullMode = VK_CULL_MODE_NONE;
    // Only pixels still at the cleared far depth pass.
    creation.depthStencil = {.depthTestEnable = true, .depthWriteEnable = false, .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};
    creation.renderPass = output;
    creation.AddDescriptorSetLayout(frameLayout);
    m_pipeline = device.CreatePipeline(creation);
}

void SkyboxPass::Shutdown()
{
    if (m_device != nullptr)
    {
        m_device->DestroyPipeline(m_pipeline);
        m_pipeline = {};
        m_device = nullptr;
    }
}

void SkyboxPass::Record(rhi::CommandBuffer& commandBuffer, rhi::DescriptorSetHandle frameSet, uint32_t frameOffset)
{
    commandBuffer.BindPipeline(m_pipeline);
    commandBuffer.BindDescriptorSet(frameSet, 0, {&frameOffset, 1});
    commandBuffer.Draw(3);
}

} // namespace mengine::render
