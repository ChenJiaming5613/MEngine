#pragma once

#include "render/Environment.hpp"
#include "render/RenderTypes.hpp"
#include "render/Scene.hpp"
#include "render/passes/ForwardPbrPass.hpp"
#include "render/passes/ShadowPass.hpp"
#include "render/passes/SkyboxPass.hpp"
#include "render/passes/TonemapPass.hpp"
#include "rhi/GpuResources.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace mengine::rhi {
class GpuDevice;
}

namespace mengine::render {

class Camera;
class ImGuiLayer;

// Directional shadow map (only re-rendered when the light, the scene or the shadow toggle changes), then forward PBR
// into an RGBA16F target (opaque, then alpha mask, then skybox), then tone mapping and ImGui into the swapchain.
class Renderer
{
public:
    static constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    // Throws on Vulkan or shader loading errors.
    void Init(rhi::GpuDevice& device);
    void Shutdown();

    // Replaces the scene; returns false (with an empty scene) when loading fails. Call outside a frame: it waits for
    // the GPU and frees the previous scene first.
    bool LoadScene(std::string_view assetPath);
    // Replaces the environment; falls back to a uniform environment when loading fails.
    void LoadEnvironment(std::optional<std::string_view> directory);

    const Scene& GetScene() const { return m_scene; }
    const Environment& GetEnvironment() const { return m_environment; }

    // Records the frame between GpuDevice::BeginFrame and EndFrame. ImGui (if any) must have finished its frame.
    void Render(const Camera& camera, const RenderSettings& settings, ImGuiLayer* imGui);

private:
    struct ShadowView
    {
        glm::mat4 viewProjection{1.0f};
        float texelWorldSize = 0.0f;
    };
    // Orthographic light view enclosing the scene bounds, Y flipped like the camera projection.
    ShadowView FitShadowView(const glm::vec3& direction) const;
    void UpdateFrameSet();

    rhi::GpuDevice* m_device = nullptr;
    rhi::DescriptorSetLayoutHandle m_frameLayout;
    rhi::DescriptorSetLayoutHandle m_materialLayout;
    rhi::DescriptorSetHandle m_frameSet;

    rhi::TextureHandle m_hdrColor;
    rhi::TextureHandle m_depth;
    rhi::RenderPassHandle m_forwardPass;

    ShadowPass m_shadow;
    ForwardPbrPass m_forwardPbr;
    SkyboxPass m_skybox;
    TonemapPass m_tonemap;

    Scene m_scene;
    Environment m_environment;

    // State the shadow map was last rendered with; it is rendered at least once so that it is always sampleable.
    bool m_shadowRendered = false;
    bool m_shadowCastersDrawn = false;
    glm::vec3 m_shadowDirection{0.0f};
    uint64_t m_sceneVersion = 0;
    uint64_t m_shadowSceneVersion = ~0ull;
};

} // namespace mengine::render
