#pragma once

#include <glm/vec3.hpp>

#include <optional>

namespace mengine {

class Input;
class Window;

namespace render {
class Camera;
}

// Bounds are the world space AABB of every draw.
struct CameraScene
{
    bool hasBounds = false;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};

    // Pose from config.json for this scene, if any. Only the fly controller's home view uses it.
    struct Pose
    {
        glm::vec3 position{0.0f};
        float yawDegrees = 0.0f;
        float pitchDegrees = 0.0f;
    };
    std::optional<Pose> configuredPose;

    // Unit box around the origin without bounds, like the rest of the viewer.
    glm::vec3 Center() const;
    // Half the diagonal (bounding sphere radius), at least 1e-3.
    float Radius() const;
    // Largest side of the AABB, at least 1e-3.
    float MaxExtent() const;
};

struct CameraInput
{
    const Input& input;
    Window& window;
    bool uiWantsKeyboard = false;
    bool uiWantsMouse = false;
};

// Turns input into camera motion. Every mode writes the same render::Camera (position, yaw, pitch, clip planes), so
// the renderer does not know which one is active.
// Lifecycle: SetScene on every controller when a scene loads, ResetView on the active one; Deactivate / Activate on a
// mode change (the new controller adopts the current pose); Update once per frame.
class CameraController
{
public:
    // Far plane every mode keeps at least, so a small scene still shows the skybox far away.
    static constexpr float kMinFarPlane = 100.0f;

    CameraController() = default;
    virtual ~CameraController() = default;
    CameraController(const CameraController&) = delete;
    CameraController& operator=(const CameraController&) = delete;

    virtual const char* Name() const = 0;
    // One line describing the controls.
    virtual const char* Help() const = 0;

    // Remembers the scene; does not touch the camera.
    virtual void SetScene(const CameraScene& scene) { m_scene = scene; }
    // Moves the camera to this mode's home view of the scene.
    virtual void ResetView(render::Camera& camera) = 0;

    // The camera keeps its pose; the controller adopts it.
    virtual void Activate(render::Camera& camera) = 0;
    virtual void Deactivate(Window& window) { static_cast<void>(window); }

    virtual void Update(render::Camera& camera, const CameraInput& input, float deltaSeconds) = 0;

    // Inside the viewer's ImGui panel.
    virtual void DrawSettings() {}
    // Between ImGui::NewFrame and EndFrame, outside any window: on-screen widgets (background draw list).
    virtual void DrawOverlay() const {}

protected:
    // Far plane that keeps the whole scene visible from the camera: twice the distance to the farthest bounding sphere
    // point, at least kMinFarPlane.
    float FarPlaneFor(const render::Camera& camera) const;

    CameraScene m_scene;
};

} // namespace mengine
