#include "render/Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace mengine::render {

glm::vec3 Camera::Forward() const
{
    const float yaw = glm::radians(yawDegrees);
    const float pitch = glm::radians(pitchDegrees);
    return glm::vec3(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
}

glm::vec3 Camera::Right() const
{
    const float yaw = glm::radians(yawDegrees);
    return glm::vec3(std::cos(yaw), 0.0f, -std::sin(yaw));
}

glm::mat3 Camera::ViewRotation() const
{
    glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(-pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
    rotation = glm::rotate(rotation, glm::radians(-yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::mat3(rotation);
}

glm::mat4 Camera::View() const
{
    glm::mat4 view(ViewRotation());
    view[3] = glm::vec4(-(ViewRotation() * position), 1.0f);
    return view;
}

glm::mat4 Camera::Projection(float aspectRatio) const
{
    glm::mat4 projection = glm::perspective(glm::radians(fovYDegrees), aspectRatio, nearPlane, farPlane);
    projection[1][1] *= -1.0f;
    return projection;
}

void Camera::ClampPitch()
{
    // Into [-180, 180): an orbit can leave any multiple of a turn behind.
    pitchDegrees = std::remainder(pitchDegrees, 360.0f);
    if (std::abs(pitchDegrees) > 90.0f)
    {
        // (yaw + 180, 180 - pitch) looks the same way without being upside down.
        pitchDegrees = (pitchDegrees > 0.0f ? 180.0f : -180.0f) - pitchDegrees;
        yawDegrees += 180.0f;
    }
    yawDegrees = std::remainder(yawDegrees, 360.0f);
    pitchDegrees = std::clamp(pitchDegrees, -kMaxPitchDegrees, kMaxPitchDegrees);
}

} // namespace mengine::render
