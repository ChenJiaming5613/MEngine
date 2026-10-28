#include "render/Environment.hpp"

#include "asset/Ktx2.hpp"
#include "core/FileSystem.hpp"
#include "core/Log.hpp"
#include "rhi/GpuDevice.hpp"

#include <glm/gtc/packing.hpp>

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mengine::render {
namespace {

std::vector<uint8_t> Concatenate(const asset::Ktx2Image& image)
{
    std::vector<uint8_t> bytes;
    for (const std::vector<uint8_t>& level : image.levels)
    {
        bytes.insert(bytes.end(), level.begin(), level.end());
    }
    return bytes;
}

// Reads and validates one KTX2 file; throws std::runtime_error with a message naming the file.
asset::Ktx2Image LoadKtx2(const std::string& path, uint32_t expectedFormat, uint32_t expectedFaces)
{
    const std::optional<std::vector<uint8_t>> bytes = ReadAssetFile(path);
    if (!bytes)
    {
        throw std::runtime_error(path + " not found");
    }
    std::string error;
    std::optional<asset::Ktx2Image> image = asset::DecodeKtx2(*bytes, &error);
    if (!image)
    {
        throw std::runtime_error(path + ": " + error);
    }
    if (image->vkFormat != expectedFormat || image->faceCount != expectedFaces)
    {
        throw std::runtime_error(path + ": expected VkFormat " + std::to_string(expectedFormat) + " with " +
                                 std::to_string(expectedFaces) + " faces, got VkFormat " + std::to_string(image->vkFormat) +
                                 " with " + std::to_string(image->faceCount));
    }
    return std::move(*image);
}

rhi::TextureHandle CreateEnvironmentTexture(rhi::GpuDevice& device, const asset::Ktx2Image& image, VkFormat format,
    const std::string& name)
{
    const std::vector<uint8_t> data = Concatenate(image);
    rhi::TextureCreation creation;
    creation.width = image.width;
    creation.height = image.height;
    creation.mipLevels = static_cast<uint32_t>(image.levels.size());
    creation.type = image.faceCount == 6 ? rhi::TextureType::TextureCube : rhi::TextureType::Texture2D;
    creation.format = format;
    creation.initialData = data.data();
    creation.initialDataSize = data.size();
    creation.name = name.c_str();
    return device.CreateTexture(creation);
}

asset::Ktx2Image SolidImage(uint32_t vkFormat, uint32_t faceCount, std::span<const float> texel)
{
    asset::Ktx2Image image;
    image.vkFormat = vkFormat;
    image.width = 1;
    image.height = 1;
    image.faceCount = faceCount;
    std::vector<uint8_t> level;
    for (uint32_t face = 0; face < faceCount; ++face)
    {
        for (float value : texel)
        {
            const uint16_t half = glm::packHalf1x16(value);
            level.push_back(static_cast<uint8_t>(half & 0xFF));
            level.push_back(static_cast<uint8_t>(half >> 8));
        }
    }
    image.levels.push_back(std::move(level));
    return image;
}

void CreateFallback(rhi::GpuDevice& device, Environment& environment)
{
    const glm::vec3 c = Environment::kFallbackRadiance;
    const std::array<float, 4> radiance = {c.r, c.g, c.b, 1.0f};
    // A uniform environment has E / pi equal to its radiance. The LUT stores (A, B) = (1, 0), so the specular term
    // reduces to radiance * F0: the fallback only keeps metals from turning black and is not a split sum result.
    const std::array<float, 2> lut = {1.0f, 0.0f};
    environment.specular = CreateEnvironmentTexture(device,
        SolidImage(asset::kVkFormatR16G16B16A16Sfloat, 6, radiance), VK_FORMAT_R16G16B16A16_SFLOAT, "FallbackSpecular");
    environment.irradiance = CreateEnvironmentTexture(device,
        SolidImage(asset::kVkFormatR16G16B16A16Sfloat, 6, radiance), VK_FORMAT_R16G16B16A16_SFLOAT, "FallbackIrradiance");
    environment.brdfLut = CreateEnvironmentTexture(device, SolidImage(asset::kVkFormatR16G16Sfloat, 1, lut),
        VK_FORMAT_R16G16_SFLOAT, "FallbackBrdfLut");
    environment.specularMipCount = 1;
    environment.isFallback = true;
    environment.directory.clear();
}

void CreateSamplers(rhi::GpuDevice& device, Environment& environment)
{
    rhi::SamplerCreation cube;
    cube.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cube.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cube.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cube.name = "EnvironmentCubeSampler";
    environment.cubeSampler = device.CreateSampler(cube);

    // The LUT stores IntegrateBRDF at texel centers; bilinear filtering with clamping interpolates between them.
    rhi::SamplerCreation lut = cube;
    lut.maxLod = 0.0f;
    lut.name = "BrdfLutSampler";
    environment.lutSampler = device.CreateSampler(lut);
}

} // namespace

void Environment::Destroy(rhi::GpuDevice& device)
{
    device.DestroyTexture(specular);
    device.DestroyTexture(irradiance);
    device.DestroyTexture(brdfLut);
    device.DestroySampler(cubeSampler);
    device.DestroySampler(lutSampler);
    *this = Environment{};
}

void LoadEnvironment(rhi::GpuDevice& device, std::optional<std::string_view> directory, Environment& environment)
{
    environment.Destroy(device);
    CreateSamplers(device, environment);

    if (directory)
    {
        const std::string base(*directory);
        try
        {
            const asset::Ktx2Image specular =
                LoadKtx2(JoinPath(base, "specular.ktx2"), asset::kVkFormatR16G16B16A16Sfloat, 6);
            const asset::Ktx2Image irradiance =
                LoadKtx2(JoinPath(base, "irradiance.ktx2"), asset::kVkFormatR16G16B16A16Sfloat, 6);
            const asset::Ktx2Image brdfLut = LoadKtx2(JoinPath(base, "brdf_lut.ktx2"), asset::kVkFormatR16G16Sfloat, 1);

            device.BeginUploadBatch();
            try
            {
                environment.specular = CreateEnvironmentTexture(device, specular, VK_FORMAT_R16G16B16A16_SFLOAT,
                    JoinPath(base, "specular.ktx2"));
                environment.irradiance = CreateEnvironmentTexture(device, irradiance, VK_FORMAT_R16G16B16A16_SFLOAT,
                    JoinPath(base, "irradiance.ktx2"));
                environment.brdfLut =
                    CreateEnvironmentTexture(device, brdfLut, VK_FORMAT_R16G16_SFLOAT, JoinPath(base, "brdf_lut.ktx2"));
            }
            catch (...)
            {
                device.EndUploadBatch();
                throw;
            }
            device.EndUploadBatch();

            environment.specularMipCount = static_cast<uint32_t>(specular.levels.size());
            environment.isFallback = false;
            environment.directory = base;
            MENGINE_LOG_INFO("Loaded environment %s: specular %ux%u with %u mips, irradiance %ux%u, BRDF LUT %ux%u",
                base.c_str(), specular.width, specular.height, environment.specularMipCount, irradiance.width,
                irradiance.height, brdfLut.width, brdfLut.height);
            return;
        }
        catch (const std::exception& exception)
        {
            MENGINE_LOG_ERROR("Failed to load environment %s: %s; using a uniform environment", base.c_str(), exception.what());
            device.DestroyTexture(environment.specular);
            device.DestroyTexture(environment.irradiance);
            device.DestroyTexture(environment.brdfLut);
            environment.specular = {};
            environment.irradiance = {};
            environment.brdfLut = {};
        }
    }
    CreateFallback(device, environment);
}

} // namespace mengine::render
