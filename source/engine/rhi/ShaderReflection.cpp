#include "rhi/ShaderReflection.hpp"

#include <spirv_reflect.h>

#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace mengine::rhi {
namespace {

// SPIRV-Reflect mirrors the Vulkan values, so its enums convert with a cast.
static_assert(SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER == static_cast<int>(VK_DESCRIPTOR_TYPE_SAMPLER));
static_assert(SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER == static_cast<int>(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER));
static_assert(SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE == static_cast<int>(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE));
static_assert(SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE == static_cast<int>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE));
static_assert(SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER == static_cast<int>(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER));
static_assert(SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER == static_cast<int>(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER));
static_assert(SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER == static_cast<int>(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER));
static_assert(SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER == static_cast<int>(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));
static_assert(SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT == static_cast<int>(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT));
static_assert(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT == static_cast<int>(VK_SHADER_STAGE_VERTEX_BIT));
static_assert(SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT == static_cast<int>(VK_SHADER_STAGE_FRAGMENT_BIT));
static_assert(SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT == static_cast<int>(VK_SHADER_STAGE_COMPUTE_BIT));

constexpr const char* kEntryPoint = "main";

class ModuleGuard
{
public:
    ModuleGuard() = default;
    ModuleGuard(const ModuleGuard&) = delete;
    ModuleGuard& operator=(const ModuleGuard&) = delete;
    ~ModuleGuard()
    {
        if (m_created)
        {
            spvReflectDestroyShaderModule(&module);
        }
    }

    SpvReflectResult Create(std::span<const uint32_t> code)
    {
        // NO_COPY: the module reads the caller's words, which outlive the guard.
        const SpvReflectResult result =
            spvReflectCreateShaderModule2(SPV_REFLECT_MODULE_FLAG_NO_COPY, code.size_bytes(), code.data(), &module);
        m_created = result == SPV_REFLECT_RESULT_SUCCESS;
        return result;
    }

    SpvReflectShaderModule module{};

private:
    bool m_created = false;
};

bool IsSupportedDescriptorType(SpvReflectDescriptorType type)
{
    switch (type)
    {
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        return true;
    default:
        return false;
    }
}

std::string ResultMessage(const char* operation, SpvReflectResult result)
{
    return std::string(operation) + " failed (SpvReflectResult " + std::to_string(static_cast<int>(result)) + ")";
}

bool BindingLess(const ReflectedBinding& a, const ReflectedBinding& b)
{
    return std::tie(a.set, a.binding) < std::tie(b.set, b.binding);
}

} // namespace

bool ReflectShader(std::span<const uint32_t> code, ShaderReflection& out, std::string& error)
{
    out = ShaderReflection{};
    if (code.empty())
    {
        error = "empty SPIR-V";
        return false;
    }

    ModuleGuard guard;
    SpvReflectResult result = guard.Create(code);
    if (result != SPV_REFLECT_RESULT_SUCCESS)
    {
        error = ResultMessage("spvReflectCreateShaderModule2", result);
        return false;
    }
    const SpvReflectShaderModule& module = guard.module;

    const SpvReflectEntryPoint* entryPoint = spvReflectGetEntryPoint(&module, kEntryPoint);
    if (entryPoint == nullptr)
    {
        error = "no entry point named \"main\"";
        return false;
    }
    out.stages = static_cast<VkShaderStageFlags>(entryPoint->shader_stage);
    out.localSize = {entryPoint->local_size.x, entryPoint->local_size.y, entryPoint->local_size.z};

    uint32_t count = 0;
    result = spvReflectEnumerateEntryPointDescriptorBindings(&module, kEntryPoint, &count, nullptr);
    if (result != SPV_REFLECT_RESULT_SUCCESS)
    {
        error = ResultMessage("spvReflectEnumerateEntryPointDescriptorBindings", result);
        return false;
    }
    std::vector<SpvReflectDescriptorBinding*> bindings(count);
    if (count > 0)
    {
        result = spvReflectEnumerateEntryPointDescriptorBindings(&module, kEntryPoint, &count, bindings.data());
        if (result != SPV_REFLECT_RESULT_SUCCESS)
        {
            error = ResultMessage("spvReflectEnumerateEntryPointDescriptorBindings", result);
            return false;
        }
    }
    for (const SpvReflectDescriptorBinding* binding : bindings)
    {
        const std::string where = "set " + std::to_string(binding->set) + " binding " + std::to_string(binding->binding) +
            " (" + (binding->name != nullptr ? binding->name : "") + ")";
        if (!IsSupportedDescriptorType(binding->descriptor_type))
        {
            error = where + ": unsupported descriptor type " + std::to_string(static_cast<int>(binding->descriptor_type));
            return false;
        }
        // Runtime sized arrays (bindless) report a count of 0; they need descriptor indexing.
        if (binding->count == 0)
        {
            error = where + ": runtime sized descriptor arrays are not supported";
            return false;
        }
        out.bindings.push_back(ReflectedBinding{
            .set = binding->set,
            .binding = binding->binding,
            .type = static_cast<VkDescriptorType>(binding->descriptor_type),
            .count = binding->count,
            .stages = out.stages,
            .textureType = binding->image.dim == SpvDimCube ? TextureType::TextureCube : TextureType::Texture2D,
        });
    }
    std::sort(out.bindings.begin(), out.bindings.end(), BindingLess);

    count = 0;
    result = spvReflectEnumerateEntryPointPushConstantBlocks(&module, kEntryPoint, &count, nullptr);
    if (result != SPV_REFLECT_RESULT_SUCCESS)
    {
        error = ResultMessage("spvReflectEnumerateEntryPointPushConstantBlocks", result);
        return false;
    }
    std::vector<SpvReflectBlockVariable*> blocks(count);
    if (count > 0)
    {
        result = spvReflectEnumerateEntryPointPushConstantBlocks(&module, kEntryPoint, &count, blocks.data());
        if (result != SPV_REFLECT_RESULT_SUCCESS)
        {
            error = ResultMessage("spvReflectEnumerateEntryPointPushConstantBlocks", result);
            return false;
        }
    }
    for (const SpvReflectBlockVariable* block : blocks)
    {
        // The block's offset is its lowest member offset, so the end comes from the members.
        uint32_t end = block->member_count == 0 ? block->offset + block->size : 0;
        for (uint32_t member = 0; member < block->member_count; ++member)
        {
            end = std::max(end, block->members[member].offset + block->members[member].size);
        }
        out.pushConstantSize = std::max(out.pushConstantSize, end);
    }
    if (out.pushConstantSize > 0)
    {
        out.pushConstantStages = out.stages;
    }

    count = 0;
    result = spvReflectEnumerateSpecializationConstants(&module, &count, nullptr);
    if (result != SPV_REFLECT_RESULT_SUCCESS)
    {
        error = ResultMessage("spvReflectEnumerateSpecializationConstants", result);
        return false;
    }
    std::vector<SpvReflectSpecializationConstant*> constants(count);
    if (count > 0)
    {
        result = spvReflectEnumerateSpecializationConstants(&module, &count, constants.data());
        if (result != SPV_REFLECT_RESULT_SUCCESS)
        {
            error = ResultMessage("spvReflectEnumerateSpecializationConstants", result);
            return false;
        }
    }
    for (const SpvReflectSpecializationConstant* constant : constants)
    {
        out.specializationConstantIds.push_back(constant->constant_id);
    }
    std::sort(out.specializationConstantIds.begin(), out.specializationConstantIds.end());
    out.specializationConstantIds.erase(
        std::unique(out.specializationConstantIds.begin(), out.specializationConstantIds.end()), out.specializationConstantIds.end());
    return true;
}

bool MergeReflections(std::span<const ShaderReflection> stages, ShaderReflection& merged, std::string& error)
{
    merged = ShaderReflection{};
    for (const ShaderReflection& stage : stages)
    {
        merged.stages |= stage.stages;
        if (stage.pushConstantSize > 0)
        {
            merged.pushConstantSize = std::max(merged.pushConstantSize, stage.pushConstantSize);
            merged.pushConstantStages |= stage.pushConstantStages;
        }
        if ((stage.stages & VK_SHADER_STAGE_COMPUTE_BIT) != 0)
        {
            merged.localSize = stage.localSize;
        }
        merged.specializationConstantIds.insert(
            merged.specializationConstantIds.end(), stage.specializationConstantIds.begin(), stage.specializationConstantIds.end());

        for (const ReflectedBinding& binding : stage.bindings)
        {
            const auto existing = std::find_if(merged.bindings.begin(), merged.bindings.end(),
                [&](const ReflectedBinding& other) { return other.set == binding.set && other.binding == binding.binding; });
            if (existing == merged.bindings.end())
            {
                merged.bindings.push_back(binding);
                continue;
            }
            if (existing->type != binding.type || existing->count != binding.count)
            {
                error = "set " + std::to_string(binding.set) + " binding " + std::to_string(binding.binding) +
                    ": stages disagree (" + DescriptorTypeName(existing->type) + " x" + std::to_string(existing->count) +
                    " vs " + DescriptorTypeName(binding.type) + " x" + std::to_string(binding.count) + ")";
                return false;
            }
            existing->stages |= binding.stages;
        }
    }
    std::sort(merged.bindings.begin(), merged.bindings.end(), BindingLess);
    std::sort(merged.specializationConstantIds.begin(), merged.specializationConstantIds.end());
    merged.specializationConstantIds.erase(
        std::unique(merged.specializationConstantIds.begin(), merged.specializationConstantIds.end()),
        merged.specializationConstantIds.end());
    return true;
}

DescriptorSetLayoutCreation BuildDescriptorSetLayout(const ShaderReflection& merged, uint32_t setIndex, const char* name)
{
    DescriptorSetLayoutCreation creation;
    creation.setIndex = setIndex;
    creation.name = name;
    for (const ReflectedBinding& binding : merged.bindings)
    {
        if (binding.set != setIndex)
        {
            continue;
        }
        if (creation.bindingCount >= kMaxDescriptorsPerSet)
        {
            throw std::runtime_error(std::string("BuildDescriptorSetLayout: more than ") + std::to_string(kMaxDescriptorsPerSet) +
                " bindings in set " + std::to_string(setIndex) + " of " + (name != nullptr ? name : "<unnamed>"));
        }
        creation.AddBinding({.type = binding.type, .index = binding.binding, .count = binding.count, .stages = binding.stages,
            .textureType = binding.textureType});
    }
    return creation;
}

ShaderReflection ReflectShaderState(const ShaderStateCreation& shaders, const char* pipelineName)
{
    const std::string name = pipelineName != nullptr ? pipelineName : "<unnamed>";
    std::array<ShaderReflection, kMaxShaderStages> stages{};
    std::string error;
    for (uint32_t index = 0; index < shaders.stageCount; ++index)
    {
        const ShaderStage& stage = shaders.stages[index];
        if (stage.code == nullptr || stage.codeSize % sizeof(uint32_t) != 0)
        {
            throw std::runtime_error("Shader reflection of " + name + ": stage " + std::to_string(index) + " has no SPIR-V words");
        }
        if (!ReflectShader({stage.code, stage.codeSize / sizeof(uint32_t)}, stages[index], error))
        {
            throw std::runtime_error("Shader reflection of " + name + ", stage " + std::to_string(index) + ": " + error);
        }
    }
    ShaderReflection merged;
    if (!MergeReflections(std::span(stages.data(), shaders.stageCount), merged, error))
    {
        throw std::runtime_error("Shader reflection of " + name + ": " + error);
    }
    return merged;
}

const char* DescriptorTypeName(VkDescriptorType type)
{
    switch (type)
    {
    case VK_DESCRIPTOR_TYPE_SAMPLER: return "SAMPLER";
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return "COMBINED_IMAGE_SAMPLER";
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return "SAMPLED_IMAGE";
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return "STORAGE_IMAGE";
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: return "UNIFORM_TEXEL_BUFFER";
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: return "STORAGE_TEXEL_BUFFER";
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return "UNIFORM_BUFFER";
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return "STORAGE_BUFFER";
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: return "UNIFORM_BUFFER_DYNAMIC";
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: return "STORAGE_BUFFER_DYNAMIC";
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: return "INPUT_ATTACHMENT";
    default: return "UNKNOWN";
    }
}

} // namespace mengine::rhi
