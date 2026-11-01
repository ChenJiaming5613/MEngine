#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace mengine::render {

struct Scene;

// The volume a view projection maps to Vulkan clip space (-w <= x, y <= w, 0 <= z <= w) as six inward facing planes,
// extracted from the matrix rows (Gribb & Hartmann 2001). A point p is inside when dot(plane.xyz, p) + plane.w >= 0 for
// every plane. Works for perspective and orthographic projections and ignores the Y flip.
struct Frustum
{
    enum Plane : uint32_t
    {
        kLeft,
        kRight,
        kBottom,
        kTop,
        kNear,
        kFar,
        kPlaneCount,
    };

    // Normalized, so plane.w is a distance in world units.
    std::array<glm::vec4, kPlaneCount> planes{};

    static Frustum FromViewProjection(const glm::mat4& viewProjection);

    // Conservative: false only when the box lies entirely outside one plane. Boxes near the frustum's edges and corners
    // can pass without touching it, which costs a draw, never a missing one. An empty box (min > max) is visible.
    bool IntersectsAabb(const glm::vec3& min, const glm::vec3& max) const;
};

// Indices of the scene draws whose world bounds intersect the frustum, in draw order (the pipeline / material sort is
// kept). Replaces the contents of visible.
void CullDraws(const Scene& scene, const Frustum& frustum, std::vector<uint32_t>& visible);

} // namespace mengine::render
