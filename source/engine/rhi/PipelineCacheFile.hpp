#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mengine::rhi {

// On-disk format of the pipeline cache, all little endian:
//   offset  0  char[4]   magic "MEPC"
//   offset  4  uint32    format version (kPipelineCacheFileVersion)
//   offset  8  uint32    VkPhysicalDeviceProperties::vendorID
//   offset 12  uint32    VkPhysicalDeviceProperties::deviceID
//   offset 16  uint32    VkPhysicalDeviceProperties::driverVersion
//   offset 20  uint32    byte count of the driver data that follows
//   offset 24  uint64    FNV-1a 64 of the driver data
//   offset 32  ...       vkGetPipelineCacheData output
// Some mobile drivers crash on stale or corrupt cache data instead of ignoring it, so everything is checked here
// before the data reaches the driver, including the driver's own header (VkPipelineCacheHeaderVersionOne).

// The validated driver data, or empty (with a log line saying why) when the file is missing or unusable.
std::vector<uint8_t> LoadPipelineCacheBlob(const std::string& path, const VkPhysicalDeviceProperties& properties);

// Prepends the header and writes the file atomically. Returns false (logged) on failure.
bool SavePipelineCacheBlob(const std::string& path, const VkPhysicalDeviceProperties& properties, std::span<const uint8_t> data);

} // namespace mengine::rhi
