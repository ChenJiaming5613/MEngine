// Bakes MEngine's IBL inputs from an equirectangular HDR image or a KTX 1 cube map, following the split sum of Brian
// Karis, "Real Shading in Unreal Engine 4" (SIGGRAPH 2013 course notes; docs/UE4-RealShading-2013.md, section 6).
//
// --flip-y mirrors the input along y, for cube maps authored in a Y down world such as Vulkan-glTF-PBR's papermill.ktx.
//
// Writes <out_dir>/specular.ktx2 (RGBA16F cube, PrefilterEnvMap per mip), irradiance.ktx2 (RGBA16F cube, E / pi) and
// brdf_lut.ktx2 (RG16F, IntegrateBrdf).

#include "Bake.hpp"
#include "Cubemap.hpp"
#include "Ktx1Cube.hpp"
#include "SelfTest.hpp"

#include "asset/ImageLoader.hpp"
#include "asset/Ktx2.hpp"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct Options
{
    std::string input;
    std::string outputDirectory;
    uint32_t cubeSize = 512;
    uint32_t specularMips = 6;
    uint32_t irradianceSize = 32;
    uint32_t lutSize = 256;
    uint32_t samples = 1024;
    bool flipY = false;
    bool selfTest = false;
};

void PrintUsage()
{
    std::fprintf(stderr,
        "Usage:\n"
        "  IblBaker <in.hdr|in.ktx> <out_dir> [--cube-size 512] [--specular-mips 6] [--irradiance-size 32]\n"
        "           [--lut-size 256] [--samples 1024] [--flip-y]\n"
        "  IblBaker --self-test [--samples 1024] [--lut-size 256]\n");
}

std::optional<uint32_t> ParseCount(std::string_view text)
{
    char* end = nullptr;
    const std::string value(text);
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (value.empty() || end != value.c_str() + value.size() || parsed == 0 || parsed > 65536)
    {
        return std::nullopt;
    }
    return static_cast<uint32_t>(parsed);
}

std::optional<Options> ParseOptions(int argc, char** argv)
{
    Options options;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        if (argument == "--self-test")
        {
            options.selfTest = true;
            continue;
        }
        if (argument == "--flip-y")
        {
            options.flipY = true;
            continue;
        }
        uint32_t* target = nullptr;
        if (argument == "--cube-size")
        {
            target = &options.cubeSize;
        }
        else if (argument == "--specular-mips")
        {
            target = &options.specularMips;
        }
        else if (argument == "--irradiance-size")
        {
            target = &options.irradianceSize;
        }
        else if (argument == "--lut-size")
        {
            target = &options.lutSize;
        }
        else if (argument == "--samples")
        {
            target = &options.samples;
        }
        if (target != nullptr)
        {
            const std::optional<uint32_t> value = i + 1 < argc ? ParseCount(argv[i + 1]) : std::nullopt;
            if (!value)
            {
                std::fprintf(stderr, "%.*s expects a positive integer\n", static_cast<int>(argument.size()), argument.data());
                return std::nullopt;
            }
            *target = *value;
            ++i;
            continue;
        }
        if (argument.starts_with("--"))
        {
            std::fprintf(stderr, "Unknown option %.*s\n", static_cast<int>(argument.size()), argument.data());
            return std::nullopt;
        }
        positional.emplace_back(argument);
    }

    if (options.selfTest)
    {
        if (!positional.empty())
        {
            std::fprintf(stderr, "--self-test takes no input or output paths\n");
            return std::nullopt;
        }
        return options;
    }
    if (positional.size() != 2)
    {
        return std::nullopt;
    }
    options.input = positional[0];
    options.outputDirectory = positional[1];

    uint32_t maxMips = 1;
    while ((options.cubeSize >> maxMips) > 0)
    {
        ++maxMips;
    }
    if (options.specularMips > maxMips)
    {
        std::fprintf(stderr, "--specular-mips %u exceeds the %u levels of a %u cube\n", options.specularMips, maxMips,
            options.cubeSize);
        return std::nullopt;
    }
    return options;
}

void AppendHalf(std::vector<uint8_t>& bytes, float value)
{
    const uint16_t half = glm::packHalf1x16(value);
    bytes.push_back(static_cast<uint8_t>(half & 0xFF));
    bytes.push_back(static_cast<uint8_t>(half >> 8));
}

std::vector<uint8_t> CubeLevelRgba16f(const iblbaker::Cubemap& cube)
{
    std::vector<uint8_t> bytes;
    bytes.reserve(static_cast<size_t>(cube.Size()) * cube.Size() * 6 * 8);
    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t y = 0; y < cube.Size(); ++y)
        {
            for (uint32_t x = 0; x < cube.Size(); ++x)
            {
                const glm::vec3& texel = cube.At(face, x, y);
                AppendHalf(bytes, texel.r);
                AppendHalf(bytes, texel.g);
                AppendHalf(bytes, texel.b);
                AppendHalf(bytes, 1.0f);
            }
        }
    }
    return bytes;
}

bool WriteKtx2(const std::filesystem::path& path, const mengine::asset::Ktx2Image& image)
{
    std::string error;
    const std::vector<uint8_t> bytes = mengine::asset::EncodeKtx2(image, &error);
    if (bytes.empty())
    {
        std::fprintf(stderr, "Failed to encode %s: %s\n", path.string().c_str(), error.c_str());
        return false;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file)
    {
        std::fprintf(stderr, "Failed to write %s\n", path.string().c_str());
        return false;
    }
    std::printf("Wrote %s (%zu bytes)\n", path.string().c_str(), bytes.size());
    return true;
}

bool WriteCube(const std::filesystem::path& path, const std::vector<iblbaker::Cubemap>& mips)
{
    mengine::asset::Ktx2Image image;
    image.vkFormat = mengine::asset::kVkFormatR16G16B16A16Sfloat;
    image.width = mips.front().Size();
    image.height = mips.front().Size();
    image.faceCount = 6;
    for (const iblbaker::Cubemap& mip : mips)
    {
        image.levels.push_back(CubeLevelRgba16f(mip));
    }
    return WriteKtx2(path, image);
}

bool WriteLut(const std::filesystem::path& path, const std::vector<glm::vec2>& lut, uint32_t size)
{
    mengine::asset::Ktx2Image image;
    image.vkFormat = mengine::asset::kVkFormatR16G16Sfloat;
    image.width = size;
    image.height = size;
    std::vector<uint8_t> level;
    level.reserve(lut.size() * 4);
    for (const glm::vec2& texel : lut)
    {
        AppendHalf(level, texel.x);
        AppendHalf(level, texel.y);
    }
    image.levels.push_back(std::move(level));
    return WriteKtx2(path, image);
}

class Stopwatch
{
public:
    double Seconds() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count(); }

private:
    std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();
};

} // namespace

int main(int argc, char** argv)
{
    const std::optional<Options> options = ParseOptions(argc, argv);
    if (!options)
    {
        PrintUsage();
        return 2;
    }

    if (options->selfTest)
    {
        return iblbaker::RunSelfTest(options->samples, options->lutSize, std::max(1u, std::thread::hardware_concurrency()))
                   ? 0
                   : 1;
    }

    std::ifstream file(options->input, std::ios::binary);
    if (!file)
    {
        std::fprintf(stderr, "Cannot open %s\n", options->input.c_str());
        return 1;
    }
    const std::vector<uint8_t> encoded((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string error;
    const uint32_t threads = std::max(1u, std::thread::hardware_concurrency());
    Stopwatch total;

    // A KTX 1 cube is used as is (resampled only when --cube-size differs); an HDR image is projected onto the cube.
    iblbaker::Cubemap cube;
    std::string sourceDescription;
    if (iblbaker::IsKtx1(encoded))
    {
        std::optional<iblbaker::Cubemap> source = iblbaker::DecodeKtx1Cube(encoded, &error);
        if (!source)
        {
            std::fprintf(stderr, "Cannot decode %s: %s\n", options->input.c_str(), error.c_str());
            return 1;
        }
        sourceDescription = "KTX cube " + std::to_string(source->Size());
        cube = source->Size() == options->cubeSize ? std::move(*source)
                                                   : iblbaker::ResampleCube(*source, options->cubeSize, threads);
    }
    else
    {
        const std::optional<mengine::asset::ImageFloat> source = mengine::asset::DecodeImageHdr(encoded, &error);
        if (!source)
        {
            std::fprintf(stderr, "Cannot decode %s: %s\n", options->input.c_str(), error.c_str());
            return 1;
        }
        sourceDescription = "equirect " + std::to_string(source->width) + "x" + std::to_string(source->height);
        cube = iblbaker::EquirectToCube(*source, options->cubeSize, threads);
    }
    if (options->flipY)
    {
        cube = iblbaker::MirrorY(cube);
    }
    std::printf("%s: %s%s, cube %u, %u specular mips, irradiance %u, LUT %u, %u samples, %u threads\n",
        options->input.c_str(), sourceDescription.c_str(), options->flipY ? " (y mirrored)" : "", options->cubeSize,
        options->specularMips, options->irradianceSize, options->lutSize, options->samples, threads);

    const std::filesystem::path outputDirectory(options->outputDirectory);
    std::error_code directoryError;
    std::filesystem::create_directories(outputDirectory, directoryError);
    if (directoryError)
    {
        std::fprintf(stderr, "Cannot create %s: %s\n", outputDirectory.string().c_str(), directoryError.message().c_str());
        return 1;
    }

    Stopwatch specularTime;
    const std::vector<iblbaker::Cubemap> specular =
        iblbaker::BakeSpecular(cube, options->specularMips, options->samples, threads);
    std::printf("Specular prefiltered in %.1f s\n", specularTime.Seconds());

    Stopwatch irradianceTime;
    const iblbaker::Cubemap irradiance =
        iblbaker::BakeIrradiance(cube, options->irradianceSize, options->samples, threads);
    std::printf("Irradiance in %.1f s\n", irradianceTime.Seconds());

    Stopwatch lutTime;
    const std::vector<glm::vec2> lut = iblbaker::BakeBrdfLut(options->lutSize, options->samples, threads);
    std::printf("BRDF LUT in %.1f s\n", lutTime.Seconds());

    const bool written = WriteCube(outputDirectory / "specular.ktx2", specular) &&
                         WriteCube(outputDirectory / "irradiance.ktx2", {irradiance}) &&
                         WriteLut(outputDirectory / "brdf_lut.ktx2", lut, options->lutSize);
    std::printf("Total %.1f s\n", total.Seconds());
    return written ? 0 : 1;
}
