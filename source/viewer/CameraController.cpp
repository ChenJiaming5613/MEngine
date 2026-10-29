#include "CameraController.hpp"

#include "render/Camera.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>

namespace mengine {

glm::vec3 CameraScene::Center() const
{
    return hasBounds ? 0.5f * (boundsMin + boundsMax) : glm::vec3(0.0f);
}

float CameraScene::Radius() const
{
    return hasBounds ? std::max(0.5f * glm::length(boundsMax - boundsMin), 1e-3f) : 1.0f;
}

float CameraScene::MaxExtent() const
{
    if (!hasBounds)
    {
        return 1.0f;
    }
    const glm::vec3 extent = boundsMax - boundsMin;
    return std::max({extent.x, extent.y, extent.z, 1e-3f});
}

float CameraController::FarPlaneFor(const render::Camera& camera) const
{
    return std::max(kMinFarPlane, 2.0f * (glm::length(camera.position - m_scene.Center()) + m_scene.Radius()));
}

} // namespace mengine
