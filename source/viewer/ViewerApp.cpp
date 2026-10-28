#include "ViewerApp.hpp"

#include "CameraController.hpp"
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
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace mengine {

namespace {

constexpr const char* kConfigPath = "config.json";
constexpr double kMaxCameraStepSeconds = 0.1;
constexpr double kStatsRefreshSeconds = 0.5;
constexpr float kMinFarPlane = 100.0f;

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
        m_renderer.Shutdown();
        m_imGui.Shutdown();
        m_device.Shutdown();
    }

    void OnFrame(double deltaSeconds) override
    {
        const float cameraStep = static_cast<float>(std::min(deltaSeconds, kMaxCameraStepSeconds));
        m_cameraController.Update(m_camera, GetInput(), GetWindow(), cameraStep, m_imGui.WantsKeyboard());

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
        const std::optional<ViewerConfig> config = LoadViewerConfig(kConfigPath);
        if (config && !config->scenePath.empty())
        {
            m_renderer.LoadScene(config->scenePath);
        }
        else
        {
            MENGINE_LOG_WARN("No usable %s, starting with an empty scene", kConfigPath);
        }
        if (config && config->environmentPath)
        {
            m_renderer.LoadEnvironment(*config->environmentPath);
        }
        else
        {
            MENGINE_LOG_INFO("No environment configured; using a uniform environment");
        }

        const render::Scene& scene = m_renderer.GetScene();
        glm::vec3 center(0.0f);
        float radius = 1.0f;
        if (scene.HasBounds())
        {
            center = 0.5f * (scene.boundsMin + scene.boundsMax);
            radius = std::max(0.5f * glm::length(scene.boundsMax - scene.boundsMin), 1e-3f);
        }

        if (config && config->camera)
        {
            m_camera.position = config->camera->position;
            m_camera.yawDegrees = config->camera->yawDegrees;
            m_camera.pitchDegrees = config->camera->pitchDegrees;
            m_camera.fovYDegrees = config->camera->fovYDegrees;
        }
        else
        {
            // Frame the bounding sphere from +Z, looking down -Z.
            const float distance = radius / std::sin(glm::radians(m_camera.fovYDegrees) * 0.5f);
            m_camera.position = center + glm::vec3(0.0f, 0.0f, distance);
        }
        m_camera.ClampPitch();
        m_camera.farPlane = std::max(kMinFarPlane, 2.0f * (glm::length(m_camera.position - center) + radius));
        m_cameraController.moveSpeed = std::max(0.5f, radius * 0.25f);

        // The light starts just above the camera, bright enough to light the scene center (about E = 5 there).
        m_settings.light.position = m_camera.position + glm::vec3(0.0f, 1.0f, 0.0f);
        const float lightDistance = glm::length(m_settings.light.position - center);
        m_settings.light.radius = std::max(2.0f * radius, 2.0f * lightDistance);
        m_settings.light.intensity = 5.0f * (lightDistance * lightDistance + 1.0f);
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
            ImGui::SliderFloat("Exposure", &m_settings.exposure, 0.05f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("IBL Intensity", &m_settings.iblIntensity, 0.0f, 4.0f, "%.2f");
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
        }

        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Position: %.2f %.2f %.2f", m_camera.position.x, m_camera.position.y, m_camera.position.z);
            ImGui::Text("Yaw %.1f  Pitch %.1f", m_camera.yawDegrees, m_camera.pitchDegrees);
            ImGui::SliderFloat("Move Speed", &m_cameraController.moveSpeed, 0.1f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::TextDisabled("RMB + WASD/QE, or one finger look / two fingers forward");
        }

        const ImVec2 position = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        m_panelMin = glm::vec2(position.x, position.y);
        m_panelMax = glm::vec2(position.x + size.x, position.y + size.y);
        ImGui::End();
        m_anyPopupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

        m_imGui.EndFrame();
    }

    rhi::GpuDevice m_device;
    render::ImGuiLayer m_imGui;
    render::Renderer m_renderer;
    render::Camera m_camera;
    render::RenderSettings m_settings;
    CameraController m_cameraController;

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
