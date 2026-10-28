#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace mengine::render {

// Free-flying perspective camera. yaw 0 / pitch 0 looks down -Z; positive yaw turns left, positive pitch looks up.
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

    glm::mat4 View() const;
    // Vulkan clip space: depth in [0, 1] and Y pointing down (the Y flip keeps counter-clockwise faces front facing).
    glm::mat4 Projection(float aspectRatio) const;

    void ClampPitch();
};

} // namespace mengine::render
