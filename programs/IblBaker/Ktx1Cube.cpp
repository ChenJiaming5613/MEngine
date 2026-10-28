#include "Ktx1Cube.hpp"

#include <glm/gtc/packing.hpp>

#include <array>
#include <cstring>

namespace iblbaker {
namespace {

constexpr std::array<uint8_t, 12> kKtx1Identifier = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
constexpr uint32_t kEndiannessLittle = 0x04030201;
constexpr uint32_t kGlHalfFloat = 0x140B;
constexpr uint32_t kGlFloat = 0x1406;
constexpr uint32_t kGlRgb = 0x1907;
constexpr uint32_t kGlRgba = 0x1908;
constexpr size_t kHeaderSize = 64;

struct Header
{
    uint32_t endianness;
    uint32_t glType;
    uint32_t glTypeSize;
    uint32_t glFormat;
    uint32_t glInternalFormat;
    uint32_t glBaseInternalFormat;
    uint32_t pixelWidth;
    uint32_t pixelHeight;
    uint32_t pixelDepth;
    uint32_t numberOfArrayElements;
    uint32_t numberOfFaces;
    uint32_t numberOfMipmapLevels;
    uint32_t bytesOfKeyValueData;
};
static_assert(sizeof(Header) == kHeaderSize - kKtx1Identifier.size());

bool Fail(std::string* error, const char* message)
{
    if (error != nullptr)
    {
        *error = message;
    }
    return false;
}

constexpr size_t AlignUp4(size_t value)
{
    return (value + 3) & ~size_t{3};
}

} // namespace

bool IsKtx1(const std::vector<uint8_t>& bytes)
{
    return bytes.size() >= kKtx1Identifier.size() && std::memcmp(bytes.data(), kKtx1Identifier.data(), kKtx1Identifier.size()) == 0;
}

std::optional<Cubemap> DecodeKtx1Cube(const std::vector<uint8_t>& bytes, std::string* error)
{
    if (bytes.size() < kHeaderSize || !IsKtx1(bytes))
    {
        Fail(error, "not a KTX 1.1 file");
        return std::nullopt;
    }
    Header header;
    std::memcpy(&header, bytes.data() + kKtx1Identifier.size(), sizeof(header));
    if (header.endianness != kEndiannessLittle)
    {
        Fail(error, "big endian KTX files are not supported");
        return std::nullopt;
    }
    if (header.numberOfFaces != 6 || header.numberOfArrayElements != 0 || header.pixelDepth != 0)
    {
        Fail(error, "expected a single cube map (6 faces, no array, no depth)");
        return std::nullopt;
    }
    if (header.pixelWidth == 0 || header.pixelWidth != header.pixelHeight)
    {
        Fail(error, "cube faces must be square");
        return std::nullopt;
    }
    const bool isHalf = header.glType == kGlHalfFloat && header.glTypeSize == 2;
    const bool isFloat = header.glType == kGlFloat && header.glTypeSize == 4;
    if (!isHalf && !isFloat)
    {
        Fail(error, "texels must be GL_HALF_FLOAT or GL_FLOAT");
        return std::nullopt;
    }
    if (header.glFormat != kGlRgb && header.glFormat != kGlRgba)
    {
        Fail(error, "format must be GL_RGB or GL_RGBA");
        return std::nullopt;
    }

    const uint32_t size = header.pixelWidth;
    const size_t channels = header.glFormat == kGlRgba ? 4 : 3;
    const size_t texelBytes = channels * header.glTypeSize;
    // KTX 1 rows are padded to GL_UNPACK_ALIGNMENT = 4, every face is followed by cube padding to 4 bytes.
    const size_t rowBytes = AlignUp4(size * texelBytes);
    const size_t faceBytes = rowBytes * size;

    size_t offset = kHeaderSize + header.bytesOfKeyValueData;
    if (offset + 4 > bytes.size())
    {
        Fail(error, "truncated before level 0");
        return std::nullopt;
    }
    uint32_t imageSize = 0;
    std::memcpy(&imageSize, bytes.data() + offset, sizeof(imageSize));
    offset += sizeof(imageSize);
    // For a non array cube map imageSize is the size of one face.
    if (imageSize != faceBytes || offset + 6 * AlignUp4(faceBytes) > bytes.size())
    {
        Fail(error, "level 0 size does not match the header");
        return std::nullopt;
    }

    auto readChannel = [&](const uint8_t* texel, size_t channel) {
        if (isHalf)
        {
            uint16_t half = 0;
            std::memcpy(&half, texel + channel * 2, sizeof(half));
            return glm::unpackHalf1x16(half);
        }
        float value = 0.0f;
        std::memcpy(&value, texel + channel * 4, sizeof(value));
        return value;
    };

    Cubemap cube(size);
    for (uint32_t face = 0; face < 6; ++face)
    {
        const uint8_t* faceData = bytes.data() + offset + face * AlignUp4(faceBytes);
        for (uint32_t y = 0; y < size; ++y)
        {
            const uint8_t* row = faceData + y * rowBytes;
            for (uint32_t x = 0; x < size; ++x)
            {
                const uint8_t* texel = row + x * texelBytes;
                cube.At(face, x, y) = glm::vec3(readChannel(texel, 0), readChannel(texel, 1), readChannel(texel, 2));
            }
        }
    }
    return cube;
}

} // namespace iblbaker
