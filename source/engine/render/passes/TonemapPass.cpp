#include "render/passes/TonemapPass.hpp"

#include "render/RenderTypes.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/GpuDevice.hpp"
#include "rhi/ShaderReflection.hpp"
#include "rhi/VulkanUtils.hpp"

#include <vector>

namespace mengine::render {

void TonemapPass::Init(rhi::GpuDevice& device, const rhi::RenderPassOutput& output)
{
    m_device = &device;

    const std::vector<uint32_t> vertexCode = rhi::LoadSpirvAsset("shaders/Tonemap.VertexMain.spv");
    const std::vector<uint32_t> fragmentCode = rhi::LoadSpirvAsset("shaders/Tonemap.FragmentMain.spv");
    rhi::PipelineCreation creation;
    creation.name = "Tonemap";
    creation.shaders.AddStage(VK_SHADER_STAGE_VERTEX_BIT, vertexCode.data(), vertexCode.size() * sizeof(uint32_t))
        .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentCode.data(), fragmentCode.size() * sizeof(uint32_t));

    // The private set 0 (the HDR input) comes straight from the shaders.
    const rhi::ShaderReflection reflection = rhi::ReflectShaderState(creation.shaders, creation.name);
    m_layout = device.CreateDescriptorSetLayout(rhi::BuildDescriptorSetLayout(reflection, 0, "TonemapLayout"));

    // Same resolution as the swapchain: nearest sampling reads every texel exactly once.
    m_sampler = device.CreateSampler({
        .minFilter = VK_FILTER_NEAREST,
        .magFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0f,
        .name = "TonemapSampler",
    });

    creation.rasterization.cullMode = VK_CULL_MODE_NONE;
    creation.renderPass = output;
    creation.AddDescriptorSetLayout(m_layout);
    creation.pushConstantSize = sizeof(TonemapConstants);
    creation.pushConstantStages = VK_SHADER_STAGE_FRAGMENT_BIT;
    m_pipeline = device.CreatePipeline(creation);
}

void TonemapPass::Shutdown()
{
    if (m_device == nullptr)
    {
        return;
    }
    m_device->DestroyPipeline(m_pipeline);
    m_device->DestroyDescriptorSet(m_set);
    m_device->DestroySampler(m_sampler);
    m_device->DestroyDescriptorSetLayout(m_layout);
    *this = TonemapPass{};
}

void TonemapPass::SetInput(rhi::TextureHandle hdrColor)
{
    rhi::DescriptorSetCreation creation{.layout = m_layout, .name = "TonemapInput"};
    creation.AddTexture(hdrColor, 0, m_sampler);
    if (m_set.IsValid())
    {
        m_device->UpdateDescriptorSet(m_set, creation);
    }
    else
    {
        m_set = m_device->CreateDescriptorSet(creation);
    }
}

void TonemapPass::Record(rhi::CommandBuffer& commandBuffer, float exposure, bool encodeSrgb)
{
    const TonemapConstants constants{.exposure = exposure, .encodeSrgb = encodeSrgb ? 1u : 0u};
    commandBuffer.BindPipeline(m_pipeline);
    commandBuffer.BindDescriptorSet(m_set, 0);
    commandBuffer.PushConstants(&constants, sizeof(constants));
    commandBuffer.Draw(3);
}

} // namespace mengine::render
