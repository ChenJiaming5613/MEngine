#include "SelfTest.hpp"

#include "Bake.hpp"
#include "UeBrdf.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace iblbaker {
namespace {

const glm::vec3 kConstantRadiance(0.25f, 1.0f, 4.0f);
constexpr uint32_t kTestCubeSize = 16;
constexpr uint32_t kTestSpecularMips = 5;
constexpr uint32_t kTestIrradianceSize = 8;
constexpr float kRelativeTolerance = 1e-4f;
constexpr float kSplitSumTolerance = 1e-3f;
constexpr float kLutSumTolerance = 1e-3f;
constexpr float kLutLimitTolerance = 1e-2f;

float RelativeError(const glm::vec3& value, const glm::vec3& expected)
{
    float error = 0.0f;
    for (int c = 0; c < 3; ++c)
    {
        error = std::max(error, std::abs(value[c] - expected[c]) / std::max(std::abs(expected[c]), 1e-6f));
    }
    return error;
}

struct Worst
{
    float error = -1.0f;
    std::string where;

    void Update(float candidate, const std::string& location)
    {
        if (candidate > error || std::isnan(candidate))
        {
            error = std::isnan(candidate) ? INFINITY : candidate;
            where = location;
        }
    }
};

bool Report(const char* name, const Worst& worst, float tolerance)
{
    const bool pass = worst.error <= tolerance;
    std::printf("[%s] %s: max deviation %.3g (tolerance %.1g) at %s\n", pass ? "PASS" : "FAIL", name, worst.error, tolerance,
        worst.where.c_str());
    return pass;
}

std::string TexelLabel(uint32_t mip, uint32_t face, uint32_t x, uint32_t y)
{
    return "mip " + std::to_string(mip) + " face " + std::to_string(face) + " texel (" + std::to_string(x) + ", " +
           std::to_string(y) + ")";
}

void CheckCube(const Cubemap& cube, uint32_t mip, const glm::vec3& expected, Worst& worst)
{
    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t y = 0; y < cube.Size(); ++y)
        {
            for (uint32_t x = 0; x < cube.Size(); ++x)
            {
                worst.Update(RelativeError(cube.At(face, x, y), expected), TexelLabel(mip, face, x, y));
            }
        }
    }
}

} // namespace

bool RunSelfTest(uint32_t samples, uint32_t lutSize, uint32_t threads)
{
    std::printf("IblBaker self-test: %u samples, LUT %ux%u, constant radiance (%.2f, %.2f, %.2f)\n", samples, lutSize, lutSize,
        kConstantRadiance.r, kConstantRadiance.g, kConstantRadiance.b);
    const Cubemap environment = ConstantCube(kTestCubeSize, kConstantRadiance);
    bool pass = true;

    // p5: for a constant L_i the prefiltered environment is that constant at every roughness.
    {
        const std::vector<Cubemap> mips = BakeSpecular(environment, kTestSpecularMips, samples, threads);
        Worst worst;
        for (uint32_t mip = 0; mip < mips.size(); ++mip)
        {
            CheckCube(mips[mip], mip, kConstantRadiance, worst);
        }
        pass &= Report("PrefilterEnvMap of a constant environment is constant", worst, kRelativeTolerance);
    }

    // Eq. 7: with a constant environment the split sum is exact, SpecularIbl == Prefiltered * (F0 * A + B).
    {
        const std::array<float, 5> roughnesses = {0.05f, 0.25f, 0.5f, 0.75f, 1.0f};
        const std::array<float, 5> noVs = {0.05f, 0.25f, 0.5f, 0.75f, 1.0f};
        const std::array<float, 2> specularColors = {0.04f, 1.0f};
        const glm::vec3 N(0.0f, 0.0f, 1.0f);
        Worst worst;
        for (float roughness : roughnesses)
        {
            for (float NoV : noVs)
            {
                const glm::vec3 V(std::sqrt(1.0f - NoV * NoV), 0.0f, NoV);
                const glm::vec3 R = 2.0f * glm::dot(V, N) * N - V;
                const glm::vec3 prefiltered = PrefilterEnvMap(environment, roughness, R, samples);
                const glm::vec2 envBrdf = IntegrateBrdf(roughness, NoV, samples);
                for (float f0 : specularColors)
                {
                    const glm::vec3 specularColor(f0);
                    const glm::vec3 reference = SpecularIbl(environment, specularColor, roughness, N, V, samples);
                    const glm::vec3 splitSum = prefiltered * (specularColor * envBrdf.x + envBrdf.y);
                    char location[96];
                    std::snprintf(location, sizeof(location), "Roughness %.2f NoV %.2f F0 %.2f", roughness, NoV, f0);
                    worst.Update(RelativeError(splitSum, reference), location);
                }
            }
        }
        pass &= Report("SpecularIbl matches Prefiltered * (F0 * A + B)", worst, kSplitSumTolerance);
    }

    // IntegrateBrdf: A + B is the directional albedo for F = 1 and must not exceed 1.
    {
        const std::vector<glm::vec2> lut = BakeBrdfLut(lutSize, samples, threads);
        Worst sum;
        for (uint32_t y = 0; y < lutSize; ++y)
        {
            for (uint32_t x = 0; x < lutSize; ++x)
            {
                const glm::vec2 ab = lut[static_cast<size_t>(y) * lutSize + x];
                char location[96];
                std::snprintf(location, sizeof(location), "texel (%u, %u) A %.4f B %.4f", x, y, ab.x, ab.y);
                sum.Update(ab.x + ab.y - 1.0f, location);
            }
        }
        pass &= Report("BRDF LUT A + B - 1 <= epsilon", sum, kLutSumTolerance);

        // Smallest roughness, largest NoV: a mirror seen head on, (A, B) -> (1, 0).
        const glm::vec2 limit = lut[lutSize - 1];
        Worst worst;
        char location[96];
        std::snprintf(location, sizeof(location), "texel (%u, 0) A %.4f B %.4f", lutSize - 1, limit.x, limit.y);
        worst.Update(std::max(std::abs(limit.x - 1.0f), std::abs(limit.y)), location);
        pass &= Report("BRDF LUT at minimum roughness and maximum NoV is (1, 0)", worst, kLutLimitTolerance);
    }

    // Diffuse addition: E / pi of a constant environment is that constant.
    {
        const Cubemap irradiance = BakeIrradiance(environment, kTestIrradianceSize, samples, threads);
        Worst worst;
        CheckCube(irradiance, 0, kConstantRadiance, worst);
        pass &= Report("Irradiance of a constant environment is constant", worst, kRelativeTolerance);
    }

    std::printf("Self-test %s\n", pass ? "passed" : "FAILED");
    return pass;
}

} // namespace iblbaker
