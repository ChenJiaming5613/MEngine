#include "asset/Ktx2.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <numeric>

namespace mengine::asset {

namespace {

constexpr std::array<uint8_t, 12> kIdentifier = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
constexpr size_t kHeaderSize = 80;
constexpr size_t kLevelIndexEntrySize = 24;

// Khronos Data Format Specification constants used by the basic descriptor block.
constexpr uint8_t kDfModelRgbsda = 1;
constexpr uint8_t kDfPrimariesBt709 = 1;
constexpr uint8_t kDfTransferLinear = 1;
constexpr uint8_t kDfTransferSrgb = 2;
constexpr uint8_t kDfChannelRed = 0;
constexpr uint8_t kDfChannelGreen = 1;
constexpr uint8_t kDfChannelBlue = 2;
constexpr uint8_t kDfChannelAlpha = 15;
constexpr uint8_t kDfQualifierLinear = 0x10;
constexpr uint8_t kDfQualifierSigned = 0x40;
constexpr uint8_t kDfQualifierFloat = 0x80;

struct FormatInfo
{
    uint32_t vkFormat;
    uint32_t bytesPerTexel;
    uint32_t typeSize;
    uint32_t channelCount;
    bool isFloat;
    bool isSrgb;
};

constexpr std::array<FormatInfo, 4> kFormats = {{
    {kVkFormatR8G8B8A8Unorm, 4, 1, 4, false, false},
    {kVkFormatR8G8B8A8Srgb, 4, 1, 4, false, true},
    {kVkFormatR16G16Sfloat, 4, 2, 2, true, false},
    {kVkFormatR16G16B16A16Sfloat, 8, 2, 4, true, false},
}};

const FormatInfo* FindFormat(uint32_t vkFormat)
{
    const auto found = std::find_if(kFormats.begin(), kFormats.end(), [&](const FormatInfo& info) { return info.vkFormat == vkFormat; });
    return found != kFormats.end() ? &*found : nullptr;
}

void SetError(std::string* error, const char* message)
{
    if (error != nullptr)
    {
        *error = message;
    }
}

void AppendU32(std::vector<uint8_t>& out, uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
    {
        out.push_back(static_cast<uint8_t>(value >> shift));
    }
}

void WriteU32(std::vector<uint8_t>& out, size_t offset, uint32_t value)
{
    for (int byte = 0; byte < 4; ++byte)
    {
        out[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
    }
}

void WriteU64(std::vector<uint8_t>& out, size_t offset, uint64_t value)
{
    for (int byte = 0; byte < 8; ++byte)
    {
        out[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
    }
}

uint32_t ReadU32(std::span<const uint8_t> bytes, size_t offset)
{
    uint32_t value = 0;
    for (int byte = 0; byte < 4; ++byte)
    {
        value |= static_cast<uint32_t>(bytes[offset + byte]) << (byte * 8);
    }
    return value;
}

uint64_t ReadU64(std::span<const uint8_t> bytes, size_t offset)
{
    uint64_t value = 0;
    for (int byte = 0; byte < 8; ++byte)
    {
        value |= static_cast<uint64_t>(bytes[offset + byte]) << (byte * 8);
    }
    return value;
}

size_t AlignUp(size_t value, size_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

uint32_t FloatBits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// Basic data format descriptor (KDFS section 5) for the supported uncompressed formats.
std::vector<uint8_t> BuildDataFormatDescriptor(const FormatInfo& format)
{
    constexpr std::array<uint8_t, 4> kChannels = {kDfChannelRed, kDfChannelGreen, kDfChannelBlue, kDfChannelAlpha};
    const uint32_t sampleCount = format.channelCount;
    const uint32_t blockSize = 24 + 16 * sampleCount;
    const uint32_t bitsPerChannel = format.typeSize * 8;

    std::vector<uint8_t> dfd;
    AppendU32(dfd, 4 + blockSize); // dfdTotalSize
    AppendU32(dfd, 0);             // vendorId = KHRONOS, descriptorType = BASICFORMAT
    AppendU32(dfd, 2u | (blockSize << 16));
    AppendU32(dfd, kDfModelRgbsda | (kDfPrimariesBt709 << 8) | ((format.isSrgb ? kDfTransferSrgb : kDfTransferLinear) << 16));
    AppendU32(dfd, 0);                    // texelBlockDimension 1x1x1x1 (stored minus one)
    AppendU32(dfd, format.bytesPerTexel); // bytesPlane0
    AppendU32(dfd, 0);                    // bytesPlane4..7

    for (uint32_t sample = 0; sample < sampleCount; ++sample)
    {
        uint8_t channelType = kChannels[sample];
        uint32_t lower = 0;
        uint32_t upper = (1u << bitsPerChannel) - 1u;
        if (format.isFloat)
        {
            channelType |= kDfQualifierFloat | kDfQualifierSigned;
            lower = FloatBits(-1.0f);
            upper = FloatBits(1.0f);
        }
        else if (format.isSrgb && kChannels[sample] == kDfChannelAlpha)
        {
            channelType |= kDfQualifierLinear;
        }
        AppendU32(dfd, (sample * bitsPerChannel) | ((bitsPerChannel - 1) << 16) | (static_cast<uint32_t>(channelType) << 24));
        AppendU32(dfd, 0); // samplePosition
        AppendU32(dfd, lower);
        AppendU32(dfd, upper);
    }
    return dfd;
}

std::vector<uint8_t> BuildKeyValueData()
{
    constexpr char kKey[] = "KTXwriter";
    constexpr char kValue[] = "MEngine";
    std::vector<uint8_t> kvd;
    AppendU32(kvd, static_cast<uint32_t>(sizeof(kKey) + sizeof(kValue)));
    kvd.insert(kvd.end(), kKey, kKey + sizeof(kKey));
    kvd.insert(kvd.end(), kValue, kValue + sizeof(kValue));
    kvd.resize(AlignUp(kvd.size(), 4), 0);
    return kvd;
}

uint64_t ExpectedLevelSize(const FormatInfo& format, uint32_t width, uint32_t height, uint32_t faceCount, uint32_t level)
{
    return static_cast<uint64_t>(MipExtent(width, level)) * MipExtent(height, level) * format.bytesPerTexel * faceCount;
}

} // namespace

uint32_t Ktx2BytesPerTexel(uint32_t vkFormat)
{
    const FormatInfo* format = FindFormat(vkFormat);
    return format != nullptr ? format->bytesPerTexel : 0;
}

std::vector<uint8_t> EncodeKtx2(const Ktx2Image& image, std::string* error)
{
    const FormatInfo* format = FindFormat(image.vkFormat);
    if (format == nullptr)
    {
        SetError(error, "unsupported vkFormat");
        return {};
    }
    if (image.width == 0 || image.height == 0 || image.levels.empty() || (image.faceCount != 1 && image.faceCount != 6))
    {
        SetError(error, "invalid extent, level count or face count");
        return {};
    }
    const uint32_t levelCount = static_cast<uint32_t>(image.levels.size());
    for (uint32_t level = 0; level < levelCount; ++level)
    {
        if (image.levels[level].size() != ExpectedLevelSize(*format, image.width, image.height, image.faceCount, level))
        {
            SetError(error, "level data size does not match the extent");
            return {};
        }
    }

    const std::vector<uint8_t> dfd = BuildDataFormatDescriptor(*format);
    const std::vector<uint8_t> kvd = BuildKeyValueData();

    const size_t levelIndexOffset = kHeaderSize;
    const size_t dfdOffset = levelIndexOffset + kLevelIndexEntrySize * levelCount;
    const size_t kvdOffset = dfdOffset + dfd.size();
    const size_t levelAlignment = std::lcm<size_t>(format->bytesPerTexel, 4);

    // Mip data is stored from the smallest level to the largest.
    std::vector<size_t> levelOffsets(levelCount);
    size_t cursor = kvdOffset + kvd.size();
    for (uint32_t level = levelCount; level > 0; --level)
    {
        cursor = AlignUp(cursor, levelAlignment);
        levelOffsets[level - 1] = cursor;
        cursor += image.levels[level - 1].size();
    }

    std::vector<uint8_t> out(cursor, 0);
    std::copy(kIdentifier.begin(), kIdentifier.end(), out.begin());
    WriteU32(out, 12, image.vkFormat);
    WriteU32(out, 16, format->typeSize);
    WriteU32(out, 20, image.width);
    WriteU32(out, 24, image.height);
    WriteU32(out, 28, 0); // pixelDepth
    WriteU32(out, 32, 0); // layerCount
    WriteU32(out, 36, image.faceCount);
    WriteU32(out, 40, levelCount);
    WriteU32(out, 44, 0); // supercompressionScheme
    WriteU32(out, 48, static_cast<uint32_t>(dfdOffset));
    WriteU32(out, 52, static_cast<uint32_t>(dfd.size()));
    WriteU32(out, 56, static_cast<uint32_t>(kvdOffset));
    WriteU32(out, 60, static_cast<uint32_t>(kvd.size()));
    WriteU64(out, 64, 0); // sgdByteOffset
    WriteU64(out, 72, 0); // sgdByteLength

    for (uint32_t level = 0; level < levelCount; ++level)
    {
        const size_t entry = levelIndexOffset + kLevelIndexEntrySize * level;
        const uint64_t size = image.levels[level].size();
        WriteU64(out, entry, levelOffsets[level]);
        WriteU64(out, entry + 8, size);
        WriteU64(out, entry + 16, size);
        std::copy(image.levels[level].begin(), image.levels[level].end(), out.begin() + static_cast<ptrdiff_t>(levelOffsets[level]));
    }
    std::copy(dfd.begin(), dfd.end(), out.begin() + static_cast<ptrdiff_t>(dfdOffset));
    std::copy(kvd.begin(), kvd.end(), out.begin() + static_cast<ptrdiff_t>(kvdOffset));
    return out;
}

std::optional<Ktx2Image> DecodeKtx2(std::span<const uint8_t> bytes, std::string* error)
{
    if (bytes.size() < kHeaderSize || !std::equal(kIdentifier.begin(), kIdentifier.end(), bytes.begin()))
    {
        SetError(error, "not a KTX2 file");
        return std::nullopt;
    }

    Ktx2Image image;
    image.vkFormat = ReadU32(bytes, 12);
    image.width = ReadU32(bytes, 20);
    image.height = ReadU32(bytes, 24);
    const uint32_t pixelDepth = ReadU32(bytes, 28);
    const uint32_t layerCount = ReadU32(bytes, 32);
    image.faceCount = ReadU32(bytes, 36);
    const uint32_t levelCount = std::max(ReadU32(bytes, 40), 1u);
    const uint32_t supercompression = ReadU32(bytes, 44);

    const FormatInfo* format = FindFormat(image.vkFormat);
    if (format == nullptr)
    {
        SetError(error, "unsupported vkFormat");
        return std::nullopt;
    }
    if (supercompression != 0)
    {
        SetError(error, "supercompressed KTX2 is not supported");
        return std::nullopt;
    }
    if (pixelDepth > 1 || layerCount > 1 || (image.faceCount != 1 && image.faceCount != 6) || image.width == 0 || image.height == 0)
    {
        SetError(error, "only 2D and cube KTX2 without array layers are supported");
        return std::nullopt;
    }
    if (bytes.size() < kHeaderSize + kLevelIndexEntrySize * static_cast<size_t>(levelCount))
    {
        SetError(error, "truncated level index");
        return std::nullopt;
    }

    // DFD, KVD and SGD are not needed for uncompressed data: the vkFormat fully describes the texels.
    image.levels.resize(levelCount);
    for (uint32_t level = 0; level < levelCount; ++level)
    {
        const size_t entry = kHeaderSize + kLevelIndexEntrySize * level;
        const uint64_t offset = ReadU64(bytes, entry);
        const uint64_t size = ReadU64(bytes, entry + 8);
        if (size != ExpectedLevelSize(*format, image.width, image.height, image.faceCount, level) || offset > bytes.size()
            || size > bytes.size() - offset)
        {
            SetError(error, "level data out of range or of unexpected size");
            return std::nullopt;
        }
        image.levels[level].assign(bytes.begin() + static_cast<ptrdiff_t>(offset), bytes.begin() + static_cast<ptrdiff_t>(offset + size));
    }
    return image;
}

} // namespace mengine::asset
