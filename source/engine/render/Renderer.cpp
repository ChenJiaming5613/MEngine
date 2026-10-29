#include "render/Renderer.hpp"

#include "render/Camera.hpp"
#include "render/Culling.hpp"
#include "render/GltfLoader.hpp"
#include "render/RenderView.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/GpuDevice.hpp"

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace mengine::render {

namespace {

constexpr std::string_view kShadowPassName = "Shadow";

} // namespace

void Renderer::Init(rhi::GpuDevice& device)
{
    m_device = &device;

    rhi::DescriptorSetLayoutCreation frameLayout;
    frameLayout.name = "FrameLayout";
    frameLayout.setIndex = 0;
    frameLayout
        .AddBinding({.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .index = 0,
            .stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT})
        .AddBinding({.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .index = 1, .stages = VK_SHADER_STAGE_FRAGMENT_BIT,
            .textureType = rhi::TextureType::TextureCube})
        .AddBinding({.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .index = 2, .stages = VK_SHADER_STAGE_FRAGMENT_BIT,
            .textureType = rhi::TextureType::TextureCube})
        .AddBinding({.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .index = 3, .stages = VK_SHADER_STAGE_FRAGMENT_BIT})
        .AddBinding({.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .index = 4, .stages = VK_SHADER_STAGE_FRAGMENT_BIT});
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

    m_shadow.Init(device, m_frameLayout, m_materialLayout);
    m_forwardPbr.Init(device, m_frameLayout, m_materialLayout);
    m_skybox.Init(device, m_frameLayout);
    m_debugDraw.Init(device);
    m_debugDrawPass.Init(device, m_frameLayout, m_debugDraw);
    m_tonemap.Init(device);

    // Registration order is execution order.
    m_graph.Init(device);
    m_graph.AddPass(kShadowPassName, FrameGraphNodeType::Graphics, m_shadow);
    m_graph.AddPass("Forward", FrameGraphNodeType::Graphics, m_forwardPbr);
    m_graph.AddPass("Skybox", FrameGraphNodeType::Graphics, m_skybox);
    m_graph.AddPass("DebugDraw", FrameGraphNodeType::Graphics, m_debugDrawPass);
    m_graph.AddPass("Tonemap", FrameGraphNodeType::Graphics, m_tonemap);
    m_graph.AddPass("ImGui", FrameGraphNodeType::Graphics, m_imGuiPass);
    m_graph.Compile();

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
    m_graph.Shutdown();
    m_tonemap.Shutdown();
    m_debugDrawPass.Shutdown();
    m_debugDraw.Shutdown();
    m_skybox.Shutdown();
    m_forwardPbr.Shutdown();
    m_shadow.Shutdown();
    device.DestroyDescriptorSet(m_frameSet);
    device.DestroyDescriptorSetLayout(m_materialLayout);
    device.DestroyDescriptorSetLayout(m_frameLayout);
    *this = Renderer{};
}

bool Renderer::LoadScene(std::string_view assetPath)
{
    ++m_sceneVersion;
    // Free the previous scene before loading the next one, so both never occupy memory together.
    m_scene.Destroy(*m_device);
    m_device->WaitIdleAndReleaseDeferred();
    if (!LoadGltfScene(*m_device, assetPath, m_materialLayout, m_scene))
    {
        return false;
    }
    m_forwardPbr.WarmPipelines(m_scene);
    m_shadow.WarmPipelines(m_scene);
    return true;
}

void Renderer::LoadEnvironment(std::optional<std::string_view> directory)
{
    render::LoadEnvironment(*m_device, directory, m_environment);
    UpdateFrameSet();
}

Renderer::ShadowView Renderer::FitShadowView(const glm::vec3& direction) const
{
    const glm::vec3 center = 0.5f * (m_scene.boundsMin + m_scene.boundsMax);
    const glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::mat4 view = glm::lookAt(center - direction, center, up);

    glm::vec3 lightMin(std::numeric_limits<float>::max());
    glm::vec3 lightMax(std::numeric_limits<float>::lowest());
    for (uint32_t corner = 0; corner < 8; ++corner)
    {
        const glm::vec3 position((corner & 1) != 0 ? m_scene.boundsMax.x : m_scene.boundsMin.x,
            (corner & 2) != 0 ? m_scene.boundsMax.y : m_scene.boundsMin.y,
            (corner & 4) != 0 ? m_scene.boundsMax.z : m_scene.boundsMin.z);
        const glm::vec3 lightPosition(view * glm::vec4(position, 1.0f));
        lightMin = glm::min(lightMin, lightPosition);
        lightMax = glm::max(lightMax, lightPosition);
    }

    // A border of two texels keeps the 3x3 PCF taps of the outermost receivers inside the map.
    const float extent = std::max({lightMax.x - lightMin.x, lightMax.y - lightMin.y, 1e-3f});
    const float border = 2.0f * extent / static_cast<float>(ShadowPass::kSize);
    const float depthMargin = 0.01f * std::max(lightMax.z - lightMin.z, 1e-3f);
    // The view looks down -Z, so near / far are the negated maximum / minimum z.
    glm::mat4 projection = glm::orthoRH_ZO(lightMin.x - border, lightMax.x + border, lightMin.y - border, lightMax.y + border,
        -lightMax.z - depthMargin, -lightMin.z + depthMargin);
    projection[1][1] *= -1.0f;

    ShadowView shadowView;
    shadowView.viewProjection = projection * view;
    shadowView.texelWorldSize = (extent + 2.0f * border) / static_cast<float>(ShadowPass::kSize);
    return shadowView;
}

void Renderer::UpdateFrameSet()
{
    rhi::DescriptorSetCreation creation{.layout = m_frameLayout, .name = "FrameSet"};
    creation.AddBuffer(m_device->GetDynamicBuffer(), 0, sizeof(FrameConstants))
        .AddTexture(m_environment.irradiance, 1, m_environment.cubeSampler)
        .AddTexture(m_environment.specular, 2, m_environment.cubeSampler)
        .AddTexture(m_environment.brdfLut, 3, m_environment.lutSampler)
        .AddTexture(m_shadow.Texture(), 4, m_shadow.Sampler());
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
    device.ConsumeSwapchainResized();
    if (m_graph.NeedsCompile())
    {
        m_graph.Compile();
        // Incompatible outputs dropped the passes' pipelines; recreate the scene's variants before recording.
        m_forwardPbr.WarmPipelines(m_scene);
        m_shadow.WarmPipelines(m_scene);
    }
    const VkExtent2D extent = device.GetSwapchainExtent();

    const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(std::max(extent.height, 1u));
    FrameConstants constants;
    constants.view = camera.View();
    constants.projection = camera.Projection(aspectRatio);
    constants.viewProjection = constants.projection * constants.view;
    constants.skyboxInverseViewProjection = glm::inverse(constants.projection * glm::mat4(glm::mat3(constants.view)));
    constants.cameraPosition = glm::vec4(camera.position, 1.0f);
    constants.lightPositionRadius = glm::vec4(settings.light.position, std::max(settings.light.radius, 1e-3f));
    constants.lightColorIntensity = glm::vec4(settings.light.color, settings.light.intensity);
    constants.iblParams = glm::vec4(settings.iblIntensity, static_cast<float>(m_environment.specularMipCount),
        glm::clamp(settings.skyboxBlur, 0.0f, 1.0f), 0.0f);

    const DirectionalLight& directional = settings.directional;
    const float directionLength = glm::length(directional.direction);
    const glm::vec3 direction =
        directionLength > 1e-6f ? directional.direction / directionLength : glm::vec3(0.0f, -1.0f, 0.0f);
    const bool castShadows = directional.castShadows && !m_scene.IsEmpty() && m_scene.HasBounds();
    // A disabled Shadow node leaves the imported shadow map with whatever it last drew; sampling it would show stale
    // shadows that no longer follow the light or the scene.
    const bool shadowsAvailable = castShadows && m_graph.IsScheduled(kShadowPassName);
    const ShadowView shadowView = castShadows ? FitShadowView(direction) : ShadowView{};
    constants.lightViewProjection = shadowView.viewProjection;
    constants.directionalDirection = glm::vec4(direction, 0.0f);
    constants.directionalColorIntensity = glm::vec4(directional.color, std::max(directional.intensity, 0.0f));
    constants.shadowParams = glm::vec4(shadowsAvailable ? 1.0f : 0.0f, 1.0f / static_cast<float>(ShadowPass::kSize),
        shadowView.texelWorldSize * std::max(directional.normalOffsetTexels, 0.0f), 0.0f);

    const rhi::DynamicAllocation allocation = device.DynamicAllocate(sizeof(FrameConstants));
    std::memcpy(allocation.data, &constants, sizeof(constants));

    // Shadow casters are not culled: the light frustum encloses the whole scene, and culling them by the camera would
    // re-render the cached map whenever the camera moves.
    if (settings.freezeCulling)
    {
        if (!m_cullingFrozen)
        {
            m_frozenViewProjection = constants.viewProjection;
            m_cullingFrozen = true;
        }
    }
    else
    {
        m_cullingFrozen = false;
    }
    const glm::mat4& cullViewProjection = m_cullingFrozen ? m_frozenViewProjection : constants.viewProjection;
    if (settings.frustumCulling)
    {
        CullDraws(m_scene, Frustum::FromViewProjection(cullViewProjection), m_visibleDraws);
    }
    else
    {
        m_visibleDraws.resize(m_scene.draws.size());
        for (uint32_t index = 0; index < static_cast<uint32_t>(m_visibleDraws.size()); ++index)
        {
            m_visibleDraws[index] = index;
        }
    }
    m_cullingStats = {
        .draws = static_cast<uint32_t>(m_scene.draws.size()),
        .visible = static_cast<uint32_t>(m_visibleDraws.size()),
        .frozen = m_cullingFrozen,
    };

    if (settings.debugSceneBounds && m_scene.HasBounds())
    {
        m_debugDraw.Aabb(m_scene.boundsMin, m_scene.boundsMax, glm::vec4(1.0f, 0.85f, 0.1f, 1.0f));
    }
    if (settings.debugShadowFrustum && castShadows)
    {
        m_debugDraw.Frustum(shadowView.viewProjection, glm::vec4(1.0f, 0.4f, 0.1f, 1.0f));
    }
    if (settings.debugDrawBounds)
    {
        // m_visibleDraws is sorted, so one pass over the draws splits them into visible and culled.
        size_t next = 0;
        for (uint32_t index = 0; index < static_cast<uint32_t>(m_scene.draws.size()); ++index)
        {
            const SceneDraw& draw = m_scene.draws[index];
            const bool visible = next < m_visibleDraws.size() && m_visibleDraws[next] == index;
            next += visible ? 1 : 0;
            if (draw.boundsMin.x <= draw.boundsMax.x)
            {
                m_debugDraw.Aabb(draw.boundsMin, draw.boundsMax,
                    visible ? glm::vec4(0.2f, 1.0f, 0.3f, 1.0f) : glm::vec4(1.0f, 0.15f, 0.15f, 1.0f));
            }
        }
        if (m_cullingFrozen)
        {
            m_debugDraw.Frustum(m_frozenViewProjection, glm::vec4(0.3f, 0.7f, 1.0f, 1.0f));
        }
    }
    m_debugDraw.Upload();

    m_shadow.Prepare(castShadows, direction, m_sceneVersion);
    m_imGuiPass.SetLayer(imGui);
    const RenderView view{
        .scene = &m_scene,
        .visibleDraws = m_visibleDraws,
        .settings = &settings,
        .frameSet = m_frameSet,
        .frameOffset = allocation.offset,
        .encodeSrgb = !device.IsSwapchainSrgb(),
    };
    m_graph.Execute(device.GetCommandBuffer(), view);
    m_debugDraw.Clear();
}

} // namespace mengine::render
