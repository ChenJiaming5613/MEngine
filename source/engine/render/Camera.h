#pragma once

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace mengine::render {

// Perspective camera without roll. yaw 0 / pitch 0 looks down -Z; positive yaw turns left, positive pitch looks up.
// The view is Rx(-pitch) * Ry(-yaw) * T(-position), the same as lookAt with +Y up while |pitch| < 90; beyond that it
// keeps going over the top (upside down), which the orbit controller relies on.
class Camera
{
public:
    static constexpr float kMaxPitchDegrees = 89.0f;

    glm::vec3 position{0.0f};
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
    float fovYDegrees = 60.0f;
    float nearPlane = 0.05f;
    float farPlane = 500.0f;

    glm::vec3 Forward() const;
    // Horizontal, perpendicular to Forward().
    glm::vec3 Right() const;

    // Rotation part of View(): world to view space.
    glm::mat3 ViewRotation() const;
    glm::mat4 View() const;
    // Vulkan clip space: depth in [0, 1] and Y pointing down (the Y flip keeps counter-clockwise faces front facing).
    glm::mat4 Projection(float aspectRatio) const;

    // Brings an upside down orientation (|pitch| > 90) upright with the same forward direction, then clamps the pitch
    // to +-kMaxPitchDegrees.
    void ClampPitch();
};

} // namespace mengine::render
