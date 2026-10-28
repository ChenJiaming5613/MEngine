#include "Bake.hpp"
#include "UeBrdf.hpp"

#include <cmath>

namespace iblbaker {

glm::vec3 IrradianceOverPi(const Cubemap& source, const glm::vec3& N, uint32_t samples)
{
    // Same tangent frame as ImportanceSampleGgx.
    const glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
    const glm::vec3 tangentX = glm::normalize(glm::cross(up, N));
    const glm::vec3 tangentY = glm::cross(N, tangentX);

    glm::vec3 sum(0.0f);
    for (uint32_t i = 0; i < samples; ++i)
    {
        // Cosine weighted hemisphere: pdf = cos(theta) / pi.
        const glm::vec2 Xi = Hammersley(i, samples);
        const float phi = 2.0f * PI * Xi.x;
        const float cosTheta = std::sqrt(1.0f - Xi.y);
        const float sinTheta = std::sqrt(Xi.y);
        const glm::vec3 L = tangentX * (sinTheta * std::cos(phi)) + tangentY * (sinTheta * std::sin(phi)) + N * cosTheta;
        sum += source.Sample(L);
    }
    return sum / static_cast<float>(samples);
}

Cubemap BakeIrradiance(const Cubemap& source, uint32_t size, uint32_t samples, uint32_t threads)
{
    Cubemap irradiance(size);
    ForEachCubeRow(size, threads, [&](uint32_t face, uint32_t y) {
        for (uint32_t x = 0; x < size; ++x)
        {
            irradiance.At(face, x, y) = IrradianceOverPi(source, TexelDirection(face, x, y, size), samples);
        }
    });
    return irradiance;
}

} // namespace iblbaker
