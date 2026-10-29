#include "ViewerApp.hpp"

#include "CameraController.hpp"
#include "FlyCameraController.hpp"
#include "OrbitCameraController.hpp"
#include "ViewerConfig.hpp"
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
    kCameraModeFly = 0, // default
    kCameraModeOrbit = 1,
    kCameraModeCount = 2,
};

class ViewerApp final : public Application
{
public:
    ViewerApp() = default;

protected:
    bool OnInitialize() override
    {
        m_device.Init(rhi::GpuDeviceCreation{.window = GetWindow().NativeHandle(), .vsync = m_vsyncEnabled});
        m_imGui.Init(m_device, GetWindow());
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

    void OnSuspendChanged(bool suspended) override { m_device.SetSuspended(suspended); }

    SDL_AppResult OnEvent(const SDL_Event& event) override
    {
        m_imGui.ProcessEvent(event);

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

    // Loads config scene `index` (none without a config) and resets the camera and both lights for it.
    void LoadSceneAt(size_t index)
    {
        const SceneConfig* sceneConfig = m_config && index < m_config->scenes.size() ? &m_config->scenes[index] : nullptr;
        if (sceneConfig != nullptr)
        {
            m_currentScene = index;
            MENGINE_LOG_INFO("Loading scene %s (%s)", sceneConfig->name.c_str(), sceneConfig->path.c_str());
            m_renderer.LoadScene(sceneConfig->path);
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
        ImGui::Text("Scene: %s", scene.path.empty() ? "<none>" : scene.path.c_str());
        ImGui::Text("Draws: %zu  Materials: %zu  Textures: %zu", scene.draws.size(), scene.materials.size(), scene.textures.size());
        ImGui::Text("Environment: %s", environment.isFallback ? "<uniform fallback>" : environment.directory.c_str());

        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Checkbox("VSync", &m_vsyncEnabled))
            {
                m_device.SetVsync(m_vsyncEnabled);
            }
            ImGui::Text("Present Mode: %s", rhi::PresentModeName(m_device.GetPresentMode()));
            ImGui::Text("FPS: %.1f (%.2f ms)", m_displayedFramesPerSecond, m_displayedFrameMilliseconds);
            for (const rhi::GpuTiming& timing : m_device.GetProfiler().Timings())
            {
                ImGui::Text("%*sGPU %s: %.3f ms", static_cast<int>(timing.depth * 2), "", timing.name.c_str(), timing.milliseconds);
            }
        }

        if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("Exposure", &m_settings.exposure, 0.1f, 32.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("IBL Intensity", &m_settings.iblIntensity, 0.0f, 4.0f, "%.2f");

            render::DirectionalLight& directional = m_settings.directional;
            ImGui::PushID("Directional");
            ImGui::SeparatorText("Directional Light");
            ImGui::ColorEdit3("Color", &directional.color.x);
            ImGui::DragFloat("Intensity", &directional.intensity, 0.02f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            glm::vec3 direction = directional.direction;
            if (ImGui::DragFloat3("Direction", &direction.x, 0.005f, -1.0f, 1.0f, "%.3f"))
            {
                // Kept normalized; a zero vector keeps the previous direction.
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

        const ImVec2 position = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        m_panelMin = glm::vec2(position.x, position.y);
        m_panelMax = glm::vec2(position.x + size.x, position.y + size.y);
        ImGui::End();
        m_anyPopupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

        ActiveCameraController().DrawOverlay();
        m_imGui.EndFrame();
    }

    CameraController& ActiveCameraController() { return *m_cameraControllers[m_cameraMode]; }

    // The new controller takes over the current view instead of jumping to its home view.
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
