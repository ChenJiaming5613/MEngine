#pragma once

#include "Cubemap.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace iblbaker {

// Specular cube: mip m is PrefilterEnvMap(source, Roughness = m / (mipCount - 1)) at size >> m. Mip 0 is not special
// cased: with Roughness 0 every GGX sample is the mirror direction, so it equals the bilinear source.
std::vector<Cubemap> BakeSpecular(const Cubemap& source, uint32_t mipCount, uint32_t samples, uint32_t threads);

// E(n) / pi = (1 / N) sum L(l_k) with cosine weighted Hammersley samples l_k (the cos / pi pdf cancels). The runtime
// multiplies it by c_diff, which is the Lambert lobe of eq. 1 integrated against the environment (eq. 6).
glm::vec3 IrradianceOverPi(const Cubemap& source, const glm::vec3& N, uint32_t samples);
Cubemap BakeIrradiance(const Cubemap& source, uint32_t size, uint32_t samples, uint32_t threads);

// texel (x, y) = IntegrateBrdf(Roughness = (y + 0.5) / size, NoV = (x + 0.5) / size), row major.
std::vector<glm::vec2> BakeBrdfLut(uint32_t size, uint32_t samples, uint32_t threads);

} // namespace iblbaker
