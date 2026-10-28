#include "Bake.hpp"
#include "UeBrdf.hpp"

namespace iblbaker {

std::vector<glm::vec2> BakeBrdfLut(uint32_t size, uint32_t samples, uint32_t threads)
{
    std::vector<glm::vec2> lut(static_cast<size_t>(size) * size);
    ParallelFor(size, threads, [&](uint32_t y) {
        const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
        for (uint32_t x = 0; x < size; ++x)
        {
            const float NoV = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            lut[static_cast<size_t>(y) * size + x] = IntegrateBrdf(roughness, NoV, samples);
        }
    });
    return lut;
}

} // namespace iblbaker
