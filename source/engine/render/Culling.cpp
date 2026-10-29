#include "render/Culling.hpp"

#include "render/Scene.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace mengine::render {

Frustum Frustum::FromViewProjection(const glm::mat4& viewProjection)
{
    // glm is column major: row i of the matrix is (m[0][i], m[1][i], m[2][i], m[3][i]).
    const auto row = [&viewProjection](int i) {
        return glm::vec4(viewProjection[0][i], viewProjection[1][i], viewProjection[2][i], viewProjection[3][i]);
    };
    const glm::vec4 x = row(0);
    const glm::vec4 y = row(1);
    const glm::vec4 z = row(2);
    const glm::vec4 w = row(3);

    Frustum frustum;
    frustum.planes[kLeft] = w + x;
    frustum.planes[kRight] = w - x;
    frustum.planes[kBottom] = w + y;
    frustum.planes[kTop] = w - y;
    frustum.planes[kNear] = z; // Vulkan depth range: 0 <= z
    frustum.planes[kFar] = w - z;
    for (glm::vec4& plane : frustum.planes)
    {
        const float length = glm::length(glm::vec3(plane));
        // A degenerate plane stays zero and accepts everything.
        plane = length > 0.0f ? plane / length : glm::vec4(0.0f);
    }
    return frustum;
}

bool Frustum::IntersectsAabb(const glm::vec3& min, const glm::vec3& max) const
{
    if (min.x > max.x || min.y > max.y || min.z > max.z)
    {
        return true;
    }
    const glm::vec3 center = 0.5f * (min + max);
    const glm::vec3 extent = 0.5f * (max - min);
    for (const glm::vec4& plane : planes)
    {
        const glm::vec3 normal(plane);
        // Signed distance of the center plus the box's projected radius onto the normal: the corner furthest along it.
        const float distance = glm::dot(normal, center) + plane.w;
        const float radius = glm::dot(glm::abs(normal), extent);
        if (distance + radius < 0.0f)
        {
            return false;
        }
    }
    return true;
}

void CullDraws(const Scene& scene, const Frustum& frustum, std::vector<uint32_t>& visible)
{
    visible.clear();
    visible.reserve(scene.draws.size());
    for (uint32_t index = 0; index < static_cast<uint32_t>(scene.draws.size()); ++index)
    {
        const SceneDraw& draw = scene.draws[index];
        if (frustum.IntersectsAabb(draw.boundsMin, draw.boundsMax))
        {
            visible.push_back(index);
        }
    }
}

} // namespace mengine::render
