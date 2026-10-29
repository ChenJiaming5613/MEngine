#include "rhi/PipelineCacheFile.hpp"

#include "core/FileSystem.hpp"
#include "core/Log.hpp"

#include <cstring>

namespace mengine::rhi {
namespace {

constexpr char kMagic[4] = {'M', 'E', 'P', 'C'};
constexpr uint32_t kPipelineCacheFileVersion = 1;
constexpr size_t kFileHeaderSize = 32;
constexpr size_t kDriverHeaderSize = 16 + VK_UUID_SIZE; // VkPipelineCacheHeaderVersionOne
constexpr size_t kMaxDataSize = size_t{64} << 20;

uint64_t Fnv1a64(std::span<const uint8_t> data)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (uint8_t byte : data)
    {
        hash ^= byte;
        hash *= 0x100000001b3ull;
    }
    return hash;
}

uint32_t ReadU32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) | (static_cast<uint32_t>(bytes[2]) << 16)
        | (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t ReadU64(const uint8_t* bytes)
{
    return static_cast<uint64_t>(ReadU32(bytes)) | (static_cast<uint64_t>(ReadU32(bytes + 4)) << 32);
}

void WriteU32(uint8_t* bytes, uint32_t value)
{
    for (int index = 0; index < 4; ++index)
    {
        bytes[index] = static_cast<uint8_t>(value >> (8 * index));
    }
}

void WriteU64(uint8_t* bytes, uint64_t value)
{
    WriteU32(bytes, static_cast<uint32_t>(value));
    WriteU32(bytes + 4, static_cast<uint32_t>(value >> 32));
}

// The spec writes VkPipelineCacheHeaderVersionOne least significant byte first, regardless of host byte order.
const char* CheckDriverHeader(std::span<const uint8_t> data, const VkPhysicalDeviceProperties& properties)
{
    if (data.size() < kDriverHeaderSize)
    {
        return "driver data shorter than its header";
    }
    const uint32_t headerSize = ReadU32(data.data());
    if (headerSize < kDriverHeaderSize || headerSize > data.size())
    {
        return "bad driver header size";
    }
    if (ReadU32(data.data() + 4) != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
    {
        return "unknown driver header version";
    }
    if (ReadU32(data.data() + 8) != properties.vendorID || ReadU32(data.data() + 12) != properties.deviceID)
    {
        return "driver header names another GPU";
    }
    if (std::memcmp(data.data() + 16, properties.pipelineCacheUUID, VK_UUID_SIZE) != 0)
    {
        return "pipelineCacheUUID changed (driver update)";
    }
    return nullptr;
}

} // namespace

std::vector<uint8_t> LoadPipelineCacheBlob(const std::string& path, const VkPhysicalDeviceProperties& properties)
{
    if (!FileExists(path))
    {
        MENGINE_LOG_INFO("Pipeline cache: no file at %s, starting empty", path.c_str());
        return {};
    }
    const std::optional<std::vector<uint8_t>> file = ReadFileBytes(path);
    if (!file.has_value())
    {
        return {};
    }

    const auto reject = [&](const char* reason) {
        MENGINE_LOG_WARN("Pipeline cache: ignoring %s (%s)", path.c_str(), reason);
        return std::vector<uint8_t>{};
    };
    const std::vector<uint8_t>& bytes = *file;
    if (bytes.size() < kFileHeaderSize)
    {
        return reject("file shorter than its header");
    }
    const uint8_t* header = bytes.data();
    if (std::memcmp(header, kMagic, sizeof(kMagic)) != 0)
    {
        return reject("bad magic");
    }
    if (ReadU32(header + 4) != kPipelineCacheFileVersion)
    {
        return reject("unknown format version");
    }
    if (ReadU32(header + 8) != properties.vendorID || ReadU32(header + 12) != properties.deviceID)
    {
        return reject("written for another GPU");
    }
    if (ReadU32(header + 16) != properties.driverVersion)
    {
        return reject("written by another driver version");
    }
    const uint32_t dataSize = ReadU32(header + 20);
    if (dataSize > kMaxDataSize || dataSize != bytes.size() - kFileHeaderSize)
    {
        return reject("truncated or oversized");
    }
    const std::span<const uint8_t> data(bytes.data() + kFileHeaderSize, dataSize);
    if (Fnv1a64(data) != ReadU64(header + 24))
    {
        return reject("checksum mismatch");
    }
    if (const char* reason = CheckDriverHeader(data, properties))
    {
        return reject(reason);
    }

    MENGINE_LOG_INFO("Pipeline cache: loaded %u bytes from %s", dataSize, path.c_str());
    return std::vector<uint8_t>(data.begin(), data.end());
}

bool SavePipelineCacheBlob(const std::string& path, const VkPhysicalDeviceProperties& properties, std::span<const uint8_t> data)
{
    if (data.size() > kMaxDataSize)
    {
        MENGINE_LOG_WARN("Pipeline cache: %zu bytes exceed the %zu byte limit, not saved", data.size(), kMaxDataSize);
        return false;
    }
    std::vector<uint8_t> file(kFileHeaderSize + data.size());
    uint8_t* header = file.data();
    std::memcpy(header, kMagic, sizeof(kMagic));
    WriteU32(header + 4, kPipelineCacheFileVersion);
    WriteU32(header + 8, properties.vendorID);
    WriteU32(header + 12, properties.deviceID);
    WriteU32(header + 16, properties.driverVersion);
    WriteU32(header + 20, static_cast<uint32_t>(data.size()));
    WriteU64(header + 24, Fnv1a64(data));
    if (!data.empty())
    {
        std::memcpy(file.data() + kFileHeaderSize, data.data(), data.size());
    }
    return WriteFileAtomic(path, file);
}

} // namespace mengine::rhi
