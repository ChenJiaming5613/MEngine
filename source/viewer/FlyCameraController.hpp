#pragma once

#include "CameraController.hpp"

#include <SDL3/SDL_touch.h>

#include <glm/vec2.hpp>

#include <optional>

namespace mengine {

// Unity scene view style fly camera.
// Desktop: hold the right mouse button to look around (relative mouse mode), WASD to move, Q/E down/up.
// Touch: a finger landing on the left half is a virtual joystick (up / down moves, left / right strafes, speed grows
// with the drag distance up to moveSpeed at the rim); a finger on the right half looks around. Both work at once.
// Input that starts over ImGui, or keys while ImGui owns the keyboard, is ignored.
class FlyCameraController final : public CameraController
{
public:
    float moveSpeed = 3.0f; // units per second; SetScene scales it to the scene
    float mouseDegreesPerPixel = 0.15f;
    float touchDegreesPerPixel = 0.25f;

    const char* Name() const override { return "Fly"; }
    const char* Help() const override { return "RMB + WASD/QE; touch: left half joystick, right half look"; }

    void SetScene(const CameraScene& scene) override;
    // The configured pose, otherwise the bounding sphere framed from +Z looking down -Z.
    void ResetView(render::Camera& camera) override;
    void Activate(render::Camera& camera) override;
    void Deactivate(Window& window) override;
    void Update(render::Camera& camera, const CameraInput& input, float deltaSeconds) override;

    void DrawSettings() override;
    // Touch devices only: a faint ring where the stick usually sits, and the live stick while a finger holds it.
    void DrawOverlay() const override;

private:
    // In window points like Input.
    struct Joystick
    {
        bool active = false;    // a finger holds the stick; otherwise center is the idle hint position
        glm::vec2 center{0.0f}; // where the stick finger landed (follows it when dragged past the rim)
        glm::vec2 knob{0.0f};   // finger position, clamped to the rim
        float radius = 0.0f;    // drag distance for full speed
    };

    glm::vec2 UpdateTouches(render::Camera& camera, const Input& input, Window& window);
    void SetMouseLook(Window& window, bool enabled);

    bool m_mouseLook = false;
    // True once the device has reported a touch (from the start on Android), so the hint is not shown on desktops.
    bool m_usesTouch = false;
    std::optional<SDL_FingerID> m_stickFinger;
    std::optional<SDL_FingerID> m_lookFinger;
    Joystick m_joystick;
};

} // namespace mengine
