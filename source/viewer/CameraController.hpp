#pragma once

#include <SDL3/SDL_touch.h>

#include <glm/vec2.hpp>

#include <optional>

namespace mengine {

class Input;
class Window;

namespace render {
class Camera;
}

// Unity scene view style fly camera.
// Desktop: hold the right mouse button to look around (relative mouse mode) and move with WASD, Q/E down/up.
// Touch: a virtual joystick. A finger that lands on the left half of the screen is the stick: dragging up / down moves
// forward / backward, left / right strafes, and the speed grows with the drag distance up to moveSpeed at the rim.
// A finger on the right half looks around. Both work at the same time.
// Input that starts over ImGui, or keys while ImGui owns the keyboard, is ignored.
class CameraController
{
public:
    float moveSpeed = 3.0f;               // units per second
    float mouseDegreesPerPixel = 0.15f;
    float touchDegreesPerPixel = 0.25f;

    // Drawn by the viewer; in window points like Input.
    struct Joystick
    {
        bool active = false;        // a finger holds the stick; otherwise center is the idle hint position
        glm::vec2 center{0.0f};     // where the stick finger landed (follows it when dragged past the rim)
        glm::vec2 knob{0.0f};       // finger position, clamped to the rim
        float radius = 0.0f;        // drag distance for full speed
    };

    void Update(render::Camera& camera, const Input& input, Window& window, float deltaSeconds, bool uiWantsKeyboard);

    // True once the device has reported a touch (from the start on Android), so the hint is not shown on desktops.
    bool UsesTouch() const { return m_usesTouch; }
    const Joystick& GetJoystick() const { return m_joystick; }

private:
    glm::vec2 UpdateTouches(render::Camera& camera, const Input& input, Window& window);

    bool m_mouseLook = false;
    bool m_usesTouch = false;
    std::optional<SDL_FingerID> m_stickFinger;
    std::optional<SDL_FingerID> m_lookFinger;
    Joystick m_joystick;
};

} // namespace mengine
