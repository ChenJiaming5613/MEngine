#pragma once

#include "render/DebugDraw.hpp"
#include "render/Environment.hpp"
#include "render/ImGuiLayer.hpp"
#include "render/RenderTypes.hpp"
#include "render/Scene.hpp"
#include "render/SceneLoader.hpp"
#include "render/graph/FrameGraph.hpp"
#include "render/passes/DebugDrawPass.hpp"
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
#include <vector>

namespace enki {
class TaskScheduler;
}

namespace mengine::rhi {
class GpuDevice;
}

namespace mengine::render {

class Camera;

// Owns the scene, the environment and the passes, and runs the frame graph every frame:
// Shadow (on demand) -> Forward -> Skybox -> DebugDraw into HDR -> Tonemap -> ImGui into the backbuffer.
class Renderer
{
public:
    // Throws on Vulkan or shader loading errors. Scene loading runs its CPU work on `tasks`.
    void Init(rhi::GpuDevice& device, enki::TaskScheduler& tasks);
    void Shutdown();

    // Frees the current scene (waiting for the GPU) and starts loading the next one; outside a frame. The scene stays
    // empty until UpdateSceneLoad returns Finished, or for good when it returns Failed.
    void RequestScene(std::string_view assetPath);
    // Once per frame, outside BeginFrame / EndFrame: uploads part of a scene in flight.
    SceneLoadStatus UpdateSceneLoad();
    SceneLoadProgress GetSceneLoadProgress() const { return m_sceneLoader.GetProgress(); }
    bool IsSceneLoading() const { return m_sceneLoader.IsLoading(); }
    // Replaces the environment; falls back to a uniform environment when loading fails.
    void LoadEnvironment(std::optional<std::string_view> directory);

    const Scene& GetScene() const { return m_scene; }
    const Environment& GetEnvironment() const { return m_environment; }
    // Pass toggles and the compile snapshot for debug UI; changes are compiled at the next Render.
    FrameGraph& GetFrameGraph() { return m_graph; }
    // Lines added before Render (after GpuDevice::BeginFrame or earlier) are drawn this frame, then cleared.
    DebugDraw& GetDebugDraw() { return m_debugDraw; }
    // Frustum culling result of the last Render.
    const CullingStats& GetCullingStats() const { return m_cullingStats; }

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

    FrameGraph m_graph;
    ShadowPass m_shadow;
    ForwardPbrPass m_forwardPbr;
    SkyboxPass m_skybox;
    DebugDraw m_debugDraw;
    DebugDrawPass m_debugDrawPass;
    TonemapPass m_tonemap;
    ImGuiPass m_imGuiPass;

    // Bounds how long one frame waits for scene uploads.
    static constexpr VkDeviceSize kSceneUploadBytesPerUpdate = 32ull << 20;

    Scene m_scene;
    SceneLoader m_sceneLoader;
    Environment m_environment;
    uint64_t m_sceneVersion = 0;

    // Storage behind RenderView::visibleDraws.
    std::vector<uint32_t> m_visibleDraws;
    glm::mat4 m_frozenViewProjection{1.0f};
    bool m_cullingFrozen = false;
    CullingStats m_cullingStats;
};

} // namespace mengine::render
