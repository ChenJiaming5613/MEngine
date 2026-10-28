#pragma once

namespace mengine {

class Input;
class Window;

namespace render {
class Camera;
}

// Unity scene view style fly camera.
// Desktop: hold the right mouse button to look around (relative mouse mode) and move with WASD, Q/E down/up.
// Touch: one finger looks around, two fingers held together move forward at a constant speed.
// Input that starts over ImGui, or keys while ImGui owns the keyboard, is ignored.
class CameraController
{
public:
    float moveSpeed = 3.0f;               // units per second
    float mouseDegreesPerPixel = 0.15f;
    float touchDegreesPerPixel = 0.25f;

    void Update(render::Camera& camera, const Input& input, Window& window, float deltaSeconds, bool uiWantsKeyboard);

private:
    bool m_mouseLook = false;
};

} // namespace mengine
