#include "Bake.hpp"
#include "UeBrdf.hpp"

#include <algorithm>

namespace iblbaker {

std::vector<Cubemap> BakeSpecular(const Cubemap& source, uint32_t mipCount, uint32_t samples, uint32_t threads)
{
    std::vector<Cubemap> mips;
    for (uint32_t mip = 0; mip < mipCount; ++mip)
    {
        const uint32_t size = std::max(1u, source.Size() >> mip);
        const float roughness = mipCount > 1 ? static_cast<float>(mip) / static_cast<float>(mipCount - 1) : 0.0f;
        Cubemap level(size);
        ForEachCubeRow(size, threads, [&](uint32_t face, uint32_t y) {
            for (uint32_t x = 0; x < size; ++x)
            {
                level.At(face, x, y) = PrefilterEnvMap(source, roughness, TexelDirection(face, x, y, size), samples);
            }
        });
        mips.push_back(std::move(level));
    }
    return mips;
}

} // namespace iblbaker
