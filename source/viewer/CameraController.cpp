#include "CameraController.hpp"

#include "platform/Input.hpp"
#include "platform/Window.hpp"
#include "render/Camera.hpp"

#include <SDL3/SDL_mouse.h>

#include <glm/geometric.hpp>

namespace mengine {

void CameraController::Update(render::Camera& camera, const Input& input, Window& window, float deltaSeconds,
    bool uiWantsKeyboard)
{
    // Desktop: right mouse look. Input already drops presses that started over ImGui.
    const bool mouseLook = input.IsMouseButtonHeld(SDL_BUTTON_RIGHT);
    if (mouseLook != m_mouseLook)
    {
        window.SetRelativeMouseMode(mouseLook);
        m_mouseLook = mouseLook;
    }
    if (m_mouseLook)
    {
        const glm::vec2 delta = input.MouseDelta();
        camera.yawDegrees -= delta.x * mouseDegreesPerPixel;
        camera.pitchDegrees -= delta.y * mouseDegreesPerPixel;
    }

    glm::vec3 movement(0.0f);
    if (!uiWantsKeyboard)
    {
        const glm::vec3 forward = camera.Forward();
        const glm::vec3 right = camera.Right();
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        if (input.IsKeyDown(SDL_SCANCODE_W))
        {
            movement += forward;
        }
        if (input.IsKeyDown(SDL_SCANCODE_S))
        {
            movement -= forward;
        }
        if (input.IsKeyDown(SDL_SCANCODE_D))
        {
            movement += right;
        }
        if (input.IsKeyDown(SDL_SCANCODE_A))
        {
            movement -= right;
        }
        if (input.IsKeyDown(SDL_SCANCODE_E))
        {
            movement += up;
        }
        if (input.IsKeyDown(SDL_SCANCODE_Q))
        {
            movement -= up;
        }
    }

    // Touch: fingers that started on ImGui are ignored entirely.
    const Input::Touch* lookTouch = nullptr;
    int sceneTouches = 0;
    for (const Input::Touch& touch : input.Touches())
    {
        if (!touch.startedOverUi)
        {
            lookTouch = &touch;
            ++sceneTouches;
        }
    }
    if (sceneTouches == 1)
    {
        camera.yawDegrees -= lookTouch->delta.x * touchDegreesPerPixel;
        camera.pitchDegrees -= lookTouch->delta.y * touchDegreesPerPixel;
    }
    else if (sceneTouches >= 2)
    {
        movement += camera.Forward();
    }

    camera.ClampPitch();
    if (glm::dot(movement, movement) > 0.0f)
    {
        camera.position += glm::normalize(movement) * moveSpeed * deltaSeconds;
    }
}

} // namespace mengine
