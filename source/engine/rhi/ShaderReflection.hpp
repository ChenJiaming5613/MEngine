#pragma once

#include "rhi/GpuResources.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mengine::rhi {

// Runtime reflection of the shipped SPIR-V through Khronos SPIRV-Reflect (the library stays private to the .cpp).
// Reflection reports what the entry point statically uses, which matches slangc -O2 dropping unused resources: different
// variants of one shader may therefore see different subsets of a shared layout.

struct ReflectedBinding
{
    uint32_t set = 0;
    uint32_t binding = 0;
    // SPIR-V cannot tell dynamic buffers apart: uniform buffers come back as UNIFORM_BUFFER, storage as STORAGE_BUFFER.
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_SAMPLER;
    uint32_t count = 1;
    VkShaderStageFlags stages = 0;
    // Image bindings only.
    TextureType textureType = TextureType::Texture2D;
};

struct ShaderReflection
{
    // One bit for a single module; the union of the stages after MergeReflections.
    VkShaderStageFlags stages = 0;
    // Sorted by (set, binding).
    std::vector<ReflectedBinding> bindings;
    // End of the last used push constant member, in bytes; 0 without push constants.
    uint32_t pushConstantSize = 0;
    VkShaderStageFlags pushConstantStages = 0;
    // Sorted, unique constant_id values.
    std::vector<uint32_t> specializationConstantIds;
    // Compute only.
    std::array<uint32_t, 3> localSize{};
};

// Reflects the "main" entry point (slangc always emits that name). Returns false and fills error for malformed SPIR-V,
// a missing entry point, runtime sized descriptor arrays or descriptor types the engine does not handle.
bool ReflectShader(std::span<const uint32_t> code, ShaderReflection& out, std::string& error);

// Unions the bindings of several stages per (set, binding). Returns false when two stages disagree on the type or count
// of a binding.
bool MergeReflections(std::span<const ShaderReflection> stages, ShaderReflection& merged, std::string& error);

// The layout of one set as used by the shaders, stages limited to the ones that use each binding. Uniform buffers come
// back as UNIFORM_BUFFER; use FindBinding to switch one to UNIFORM_BUFFER_DYNAMIC. Throws when the set holds more than
// kMaxDescriptorsPerSet bindings.
DescriptorSetLayoutCreation BuildDescriptorSetLayout(const ShaderReflection& merged, uint32_t setIndex, const char* name);

// Reflects and merges a whole shader state; throws std::runtime_error naming the pipeline on failure.
ShaderReflection ReflectShaderState(const ShaderStateCreation& shaders, const char* pipelineName);

const char* DescriptorTypeName(VkDescriptorType type);

} // namespace mengine::rhi
