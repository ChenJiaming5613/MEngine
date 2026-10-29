#pragma once

#include "asset/ImageLoader.hpp"

#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace iblbaker {

// RGB float cube map. Faces in Vulkan order +X, -X, +Y, -Y, +Z, -Z; texel (x, y) of a face maps to the (s, t)
// coordinates of the Vulkan cube map face selection table (Vulkan spec, "Cube Map Face Selection"), row 0 at t = 0.
class Cubemap
{
public:
    Cubemap() = default;
    explicit Cubemap(uint32_t size);

    uint32_t Size() const { return m_size; }
    glm::vec3& At(uint32_t face, uint32_t x, uint32_t y) { return m_faces[face][static_cast<size_t>(y) * m_size + x]; }
    const glm::vec3& At(uint32_t face, uint32_t x, uint32_t y) const { return m_faces[face][static_cast<size_t>(y) * m_size + x]; }

    // Bilinear filtering inside the selected face, clamped at its edges; level 0 only, like textureCube(EnvMap, L) in
    // the notes.
    glm::vec3 Sample(const glm::vec3& direction) const;

private:
    uint32_t m_size = 0;
    std::array<std::vector<glm::vec3>, 6> m_faces;
};

// Unit direction through the center of texel (x, y) of `face`.
glm::vec3 TexelDirection(uint32_t face, uint32_t x, uint32_t y, uint32_t size);

// Resamples an equirectangular (latitude-longitude) image with bilinear filtering. u = 0.5 looks down -Z, v = 0 is +Y.
Cubemap EquirectToCube(const mengine::asset::ImageFloat& image, uint32_t size, uint32_t threads);

Cubemap ConstantCube(uint32_t size, const glm::vec3& value);

// result(x, y, z) = source(x, -y, z): converts a cube authored for a Y down world (Vulkan-glTF-PBR negates y of world
// positions and normals) to MEngine's Y up glTF world. Exact texel remap: +Y and -Y swap, every face flips its rows.
Cubemap MirrorY(const Cubemap& source);

Cubemap ResampleCube(const Cubemap& source, uint32_t size, uint32_t threads);

// Calls rowFunction(face, y) for every row on `threads` workers. Texels are independent, so the result does not
// depend on the thread count.
void ForEachCubeRow(uint32_t size, uint32_t threads, const std::function<void(uint32_t face, uint32_t y)>& rowFunction);

void ParallelFor(uint32_t count, uint32_t threads, const std::function<void(uint32_t index)>& function);

} // namespace iblbaker
