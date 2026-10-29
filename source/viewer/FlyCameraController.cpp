#include "FlyCameraController.hpp"

#include "platform/Input.hpp"
#include "platform/Window.hpp"
#include "render/Camera.hpp"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_platform_defines.h>

#include <glm/common.hpp>
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

void FlyCameraController::SetScene(const CameraScene& scene)
{
    CameraController::SetScene(scene);
    moveSpeed = std::max(0.5f, scene.Radius() * 0.25f);
}

void FlyCameraController::ResetView(render::Camera& camera)
{
    if (m_scene.configuredPose)
    {
        camera.position = m_scene.configuredPose->position;
        camera.yawDegrees = m_scene.configuredPose->yawDegrees;
        camera.pitchDegrees = m_scene.configuredPose->pitchDegrees;
    }
    else
    {
        // Frame the bounding sphere from +Z, looking down -Z.
        const float distance = m_scene.Radius() / std::sin(glm::radians(camera.fovYDegrees) * 0.5f);
        camera.position = m_scene.Center() + glm::vec3(0.0f, 0.0f, distance);
        camera.yawDegrees = 0.0f;
        camera.pitchDegrees = 0.0f;
    }
    camera.ClampPitch();
    camera.farPlane = FarPlaneFor(camera);
}

void FlyCameraController::Activate(render::Camera& camera)
{
    // An orbit may have left the camera upside down; keep the view direction, drop the roll.
    camera.ClampPitch();
    camera.farPlane = FarPlaneFor(camera);
}

void FlyCameraController::Deactivate(Window& window)
{
    SetMouseLook(window, false);
    m_stickFinger.reset();
    m_lookFinger.reset();
    m_joystick.active = false;
}

void FlyCameraController::SetMouseLook(Window& window, bool enabled)
{
    if (enabled != m_mouseLook)
    {
        window.SetRelativeMouseMode(enabled);
        m_mouseLook = enabled;
    }
}

// Returns the stick deflection: x strafes right, y moves forward, length in [0, 1].
glm::vec2 FlyCameraController::UpdateTouches(render::Camera& camera, const Input& input, Window& window)
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

void FlyCameraController::Update(render::Camera& camera, const CameraInput& context, float deltaSeconds)
{
    const Input& input = context.input;

    // Desktop: right mouse look. Input already drops presses that started over ImGui.
    SetMouseLook(context.window, input.IsMouseButtonHeld(SDL_BUTTON_RIGHT));
    if (m_mouseLook)
    {
        const glm::vec2 delta = input.MouseDelta();
        camera.yawDegrees -= delta.x * mouseDegreesPerPixel;
        camera.pitchDegrees -= delta.y * mouseDegreesPerPixel;
    }

    glm::vec3 movement(0.0f);
    if (!context.uiWantsKeyboard)
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

    const glm::vec2 stick = UpdateTouches(camera, input, context.window);
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

void FlyCameraController::DrawSettings()
{
    ImGui::SliderFloat("Move Speed", &moveSpeed, 0.1f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
}

void FlyCameraController::DrawOverlay() const
{
    if (!m_usesTouch)
    {
        return;
    }
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const ImVec2 center(m_joystick.center.x, m_joystick.center.y);
    const float thickness = std::max(2.0f, m_joystick.radius * 0.04f);
    if (!m_joystick.active)
    {
        drawList->AddCircle(center, m_joystick.radius, IM_COL32(255, 255, 255, 60), 48, thickness);
        drawList->AddCircleFilled(center, m_joystick.radius * 0.35f, IM_COL32(255, 255, 255, 40), 32);
        return;
    }
    drawList->AddCircleFilled(center, m_joystick.radius, IM_COL32(0, 0, 0, 50), 48);
    drawList->AddCircle(center, m_joystick.radius, IM_COL32(255, 255, 255, 150), 48, thickness);
    drawList->AddCircleFilled(ImVec2(m_joystick.knob.x, m_joystick.knob.y), m_joystick.radius * 0.35f,
        IM_COL32(255, 255, 255, 140), 32);
}

} // namespace mengine
