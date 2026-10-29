#pragma once

#include "CameraController.hpp"

#include <SDL3/SDL_touch.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <optional>
#include <utility>

namespace mengine {

// Orbit (look-at) camera, a port of Sascha Willems' Vulkan-glTF-PBR (base/camera.hpp with CameraType::lookat, the
// mouse handling of VulkanExampleBase::handleMouseMove / WM_MOUSEWHEEL and the Android touch handling), with its
// default speeds:
//   left drag     rotate, rotationSpeed degrees per pixel (rotation.x by vertical, rotation.y by horizontal motion)
//   right drag    zoom, 0.005 * movementSpeed per pixel; wheel: 120 * 0.005 * movementSpeed per notch
//   middle drag   pan, 0.01 per pixel
//   one finger    rotate at half the mouse speed; two fingers: pinch zoom, 0.03 per frame the distance changes
// There is no pitch limit (the camera goes over the top) and no zoom limit (zooming past the pivot looks back at it).
//
// Vulkan-glTF-PBR scales and centers the model so its largest AABB side is 0.5 and the camera starts at distance 1,
// with view = T(position * (1, 1, -1)) * Rx(rotation.x) * Ry(rotation.y), in a world whose Y is negated. Its state is
// kept in those units here. Undoing the model transform and the Y flip gives the equivalent MEngine camera with pivot
// c (the scene center) and L = 2 * largest side:
//   pitch = rotation.x, yaw = -rotation.y, t = L * (position.x, -position.y, -position.z), camera = c - R^T t
// where R is the camera's view rotation. The mapping is exact (same image), and invertible, so switching from the fly
// controller keeps the current view and simply starts orbiting the scene center.
class OrbitCameraController final : public CameraController
{
public:
    float rotationSpeed = 0.25f;
    float movementSpeed = 0.1f;

    const char* Name() const override { return "Orbit"; }
    const char* Help() const override { return "LMB rotate, RMB drag / wheel zoom, MMB pan; touch: drag rotate, pinch zoom"; }

    // Vulkan-glTF-PBR's resetCamera(): rotation 0, position (0, 0, 1), i.e. the center seen from +Z at distance L.
    void ResetView(render::Camera& camera) override;
    void Activate(render::Camera& camera) override;
    void Deactivate(Window& window) override;
    void Update(render::Camera& camera, const CameraInput& input, float deltaSeconds) override;

    void DrawSettings() override;

private:
    // Scene units per Vulkan-glTF-PBR unit.
    float UnitLength() const { return 2.0f * m_scene.MaxExtent(); }
    void ApplyTo(render::Camera& camera) const;
    void AdoptFrom(const render::Camera& camera);
    void UpdateTouches(const CameraInput& input, float pixelsPerPoint);

    glm::vec2 m_rotation{0.0f}; // degrees, Vulkan-glTF-PBR's rotation.xy
    glm::vec3 m_position{0.0f, 0.0f, 1.0f};
    std::optional<std::pair<SDL_FingerID, SDL_FingerID>> m_pinchFingers;
    float m_pinchDistance = 0.0f;
};

} // namespace mengine
