#include "OrbitCameraController.hpp"

#include "platform/Input.hpp"
#include "platform/Window.hpp"
#include "render/Camera.hpp"

#include <SDL3/SDL_mouse.h>

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

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

#include <vector>

namespace mengine {
namespace {

// Vulkan-glTF-PBR constants (VulkanExampleBase.cpp).
constexpr float kZoomPerPixel = 0.005f;       // right drag, times movementSpeed
constexpr float kWheelUnitsPerNotch = 120.0f; // WHEEL_DELTA: Win32 reports 120 per notch
constexpr float kZoomPerWheelUnit = 0.005f;   // times movementSpeed
constexpr float kPanPerPixel = 0.01f;         // middle drag
constexpr float kTouchRotationScale = 0.5f;   // of rotationSpeed
constexpr float kPinchZoomStep = 0.03f;

// Vulkan-glTF-PBR works in window pixels; Input reports window points.
float PixelsPerPoint(const Window& window)
{
    int width = 0;
    int height = 0;
    window.GetSize(width, height);
    uint32_t pixelWidth = 0;
    uint32_t pixelHeight = 0;
    window.GetPixelSize(pixelWidth, pixelHeight);
    return width > 0 && pixelWidth > 0 ? static_cast<float>(pixelWidth) / static_cast<float>(width) : 1.0f;
}

} // namespace

void OrbitCameraController::ApplyTo(render::Camera& camera) const
{
    camera.pitchDegrees = m_rotation.x;
    camera.yawDegrees = -m_rotation.y;
    const glm::vec3 viewTranslation = UnitLength() * glm::vec3(m_position.x, -m_position.y, -m_position.z);
    camera.position = m_scene.Center() - glm::transpose(camera.ViewRotation()) * viewTranslation;
    camera.farPlane = FarPlaneFor(camera);
}

void OrbitCameraController::AdoptFrom(const render::Camera& camera)
{
    m_rotation = glm::vec2(camera.pitchDegrees, -camera.yawDegrees);
    const glm::vec3 viewTranslation = camera.ViewRotation() * (m_scene.Center() - camera.position);
    m_position = glm::vec3(viewTranslation.x, -viewTranslation.y, -viewTranslation.z) / UnitLength();
}

void OrbitCameraController::ResetView(render::Camera& camera)
{
    m_rotation = glm::vec2(0.0f);
    m_position = glm::vec3(0.0f, 0.0f, 1.0f);
    ApplyTo(camera);
}

void OrbitCameraController::Activate(render::Camera& camera)
{
    AdoptFrom(camera);
    ApplyTo(camera);
}

void OrbitCameraController::Deactivate(Window& window)
{
    static_cast<void>(window);
    m_pinchFingers.reset();
}

void OrbitCameraController::UpdateTouches(const CameraInput& context, float pixelsPerPoint)
{
    std::vector<const Input::Touch*> fingers;
    for (const Input::Touch& touch : context.input.Touches())
    {
        if (!touch.startedOverUi)
        {
            fingers.push_back(&touch);
        }
    }

    if (fingers.size() >= 2)
    {
        // Vulkan-glTF-PBR compares the finger distance with the previous one on every move and zooms a fixed step.
        // Tracking starts when the pair forms, so the first frame does not jump.
        const std::pair<SDL_FingerID, SDL_FingerID> pair(fingers[0]->id, fingers[1]->id);
        const float distance = glm::length(fingers[1]->position - fingers[0]->position);
        if (m_pinchFingers == pair)
        {
            if (distance < m_pinchDistance)
            {
                m_position.z += kPinchZoomStep;
            }
            else if (distance > m_pinchDistance)
            {
                m_position.z -= kPinchZoomStep;
            }
        }
        m_pinchFingers = pair;
        m_pinchDistance = distance;
        return;
    }

    m_pinchFingers.reset();
    if (fingers.size() == 1)
    {
        const glm::vec2 delta = fingers[0]->delta * pixelsPerPoint;
        const float scale = rotationSpeed * kTouchRotationScale;
        m_rotation.x -= delta.y * scale;
        m_rotation.y += delta.x * scale;
    }
}

void OrbitCameraController::Update(render::Camera& camera, const CameraInput& context, float /*deltaSeconds*/)
{
    // Vulkan-glTF-PBR's motion is per event, not per second.
    const Input& input = context.input;
    const float pixelsPerPoint = PixelsPerPoint(context.window);

    if (!context.uiWantsMouse)
    {
        // handleMouseMove: dx, dy = previous - current position.
        const glm::vec2 delta = input.MouseDelta() * pixelsPerPoint;
        const float dx = -delta.x;
        const float dy = -delta.y;
        if (input.IsMouseButtonHeld(SDL_BUTTON_LEFT))
        {
            m_rotation += glm::vec2(dy * rotationSpeed, -dx * rotationSpeed);
        }
        if (input.IsMouseButtonHeld(SDL_BUTTON_RIGHT))
        {
            m_position.z += dy * kZoomPerPixel * movementSpeed;
        }
        if (input.IsMouseButtonHeld(SDL_BUTTON_MIDDLE))
        {
            m_position.x -= dx * kPanPerPixel;
            m_position.y -= dy * kPanPerPixel;
        }
        // WM_MOUSEWHEEL: translate(0, 0, -wheelDelta * 0.005 * movementSpeed).
        m_position.z -= input.WheelDelta() * kWheelUnitsPerNotch * kZoomPerWheelUnit * movementSpeed;

        UpdateTouches(context, pixelsPerPoint);
    }
    else
    {
        m_pinchFingers.reset();
    }

    ApplyTo(camera);
}

void OrbitCameraController::DrawSettings()
{
    ImGui::SliderFloat("Rotation Speed", &rotationSpeed, 0.05f, 2.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Movement Speed", &movementSpeed, 0.01f, 1.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::Text("Distance: %.2f (x %.2f units)", m_position.z, UnitLength());
}

} // namespace mengine
