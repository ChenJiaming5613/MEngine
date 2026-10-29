#pragma once

#include "rhi/GpuResources.hpp"

#include <glm/vec3.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace mengine::rhi {
class GpuDevice;
}

namespace mengine::render {

// Image based lighting inputs baked offline by IblBaker: specular.ktx2 (RGBA16F cube, PrefilterEnvMap per mip),
// irradiance.ktx2 (RGBA16F cube, E / pi) and brdf_lut.ktx2 (RG16F, IntegrateBRDF).
struct Environment
{
    // Uniform radiance used when no environment is configured or it fails to load.
    static constexpr glm::vec3 kFallbackRadiance{0.05f, 0.05f, 0.06f};

    std::string directory; // empty for the fallback
    bool isFallback = true;

    rhi::TextureHandle specular;
    rhi::TextureHandle irradiance;
    rhi::TextureHandle brdfLut;
    uint32_t specularMipCount = 1;

    rhi::SamplerHandle cubeSampler;
    rhi::SamplerHandle lutSampler;

    void Destroy(rhi::GpuDevice& device);
};

// Loads the three files from `directory` (relative to the asset root). nullopt or any load failure (logged) falls back
// to a uniform environment of kFallbackRadiance; never throws on bad assets.
void LoadEnvironment(rhi::GpuDevice& device, std::optional<std::string_view> directory, Environment& environment);

} // namespace mengine::render
