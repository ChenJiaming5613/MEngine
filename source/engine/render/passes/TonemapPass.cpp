#include "render/passes/TonemapPass.hpp"

#include "render/RenderTypes.hpp"
#include "render/RenderView.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/GpuDevice.hpp"
#include "rhi/ShaderReflection.hpp"
#include "rhi/VulkanUtils.hpp"

namespace mengine::render {

namespace {

rhi::ShaderStateCreation TonemapShaders(const std::vector<uint32_t>& vertexCode, const std::vector<uint32_t>& fragmentCode)
{
    rhi::ShaderStateCreation shaders;
    shaders.AddStage(VK_SHADER_STAGE_VERTEX_BIT, vertexCode.data(), vertexCode.size() * sizeof(uint32_t))
        .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentCode.data(), fragmentCode.size() * sizeof(uint32_t));
    return shaders;
}

} // namespace

void TonemapPass::Init(rhi::GpuDevice& device)
{
    m_device = &device;
    m_vertexCode = rhi::LoadSpirvAsset("shaders/Tonemap.VertexMain.spv");
    m_fragmentCode = rhi::LoadSpirvAsset("shaders/Tonemap.FragmentMain.spv");

    // The private set 0 (the HDR input) comes straight from the shaders.
    const rhi::ShaderReflection reflection = rhi::ReflectShaderState(TonemapShaders(m_vertexCode, m_fragmentCode), "Tonemap");
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

void TonemapPass::Setup(FrameGraphBuilder& builder)
{
    m_input = builder.Read(kResourceHdrColor, rhi::kResourceStatePixelShaderResource);
    builder.ImportBackbuffer(kResourceBackbuffer);
    // Every pixel is written.
    builder.ColorAttachment(kResourceBackbuffer, rhi::RenderPassOperation::DontCare);
}

void TonemapPass::OnCompiled(const FrameGraphResources& resources)
{
    rhi::DescriptorSetCreation setCreation{.layout = m_layout, .name = "TonemapInput"};
    setCreation.AddTexture(resources.GetTexture(m_input), 0, m_sampler);
    if (m_set.IsValid())
    {
        m_device->UpdateDescriptorSet(m_set, setCreation);
    }
    else
    {
        m_set = m_device->CreateDescriptorSet(setCreation);
    }

    const rhi::RenderPassOutput& output = resources.GetRenderPassOutput();
    if (m_pipeline.IsValid() && IsRenderPassCompatible(output, m_output))
    {
        return;
    }
    m_device->DestroyPipeline(m_pipeline);
    m_output = output;

    rhi::PipelineCreation creation;
    creation.name = "Tonemap";
    creation.shaders = TonemapShaders(m_vertexCode, m_fragmentCode);
    creation.rasterization.cullMode = VK_CULL_MODE_NONE;
    creation.renderPass = output;
    creation.AddDescriptorSetLayout(m_layout);
    creation.pushConstantSize = sizeof(TonemapConstants);
    creation.pushConstantStages = VK_SHADER_STAGE_FRAGMENT_BIT;
    m_pipeline = m_device->CreatePipeline(creation);
}

void TonemapPass::Execute(FrameGraphContext& context)
{
    const TonemapConstants constants{
        .exposure = context.view.settings->exposure,
        .encodeSrgb = context.view.encodeSrgb ? 1u : 0u,
    };
    rhi::CommandBuffer& commandBuffer = context.commandBuffer;
    commandBuffer.BindPipeline(m_pipeline);
    commandBuffer.BindDescriptorSet(m_set, 0);
    commandBuffer.PushConstants(&constants, sizeof(constants));
    commandBuffer.Draw(3);
}

} // namespace mengine::render
