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

glm::mat4 Camera::View() const
{
    return glm::lookAt(position, position + Forward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::Projection(float aspectRatio) const
{
    glm::mat4 projection = glm::perspective(glm::radians(fovYDegrees), aspectRatio, nearPlane, farPlane);
    projection[1][1] *= -1.0f;
    return projection;
}

void Camera::ClampPitch()
{
    pitchDegrees = std::clamp(pitchDegrees, -kMaxPitchDegrees, kMaxPitchDegrees);
}

} // namespace mengine::render
