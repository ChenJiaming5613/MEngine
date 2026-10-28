#include "render/Renderer.hpp"

#include "render/Camera.hpp"
#include "render/GltfLoader.hpp"
#include "render/ImGuiLayer.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/GpuDevice.hpp"

#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cstring>

namespace mengine::render {

void Renderer::Init(rhi::GpuDevice& device)
{
    m_device = &device;
    const VkExtent2D extent = device.GetSwapchainExtent();

    rhi::DescriptorSetLayoutCreation frameLayout;
    frameLayout.name = "FrameLayout";
    frameLayout.setIndex = 0;
    frameLayout
        .AddBinding({.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .index = 0,
            .stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT})
        .AddBinding({.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .index = 1, .stages = VK_SHADER_STAGE_FRAGMENT_BIT})
        .AddBinding({.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .index = 2, .stages = VK_SHADER_STAGE_FRAGMENT_BIT})
        .AddBinding({.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .index = 3, .stages = VK_SHADER_STAGE_FRAGMENT_BIT});
    m_frameLayout = device.CreateDescriptorSetLayout(frameLayout);

    rhi::DescriptorSetLayoutCreation materialLayout;
    materialLayout.name = "MaterialLayout";
    materialLayout.setIndex = 1;
    materialLayout.AddBinding(
        {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .index = kMaterialBindingData, .stages = VK_SHADER_STAGE_FRAGMENT_BIT});
    for (uint32_t binding = kMaterialBindingBaseColor; binding <= kMaterialBindingEmissive; ++binding)
    {
        materialLayout.AddBinding(
            {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .index = binding, .stages = VK_SHADER_STAGE_FRAGMENT_BIT});
    }
    m_materialLayout = device.CreateDescriptorSetLayout(materialLayout);

    m_hdrColor = device.CreateTexture({
        .width = extent.width,
        .height = extent.height,
        .format = kHdrFormat,
        .flags = rhi::kTextureFlagRenderTarget,
        .name = "HdrColor",
    });
    m_depth = device.CreateTexture({
        .width = extent.width,
        .height = extent.height,
        .format = device.GetDepthFormat(),
        .flags = rhi::kTextureFlagRenderTarget,
        .name = "SceneDepth",
    });
    rhi::RenderPassCreation passCreation{.name = "Forward"};
    passCreation.AddRenderTexture(m_hdrColor).SetDepthStencilTexture(m_depth).SetOperations(
        rhi::RenderPassOperation::Clear, rhi::RenderPassOperation::Clear);
    m_forwardPass = device.CreateRenderPass(passCreation);

    const rhi::RenderPassOutput forwardOutput = device.AccessRenderPass(m_forwardPass)->output;
    m_forwardPbr.Init(device, forwardOutput, m_frameLayout, m_materialLayout);
    m_skybox.Init(device, forwardOutput, m_frameLayout);
    m_tonemap.Init(device, device.GetSwapchainOutput());
    m_tonemap.SetInput(m_hdrColor);

    LoadEnvironment(std::nullopt);
}

void Renderer::Shutdown()
{
    if (m_device == nullptr)
    {
        return;
    }
    rhi::GpuDevice& device = *m_device;
    m_scene.Destroy(device);
    m_environment.Destroy(device);
    m_tonemap.Shutdown();
    m_skybox.Shutdown();
    m_forwardPbr.Shutdown();
    device.DestroyRenderPass(m_forwardPass);
    device.DestroyTexture(m_depth);
    device.DestroyTexture(m_hdrColor);
    device.DestroyDescriptorSet(m_frameSet);
    device.DestroyDescriptorSetLayout(m_materialLayout);
    device.DestroyDescriptorSetLayout(m_frameLayout);
    *this = Renderer{};
}

bool Renderer::LoadScene(std::string_view assetPath)
{
    return LoadGltfScene(*m_device, assetPath, m_materialLayout, m_scene);
}

void Renderer::LoadEnvironment(std::optional<std::string_view> directory)
{
    render::LoadEnvironment(*m_device, directory, m_environment);
    UpdateFrameSet();
}

void Renderer::UpdateFrameSet()
{
    rhi::DescriptorSetCreation creation{.layout = m_frameLayout, .name = "FrameSet"};
    creation.AddBuffer(m_device->GetDynamicBuffer(), 0, sizeof(FrameConstants))
        .AddTexture(m_environment.irradiance, 1, m_environment.cubeSampler)
        .AddTexture(m_environment.specular, 2, m_environment.cubeSampler)
        .AddTexture(m_environment.brdfLut, 3, m_environment.lutSampler);
    if (m_frameSet.IsValid())
    {
        m_device->UpdateDescriptorSet(m_frameSet, creation);
    }
    else
    {
        m_frameSet = m_device->CreateDescriptorSet(creation);
    }
}

void Renderer::Render(const Camera& camera, const RenderSettings& settings, ImGuiLayer* imGui)
{
    rhi::GpuDevice& device = *m_device;
    const VkExtent2D extent = device.GetSwapchainExtent();
    if (device.ConsumeSwapchainResized())
    {
        device.ResizeOutputTextures(m_forwardPass, extent.width, extent.height);
        m_tonemap.SetInput(m_hdrColor);
    }

    const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(std::max(extent.height, 1u));
    FrameConstants constants;
    constants.view = camera.View();
    constants.projection = camera.Projection(aspectRatio);
    constants.viewProjection = constants.projection * constants.view;
    constants.skyboxInverseViewProjection = glm::inverse(constants.projection * glm::mat4(glm::mat3(constants.view)));
    constants.cameraPosition = glm::vec4(camera.position, 1.0f);
    constants.lightPositionRadius = glm::vec4(settings.light.position, std::max(settings.light.radius, 1e-3f));
    constants.lightColorIntensity = glm::vec4(settings.light.color, settings.light.intensity);
    constants.iblParams = glm::vec4(settings.iblIntensity, static_cast<float>(m_environment.specularMipCount), 0.0f, 0.0f);

    const rhi::DynamicAllocation allocation = device.DynamicAllocate(sizeof(FrameConstants));
    std::memcpy(allocation.data, &constants, sizeof(constants));

    rhi::CommandBuffer& commandBuffer = device.GetCommandBuffer();
    commandBuffer.PushTimestamp("Forward");
    commandBuffer.SetClearColor(0, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    commandBuffer.SetClearDepth(1.0f);
    commandBuffer.BindPass(m_forwardPass);
    m_forwardPbr.Record(commandBuffer, m_scene, m_frameSet, allocation.offset);
    commandBuffer.PushTimestamp("Skybox");
    m_skybox.Record(commandBuffer, m_frameSet, allocation.offset);
    commandBuffer.PopTimestamp();
    commandBuffer.EndPass();
    commandBuffer.PopTimestamp();

    commandBuffer.PushTimestamp("Tonemap");
    commandBuffer.BindPass(device.GetSwapchainPass());
    m_tonemap.Record(commandBuffer, settings.exposure, !device.IsSwapchainSrgb());
    commandBuffer.PopTimestamp();
    if (imGui != nullptr)
    {
        commandBuffer.PushTimestamp("ImGui");
        imGui->Render(commandBuffer);
        commandBuffer.PopTimestamp();
    }
    commandBuffer.EndPass();
}

} // namespace mengine::render
