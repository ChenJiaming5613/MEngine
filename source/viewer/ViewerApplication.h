#pragma once

#include "Application.h"
#include "DeviceCapabilitiesPanel.h"
#include "FrameGraphViewer.h"
#include "ProfilerPanel.h"
#include "camera/CameraController.h"
#include "camera/FlyCameraController.h"
#include "camera/OrbitCameraController.h"
#include "ViewerConfig.h"
#include "camera/Camera.h"
#include "render/ImGuiLayer.h"
#include "render/RenderTypes.h"
#include "render/Renderer.h"
#include "rhi/GfxDevice.h"

#include <SDL3/SDL.h>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <array>
#include <memory>
#include <optional>

namespace mengine {

enum CameraMode : size_t
{
    kCameraModeFly = 0,
    kCameraModeOrbit = 1,
    kCameraModeCount = 2,
};

class ViewerApplication final : public Application
{
public:
    ViewerApplication() = default;

protected:
    bool OnInitialize() override;
    void OnShutdown() override;
    void OnTick(double deltaSeconds) override;
    void OnSuspendChanged(bool suspended) override;
    SDL_AppResult OnEvent(const SDL_Event& event) override;

private:
    void LoadConfiguredScene();
    void LoadSceneAt(size_t index);
    void ResetCameraAndLights();
    bool HitsUi(float x, float y) const;
    void UpdateFrameStats(double deltaSeconds);
    void BuildPanel();
    const char* CurrentSceneName() const;
    void DrawSceneLoadOverlay();
    void BuildFrameGraphPanel();
    camera::CameraController& ActiveCameraController();
    void SetCameraMode(size_t mode);

    rhi::GfxDevice m_device;
    render::ImGuiLayer m_imGui;
    FrameGraphViewer m_frameGraphViewer;
    DeviceCapabilitiesPanel m_capabilitiesPanel;
    ProfilerPanel m_profilerPanel;
    render::Renderer m_renderer;
    camera::Camera m_camera;
    render::RenderSettings m_settings;
    std::array<std::unique_ptr<camera::CameraController>, kCameraModeCount> m_cameraControllers{
        std::make_unique<camera::FlyCameraController>(),
        std::make_unique<camera::OrbitCameraController>(),
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

} // namespace mengine
