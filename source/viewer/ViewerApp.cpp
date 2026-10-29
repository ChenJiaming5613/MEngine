#include "ViewerApp.hpp"

#include "CameraController.hpp"
#include "DeviceCapabilitiesPanel.hpp"
#include "FlyCameraController.hpp"
#include "FrameGraphViewer.hpp"
#include "OrbitCameraController.hpp"
#include "ViewerConfig.hpp"
#include "core/FileSystem.hpp"
#include "core/Log.hpp"
#include "render/Camera.hpp"
#include "render/ImGuiLayer.hpp"
#include "render/RenderTypes.hpp"
#include "render/Renderer.hpp"
#include "rhi/GpuDevice.hpp"

#include <SDL3/SDL.h>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <imgui.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mengine {

namespace {

constexpr const char* kConfigPath = "config.json";
constexpr double kMaxCameraStepSeconds = 0.1;
constexpr double kStatsRefreshSeconds = 0.5;

enum CameraMode : size_t
{
    kCameraModeFly = 0,
    kCameraModeOrbit = 1,
    kCameraModeCount = 2,
};

// The pass drawing the panel itself cannot be switched off from it.
constexpr const char* kImGuiPassName = "ImGui";

const char* FormatName(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_UNDEFINED: return "-";
    case VK_FORMAT_R16G16B16A16_SFLOAT: return "RGBA16F";
    case VK_FORMAT_R8G8B8A8_UNORM: return "RGBA8";
    case VK_FORMAT_R8G8B8A8_SRGB: return "RGBA8 sRGB";
    case VK_FORMAT_B8G8R8A8_UNORM: return "BGRA8";
    case VK_FORMAT_B8G8R8A8_SRGB: return "BGRA8 sRGB";
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "A2BGR10";
    case VK_FORMAT_D16_UNORM: return "D16";
    case VK_FORMAT_D32_SFLOAT: return "D32F";
    case VK_FORMAT_D24_UNORM_S8_UINT: return "D24S8";
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return "D32FS8";
    case VK_FORMAT_X8_D24_UNORM_PACK32: return "X8D24";
    default: return "other";
    }
}

class ViewerApp final : public Application
{
public:
    ViewerApp() = default;

protected:
    bool OnInitialize() override
    {
        rhi::GpuDeviceCreation deviceCreation{.window = GetWindow().NativeHandle(), .vsync = m_vsyncEnabled};
        if (const std::optional<std::string> userDirectory = GetUserDataDirectory())
        {
            deviceCreation.pipelineCachePath = *userDirectory + "pipeline_cache.bin";
        }
        m_device.Init(deviceCreation);
        m_imGui.Init(m_device, GetWindow());
        m_frameGraphViewer.Init();
        m_renderer.Init(m_device);
        GetInput().SetUiHitTest([this](float x, float y) { return HitsUi(x, y); });
        LoadConfiguredScene();
        return true;
    }

    void OnShutdown() override
    {
        if (m_device.GetDevice() != VK_NULL_HANDLE)
        {
            m_device.WaitIdle();
        }
        GetInput().SetUiHitTest({});
        ActiveCameraController().Deactivate(GetWindow());
        m_renderer.Shutdown();
        m_frameGraphViewer.Shutdown();
        m_imGui.Shutdown();
        m_device.Shutdown();
    }

    void OnFrame(double deltaSeconds) override
    {
        // Scene switches requested by the panel run here, outside BeginFrame / EndFrame.
        if (m_pendingScene)
        {
            const size_t index = *m_pendingScene;
            m_pendingScene.reset();
            LoadSceneAt(index);
        }

        const float cameraStep = static_cast<float>(std::min(deltaSeconds, kMaxCameraStepSeconds));
        const CameraInput cameraInput{
            .input = GetInput(),
            .window = GetWindow(),
            .uiWantsKeyboard = m_imGui.WantsKeyboard(),
            .uiWantsMouse = m_imGui.WantsMouse(),
        };
        ActiveCameraController().Update(m_camera, cameraInput, cameraStep);

        if (!m_device.BeginFrame())
        {
            return;
        }
        UpdateFrameStats(deltaSeconds);
        BuildPanel();
        m_renderer.Render(m_camera, m_settings, &m_imGui);
        m_device.EndFrame();
    }

    void OnSuspendChanged(bool suspended) override
    {
        // Android may kill a backgrounded app without a shutdown, so the cache is saved on the way out.
        if (suspended)
        {
            m_device.SavePipelineCache();
        }
        m_device.SetSuspended(suspended);
    }

    SDL_AppResult OnEvent(const SDL_Event& event) override
    {
        // The graph viewer keeps the touch emulated mouse from ImGui during a pinch on its canvas.
        if (!m_frameGraphViewer.HandleEvent(event, GetInput()))
        {
            m_imGui.ProcessEvent(event);
        }

        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
            if (m_imGui.WantsKeyboard())
            {
                break;
            }
            if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_AC_BACK)
            {
                return SDL_APP_SUCCESS;
            }
            break;

        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            m_device.MarkSwapchainDirty();
            break;

        default:
            break;
        }
        return SDL_APP_CONTINUE;
    }

private:
    void LoadConfiguredScene()
    {
        m_config = LoadViewerConfig(kConfigPath);
        if (m_config && m_config->environmentPath)
        {
            m_renderer.LoadEnvironment(*m_config->environmentPath);
        }
        else
        {
            MENGINE_LOG_INFO("No environment configured; using a uniform environment");
        }
        if (!m_config)
        {
            MENGINE_LOG_WARN("No usable %s, starting with an empty scene", kConfigPath);
        }
        LoadSceneAt(0);
    }

    // Also resets the camera and both lights; without a config only the reset runs.
    void LoadSceneAt(size_t index)
    {
        const SceneConfig* sceneConfig = m_config && index < m_config->scenes.size() ? &m_config->scenes[index] : nullptr;
        if (sceneConfig != nullptr)
        {
            m_currentScene = index;
            MENGINE_LOG_INFO("Loading scene %s (%s)", sceneConfig->name.c_str(), sceneConfig->path.c_str());
            m_sceneLoadFailed = !m_renderer.LoadScene(sceneConfig->path);
            if (m_sceneLoadFailed)
            {
                MENGINE_LOG_ERROR("Failed to load scene %s; showing an empty scene", sceneConfig->name.c_str());
            }
            else
            {
                // The scene's pipelines were just created (warm-up); persist them.
                m_device.SavePipelineCache();
            }
        }

        const render::Scene& scene = m_renderer.GetScene();
        CameraScene cameraScene;
        cameraScene.hasBounds = scene.HasBounds();
        if (cameraScene.hasBounds)
        {
            cameraScene.boundsMin = scene.boundsMin;
            cameraScene.boundsMax = scene.boundsMax;
        }

        m_camera = render::Camera{};
        if (sceneConfig != nullptr && sceneConfig->camera)
        {
            cameraScene.configuredPose = CameraScene::Pose{
                .position = sceneConfig->camera->position,
                .yawDegrees = sceneConfig->camera->yawDegrees,
                .pitchDegrees = sceneConfig->camera->pitchDegrees,
            };
            m_camera.fovYDegrees = sceneConfig->camera->fovYDegrees;
        }
        for (const std::unique_ptr<CameraController>& controller : m_cameraControllers)
        {
            controller->SetScene(cameraScene);
        }
        ActiveCameraController().ResetView(m_camera);

        // The light starts just above the camera with E = 1 at the scene center, like Vulkan-glTF-PBR's unit color
        // directional light under the same exposure.
        const glm::vec3 center = cameraScene.Center();
        const float radius = cameraScene.Radius();
        m_settings.light = render::PointLight{};
        m_settings.light.position = m_camera.position + glm::vec3(0.0f, 1.0f, 0.0f);
        const float lightDistance = glm::length(m_settings.light.position - center);
        m_settings.light.radius = std::max(2.0f * radius, 2.0f * lightDistance);
        m_settings.light.intensity = lightDistance * lightDistance + 1.0f;

        render::DirectionalLight& directional = m_settings.directional;
        directional = render::DirectionalLight{};
        if (m_config && m_config->directionalLight)
        {
            const DirectionalLightConfig& lightConfig = *m_config->directionalLight;
            directional.direction = lightConfig.direction.value_or(directional.direction);
            directional.color = lightConfig.color.value_or(directional.color);
            directional.intensity = lightConfig.intensity.value_or(directional.intensity);
        }
        directional.direction = glm::normalize(directional.direction);
    }

    bool HitsUi(float x, float y) const
    {
        if (m_anyPopupOpen)
        {
            return true;
        }
        if (m_frameGraphViewer.Contains(x, y) || m_capabilitiesPanel.Contains(x, y))
        {
            return true;
        }
        return x >= m_panelMin.x && y >= m_panelMin.y && x <= m_panelMax.x && y <= m_panelMax.y;
    }

    void UpdateFrameStats(double deltaSeconds)
    {
        m_statsAccumulatedSeconds += deltaSeconds;
        ++m_statsAccumulatedFrames;
        if (m_statsAccumulatedSeconds >= kStatsRefreshSeconds)
        {
            m_displayedFramesPerSecond = static_cast<float>(m_statsAccumulatedFrames / m_statsAccumulatedSeconds);
            m_displayedFrameMilliseconds = static_cast<float>(m_statsAccumulatedSeconds * 1000.0 / m_statsAccumulatedFrames);
            m_statsAccumulatedSeconds = 0.0;
            m_statsAccumulatedFrames = 0;
        }
    }

    void BuildPanel()
    {
        m_imGui.NewFrame();

        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("MEngine", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        const render::Scene& scene = m_renderer.GetScene();
        const render::Environment& environment = m_renderer.GetEnvironment();
        if (m_config && !m_config->scenes.empty())
        {
            const std::vector<SceneConfig>& scenes = m_config->scenes;
            if (ImGui::BeginCombo("Scene", scenes[std::min(m_currentScene, scenes.size() - 1)].name.c_str()))
            {
                for (size_t i = 0; i < scenes.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    const bool selected = i == m_currentScene;
                    if (ImGui::Selectable(scenes[i].name.c_str(), selected) && !selected)
                    {
                        m_pendingScene = i;
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        }
        if (m_sceneLoadFailed)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "Scene: failed to load (see log)");
        }
        else
        {
            ImGui::Text("Scene: %s", scene.path.empty() ? "<none>" : scene.path.c_str());
        }
        ImGui::Text("Draws: %zu (%u visible)  Materials: %zu  Textures: %zu", scene.draws.size(),
            m_renderer.GetCullingStats().visible, scene.materials.size(), scene.textures.size());
        ImGui::Text("Environment: %s", environment.isFallback ? "<uniform fallback>" : environment.directory.c_str());

        if (ImGui::CollapsingHeader("Device"))
        {
            const rhi::DeviceCapabilities& caps = m_device.GetCapabilities();
            ImGui::Text("%s", caps.properties.deviceName);
            ImGui::Text("Tier %s (%s)  Vulkan %s", rhi::DeviceTierName(caps.tier), rhi::DeviceTierDescription(caps.tier),
                rhi::ApiVersionString(caps.properties.apiVersion).c_str());
            bool showCapabilities = m_capabilitiesPanel.IsOpen();
            if (ImGui::Checkbox("Show Device Capabilities", &showCapabilities))
            {
                m_capabilitiesPanel.SetOpen(showCapabilities);
            }
        }

        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Checkbox("VSync", &m_vsyncEnabled))
            {
                m_device.SetVsync(m_vsyncEnabled);
            }
            ImGui::Text("Present Mode: %s", rhi::PresentModeName(m_device.GetPresentMode()));
            ImGui::Text("FPS: %.1f (%.2f ms)", m_displayedFramesPerSecond, m_displayedFrameMilliseconds);
            const rhi::PipelineStats& pipelineStats = m_device.GetPipelineStats();
            ImGui::Text("Pipelines: %u created, %.1f ms (cache loaded %zu KB)", pipelineStats.created,
                pipelineStats.totalMilliseconds, pipelineStats.loadedCacheBytes / 1024);
            for (const rhi::GpuTiming& timing : m_device.GetProfiler().Timings())
            {
                ImGui::Text("%*sGPU %s: %.3f ms", static_cast<int>(timing.depth * 2), "", timing.name.c_str(), timing.milliseconds);
            }
        }

        if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("Exposure", &m_settings.exposure, 0.1f, 32.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("IBL Intensity", &m_settings.iblIntensity, 0.0f, 4.0f, "%.2f");
            ImGui::SliderFloat("Skybox Blur", &m_settings.skyboxBlur, 0.0f, 1.0f, "%.3f");

            render::DirectionalLight& directional = m_settings.directional;
            ImGui::PushID("Directional");
            ImGui::SeparatorText("Directional Light");
            ImGui::ColorEdit3("Color", &directional.color.x);
            ImGui::DragFloat("Intensity", &directional.intensity, 0.02f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            glm::vec3 direction = directional.direction;
            if (ImGui::DragFloat3("Direction", &direction.x, 0.005f, -1.0f, 1.0f, "%.3f"))
            {
                const float length = glm::length(direction);
                if (length > 1e-4f)
                {
                    directional.direction = direction / length;
                }
            }
            if (ImGui::Button("Use Camera Direction"))
            {
                directional.direction = m_camera.Forward();
            }
            ImGui::Checkbox("Cast Shadows", &directional.castShadows);
            ImGui::SliderFloat("Normal Offset", &directional.normalOffsetTexels, 0.0f, 5.0f, "%.2f texels");
            ImGui::PopID();

            ImGui::PushID("Point");
            ImGui::SeparatorText("Point Light");
            ImGui::DragFloat3("Position", &m_settings.light.position.x, 0.05f);
            ImGui::ColorEdit3("Color", &m_settings.light.color.x);
            ImGui::DragFloat("Intensity", &m_settings.light.intensity, 0.5f, 0.0f, 100000.0f, "%.1f",
                ImGuiSliderFlags_Logarithmic);
            ImGui::DragFloat("Radius", &m_settings.light.radius, 0.1f, 0.01f, 10000.0f, "%.2f");
            if (ImGui::Button("Move Light To Camera"))
            {
                m_settings.light.position = m_camera.position;
            }
            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            CameraController& active = ActiveCameraController();
            if (ImGui::BeginCombo("Control", active.Name()))
            {
                for (size_t mode = 0; mode < m_cameraControllers.size(); ++mode)
                {
                    const bool selected = mode == m_cameraMode;
                    if (ImGui::Selectable(m_cameraControllers[mode]->Name(), selected))
                    {
                        SetCameraMode(mode);
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset"))
            {
                ActiveCameraController().ResetView(m_camera);
            }
            ImGui::Text("Position: %.2f %.2f %.2f", m_camera.position.x, m_camera.position.y, m_camera.position.z);
            ImGui::Text("Yaw %.1f  Pitch %.1f", m_camera.yawDegrees, m_camera.pitchDegrees);
            ActiveCameraController().DrawSettings();
            ImGui::TextDisabled("%s", ActiveCameraController().Help());
        }

        if (ImGui::CollapsingHeader("Debug"))
        {
            const render::CullingStats& culling = m_renderer.GetCullingStats();
            ImGui::Checkbox("Frustum Culling", &m_settings.frustumCulling);
            ImGui::SameLine();
            ImGui::Checkbox("Freeze Culling", &m_settings.freezeCulling);
            ImGui::Text("Visible Draws: %u / %u%s", culling.visible, culling.draws, culling.frozen ? " (frozen)" : "");
            ImGui::Checkbox("Scene Bounds", &m_settings.debugSceneBounds);
            ImGui::Checkbox("Shadow Frustum", &m_settings.debugShadowFrustum);
            ImGui::Checkbox("Draw Bounds", &m_settings.debugDrawBounds);
        }

        if (ImGui::CollapsingHeader("Frame Graph"))
        {
            bool showViewer = m_frameGraphViewer.IsOpen();
            if (ImGui::Checkbox("Show Graph Viewer", &showViewer))
            {
                m_frameGraphViewer.SetOpen(showViewer);
            }
            BuildFrameGraphPanel();
        }

        const ImVec2 position = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        m_panelMin = glm::vec2(position.x, position.y);
        m_panelMax = glm::vec2(position.x + size.x, position.y + size.y);
        ImGui::End();

        m_frameGraphViewer.Draw(m_renderer.GetFrameGraph(), m_settings, kImGuiPassName, GetInput());
        m_capabilitiesPanel.Draw(m_device);
        // After every window, so popups of the graph viewer count too.
        m_anyPopupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

        ActiveCameraController().DrawOverlay();
        m_imGui.EndFrame();
    }

    // Toggles take effect at the next Render, which recompiles the graph.
    void BuildFrameGraphPanel()
    {
        render::FrameGraph& graph = m_renderer.GetFrameGraph();
        const render::FrameGraphDebugInfo& info = graph.GetDebugInfo();
        ImGui::Text("Compiles: %u  Render passes: %zu  Textures: %u", info.compileCount, info.renderPasses.size(),
            info.physicalTextures);
        bool aliasing = graph.IsAliasingEnabled();
        if (ImGui::Checkbox("Texture Aliasing", &aliasing))
        {
            graph.SetAliasingEnabled(aliasing);
        }

        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
        ImGui::SeparatorText("Passes");
        if (ImGui::BeginTable("Passes", 4, kTableFlags))
        {
            ImGui::TableSetupColumn("Pass");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Render Pass");
            ImGui::TableSetupColumn("Status");
            ImGui::TableHeadersRow();
            for (const render::FrameGraphDebugInfo::Node& node : info.nodes)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(node.name.c_str());
                bool enabled = graph.IsEnabled(node.name);
                const bool locked = node.name == kImGuiPassName;
                ImGui::BeginDisabled(locked);
                if (ImGui::Checkbox(node.name.c_str(), &enabled))
                {
                    graph.SetEnabled(node.name, enabled);
                }
                ImGui::EndDisabled();
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(node.type == render::FrameGraphNodeType::Compute ? "compute" : "graphics");
                ImGui::TableNextColumn();
                if (node.renderPass >= 0 && static_cast<size_t>(node.renderPass) < info.renderPasses.size())
                {
                    const render::FrameGraphDebugInfo::RenderPass& renderPass = info.renderPasses[node.renderPass];
                    // Group names join the merged passes with '+'.
                    const bool merged = renderPass.name.find('+') != std::string::npos;
                    ImGui::Text("#%d%s", node.renderPass, merged ? " (merged)" : "");
                }
                else
                {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableNextColumn();
                if (node.scheduled)
                {
                    ImGui::TextUnformatted(node.status.c_str());
                }
                else
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", node.status.c_str());
                }
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Render Passes");
        for (size_t index = 0; index < info.renderPasses.size(); ++index)
        {
            const render::FrameGraphDebugInfo::RenderPass& renderPass = info.renderPasses[index];
            ImGui::BulletText("#%zu %s: %s", index, renderPass.name.c_str(), renderPass.operations.c_str());
        }

        ImGui::SeparatorText("Resources");
        if (ImGui::BeginTable("Resources", 5, kTableFlags))
        {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Kind");
            ImGui::TableSetupColumn("Format");
            ImGui::TableSetupColumn("Lifetime");
            ImGui::TableSetupColumn("Texture");
            ImGui::TableHeadersRow();
            for (const render::FrameGraphDebugInfo::Resource& resource : info.resources)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(resource.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(resource.kind);
                ImGui::TableNextColumn();
                if (resource.width > 0)
                {
                    ImGui::Text("%s %ux%u", FormatName(resource.format), resource.width, resource.height);
                }
                else
                {
                    ImGui::TextUnformatted(FormatName(resource.format));
                }
                ImGui::TableNextColumn();
                if (resource.firstUse >= 0)
                {
                    ImGui::Text("%d..%d", resource.firstUse, resource.lastUse);
                }
                else
                {
                    ImGui::TextDisabled("unused");
                }
                ImGui::TableNextColumn();
                if (resource.physical >= 0)
                {
                    ImGui::Text("#%d", resource.physical);
                }
                else
                {
                    ImGui::TextDisabled("-");
                }
            }
            ImGui::EndTable();
        }
    }

    CameraController& ActiveCameraController() { return *m_cameraControllers[m_cameraMode]; }

    void SetCameraMode(size_t mode)
    {
        if (mode == m_cameraMode || mode >= m_cameraControllers.size())
        {
            return;
        }
        ActiveCameraController().Deactivate(GetWindow());
        m_cameraMode = mode;
        ActiveCameraController().Activate(m_camera);
        MENGINE_LOG_INFO("Camera control: %s", ActiveCameraController().Name());
    }

    rhi::GpuDevice m_device;
    render::ImGuiLayer m_imGui;
    FrameGraphViewer m_frameGraphViewer;
    DeviceCapabilitiesPanel m_capabilitiesPanel;
    render::Renderer m_renderer;
    render::Camera m_camera;
    render::RenderSettings m_settings;
    std::array<std::unique_ptr<CameraController>, kCameraModeCount> m_cameraControllers{
        std::make_unique<FlyCameraController>(),
        std::make_unique<OrbitCameraController>(),
    };
    size_t m_cameraMode = kCameraModeFly;

    std::optional<ViewerConfig> m_config;
    size_t m_currentScene = 0;
    bool m_sceneLoadFailed = false;
    std::optional<size_t> m_pendingScene;

    glm::vec2 m_panelMin{0.0f};
    glm::vec2 m_panelMax{-1.0f};
    bool m_anyPopupOpen = false;

    bool m_vsyncEnabled = true;
    double m_statsAccumulatedSeconds = 0.0;
    uint32_t m_statsAccumulatedFrames = 0;
    float m_displayedFramesPerSecond = 0.0f;
    float m_displayedFrameMilliseconds = 0.0f;
};

} // namespace

std::unique_ptr<Application> CreateViewerApp()
{
    return std::make_unique<ViewerApp>();
}

} // namespace mengine
