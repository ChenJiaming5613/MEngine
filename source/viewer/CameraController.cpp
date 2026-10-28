#include "CameraController.hpp"

#include "platform/Input.hpp"
#include "platform/Window.hpp"
#include "render/Camera.hpp"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_platform_defines.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>

namespace mengine {
namespace {

constexpr float kStickRadiusFraction = 0.15f; // of the shorter window side
constexpr float kMinStickRadius = 40.0f;      // points
constexpr float kMaxStickRadius = 120.0f;
constexpr float kStickDeadZone = 0.08f;       // fraction of the radius

const Input::Touch* FindTouch(const Input& input, SDL_FingerID id)
{
    for (const Input::Touch& touch : input.Touches())
    {
        if (touch.id == id)
        {
            return &touch;
        }
    }
    return nullptr;
}

} // namespace

// Returns the stick deflection: x strafes right, y moves forward, length in [0, 1].
glm::vec2 CameraController::UpdateTouches(render::Camera& camera, const Input& input, Window& window)
{
#if defined(SDL_PLATFORM_ANDROID)
    m_usesTouch = true;
#endif
    int width = 0;
    int height = 0;
    window.GetSize(width, height);
    const float radius =
        std::clamp(kStickRadiusFraction * static_cast<float>(std::min(width, height)), kMinStickRadius, kMaxStickRadius);
    m_joystick.radius = radius;

    // Fingers that lifted give up their role.
    if (m_stickFinger && FindTouch(input, *m_stickFinger) == nullptr)
    {
        m_stickFinger.reset();
    }
    if (m_lookFinger && FindTouch(input, *m_lookFinger) == nullptr)
    {
        m_lookFinger.reset();
    }

    // New fingers take the free role of their screen half; fingers that started on ImGui are ignored entirely.
    for (const Input::Touch& touch : input.Touches())
    {
        m_usesTouch = true;
        if (touch.startedOverUi || touch.id == m_stickFinger || touch.id == m_lookFinger)
        {
            continue;
        }
        const bool leftHalf = touch.position.x < 0.5f * static_cast<float>(width);
        if (leftHalf && !m_stickFinger)
        {
            m_stickFinger = touch.id;
            m_joystick.center = touch.position - touch.delta; // where it landed this frame
        }
        else if (!leftHalf && !m_lookFinger)
        {
            m_lookFinger = touch.id;
        }
    }

    if (const Input::Touch* look = m_lookFinger ? FindTouch(input, *m_lookFinger) : nullptr)
    {
        camera.yawDegrees -= look->delta.x * touchDegreesPerPixel;
        camera.pitchDegrees -= look->delta.y * touchDegreesPerPixel;
    }

    const Input::Touch* stick = m_stickFinger ? FindTouch(input, *m_stickFinger) : nullptr;
    m_joystick.active = stick != nullptr;
    if (stick == nullptr)
    {
        m_joystick.center = glm::vec2(2.0f * radius, static_cast<float>(height) - 2.0f * radius);
        m_joystick.knob = m_joystick.center;
        return glm::vec2(0.0f);
    }

    // Dragging past the rim pulls the stick along, so reversing direction responds immediately.
    glm::vec2 offset = stick->position - m_joystick.center;
    const float distance = glm::length(offset);
    if (distance > radius)
    {
        m_joystick.center = stick->position - offset * (radius / distance);
        offset *= radius / distance;
    }
    m_joystick.knob = m_joystick.center + offset;

    glm::vec2 deflection = offset / radius;
    const float amount = glm::length(deflection);
    if (amount < kStickDeadZone)
    {
        return glm::vec2(0.0f);
    }
    // Rescale so speed starts at 0 at the dead zone edge and reaches 1 at the rim.
    deflection *= (amount - kStickDeadZone) / ((1.0f - kStickDeadZone) * amount);
    return glm::vec2(deflection.x, -deflection.y); // screen y grows downward
}

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

    const glm::vec2 stick = UpdateTouches(camera, input, window);
    camera.ClampPitch();

    // Keys move at full speed in any combination; the stick scales with its deflection.
    glm::vec3 velocity(0.0f);
    if (glm::dot(movement, movement) > 0.0f)
    {
        velocity = glm::normalize(movement);
    }
    velocity += camera.Forward() * stick.y + camera.Right() * stick.x;
    const float speed = glm::length(velocity);
    if (speed > 1.0f)
    {
        velocity /= speed;
    }
    camera.position += velocity * moveSpeed * deltaSeconds;
}

} // namespace mengine
