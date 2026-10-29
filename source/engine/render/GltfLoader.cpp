#include "render/GltfLoader.hpp"

#include "asset/Bc7Decoder.hpp"
#include "asset/ImageLoader.hpp"
#include "asset/Ktx2.hpp"
#include "core/FileSystem.hpp"
#include "core/Log.hpp"
#include "render/DdsFile.hpp"
#include "rhi/GpuDevice.hpp"

#include <tiny_gltf_v3.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <future>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace mengine::render {
namespace {

constexpr float kMaxAnisotropy = 8.0f;
constexpr uint32_t kMaxNodeDepth = 256;

class GltfError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

std::string_view ToView(tg3_str value)
{
    return value.data != nullptr ? std::string_view(value.data, value.len) : std::string_view();
}

std::string Describe(tg3_str name, std::string_view kind, uint32_t index)
{
    std::string result = std::string(kind) + " " + std::to_string(index);
    if (!ToView(name).empty())
    {
        result += " '" + std::string(ToView(name)) + "'";
    }
    return result;
}

// tinygltf keeps unknown extensions as generic JSON values.
const tg3_value* FindExtension(const tg3_extras_ext& ext, std::string_view name)
{
    for (uint32_t i = 0; i < ext.extensions_count; ++i)
    {
        if (ToView(ext.extensions[i].name) == name)
        {
            return &ext.extensions[i].value;
        }
    }
    return nullptr;
}

const tg3_value* FindMember(const tg3_value* object, std::string_view key)
{
    if (object == nullptr || object->type != TG3_VALUE_OBJECT)
    {
        return nullptr;
    }
    for (uint32_t i = 0; i < object->object_count; ++i)
    {
        if (ToView(object->object_data[i].key) == key)
        {
            return &object->object_data[i].value;
        }
    }
    return nullptr;
}

std::optional<double> AsNumber(const tg3_value* value)
{
    if (value == nullptr)
    {
        return std::nullopt;
    }
    if (value->type == TG3_VALUE_INT)
    {
        return static_cast<double>(value->int_val);
    }
    if (value->type == TG3_VALUE_REAL)
    {
        return value->real_val;
    }
    return std::nullopt;
}

float ReadFloat(const tg3_value* value, float fallback)
{
    const std::optional<double> number = AsNumber(value);
    return number ? static_cast<float>(*number) : fallback;
}

// Fills the first count components of *out from a numeric array of exactly count elements; leaves it otherwise.
void ReadNumberArray(const tg3_value* value, uint32_t count, float* out)
{
    if (value == nullptr || value->type != TG3_VALUE_ARRAY || value->array_count != count)
    {
        return;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        if (const std::optional<double> number = AsNumber(&value->array_data[i]))
        {
            out[i] = static_cast<float>(*number);
        }
    }
}

struct TextureReference
{
    int32_t index = -1;
    int32_t texCoord = 0;
};

// A glTF textureInfo object inside an extension.
TextureReference ReadTextureReference(const tg3_value* value)
{
    TextureReference reference;
    if (const std::optional<double> index = AsNumber(FindMember(value, "index")))
    {
        reference.index = static_cast<int32_t>(*index);
    }
    if (const std::optional<double> texCoord = AsNumber(FindMember(value, "texCoord")))
    {
        reference.texCoord = static_cast<int32_t>(*texCoord);
    }
    return reference;
}

constexpr std::string_view kExtensionSpecularGlossiness = "KHR_materials_pbrSpecularGlossiness";
constexpr std::string_view kExtensionTransmission = "KHR_materials_transmission";
constexpr std::string_view kExtensionTextureDds = "MSFT_texture_dds";

// Software decoded BC7 (devices without textureCompressionBC, i.e. most Android GPUs) starts at the first mip level
// that fits this extent: decoding is slow and RGBA8 is four times larger than BC7.
constexpr uint32_t kMaxDecodedBcExtent = 512;

int HexValue(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

// glTF URIs are RFC 3986 references; file names with spaces arrive as %20.
std::string PercentDecode(std::string_view uri)
{
    std::string result;
    result.reserve(uri.size());
    for (size_t i = 0; i < uri.size(); ++i)
    {
        if (uri[i] == '%' && i + 2 < uri.size() && HexValue(uri[i + 1]) >= 0 && HexValue(uri[i + 2]) >= 0)
        {
            result.push_back(static_cast<char>(HexValue(uri[i + 1]) * 16 + HexValue(uri[i + 2])));
            i += 2;
        }
        else
        {
            result.push_back(uri[i]);
        }
    }
    return result;
}

std::optional<std::vector<uint8_t>> DecodeBase64(std::string_view text)
{
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z')
        {
            return c - 'A';
        }
        if (c >= 'a' && c <= 'z')
        {
            return c - 'a' + 26;
        }
        if (c >= '0' && c <= '9')
        {
            return c - '0' + 52;
        }
        if (c == '+' || c == '-')
        {
            return 62;
        }
        if (c == '/' || c == '_')
        {
            return 63;
        }
        return -1;
    };

    std::vector<uint8_t> bytes;
    bytes.reserve(text.size() / 4 * 3);
    uint32_t accumulator = 0;
    int bits = 0;
    for (char c : text)
    {
        if (c == '=')
        {
            break;
        }
        const int v = value(c);
        if (v < 0)
        {
            return std::nullopt;
        }
        accumulator = (accumulator << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            bytes.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFF));
        }
    }
    return bytes;
}

// tinygltf v3 reads external buffers through these callbacks; they go through the asset file system so .gltf files
// work from the APK on Android.
int32_t ReadFileCallback(uint8_t** outData, uint64_t* outSize, const char* path, uint32_t pathLength, void* /*userData*/)
{
    const std::optional<std::vector<uint8_t>> bytes = ReadAssetFile(PercentDecode(std::string_view(path, pathLength)));
    if (!bytes)
    {
        return 0;
    }
    auto* data = static_cast<uint8_t*>(std::malloc(std::max<size_t>(bytes->size(), 1)));
    if (data == nullptr)
    {
        return 0;
    }
    if (!bytes->empty())
    {
        std::memcpy(data, bytes->data(), bytes->size());
    }
    *outData = data;
    *outSize = bytes->size();
    return 1;
}

void FreeFileCallback(uint8_t* data, uint64_t /*size*/, void* /*userData*/)
{
    std::free(data);
}

struct ParsedModel
{
    tg3_model model{};
    tg3_error_stack errors{};

    ParsedModel() { tg3_error_stack_init(&errors); }
    ~ParsedModel()
    {
        tg3_model_free(&model);
        tg3_error_stack_free(&errors);
    }
    ParsedModel(const ParsedModel&) = delete;
    ParsedModel& operator=(const ParsedModel&) = delete;
};

float ReadComponent(const uint8_t* data, int32_t componentType, bool normalized)
{
    switch (componentType)
    {
    case TG3_COMPONENT_TYPE_FLOAT:
    {
        float value;
        std::memcpy(&value, data, sizeof(value));
        return value;
    }
    case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
        return normalized ? data[0] / 255.0f : static_cast<float>(data[0]);
    case TG3_COMPONENT_TYPE_BYTE:
    {
        const auto value = static_cast<int8_t>(data[0]);
        return normalized ? std::max(value / 127.0f, -1.0f) : static_cast<float>(value);
    }
    case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
    {
        uint16_t value;
        std::memcpy(&value, data, sizeof(value));
        return normalized ? value / 65535.0f : static_cast<float>(value);
    }
    case TG3_COMPONENT_TYPE_SHORT:
    {
        int16_t value;
        std::memcpy(&value, data, sizeof(value));
        return normalized ? std::max(value / 32767.0f, -1.0f) : static_cast<float>(value);
    }
    case TG3_COMPONENT_TYPE_UNSIGNED_INT:
    {
        uint32_t value;
        std::memcpy(&value, data, sizeof(value));
        return static_cast<float>(value);
    }
    default:
        throw GltfError("unsupported accessor component type " + std::to_string(componentType));
    }
}

uint32_t ReadIndexComponent(const uint8_t* data, int32_t componentType)
{
    switch (componentType)
    {
    case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
        return data[0];
    case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
    {
        uint16_t value;
        std::memcpy(&value, data, sizeof(value));
        return value;
    }
    case TG3_COMPONENT_TYPE_UNSIGNED_INT:
    {
        uint32_t value;
        std::memcpy(&value, data, sizeof(value));
        return value;
    }
    default:
        throw GltfError("unsupported index component type " + std::to_string(componentType));
    }
}

struct StreamBinding
{
    rhi::BufferHandle buffer;
    VkDeviceSize offset = 0;
};

struct IndexBinding
{
    rhi::BufferHandle buffer;
    VkDeviceSize offset = 0;
    VkIndexType type = VK_INDEX_TYPE_UINT32;
    uint32_t count = 0;
};

// A texture ready for GpuDevice::CreateTexture, produced on a worker thread.
struct PreparedTexture
{
    std::vector<uint8_t> bytes; // owns the pixel data, which starts at dataOffset
    size_t dataOffset = 0;
    size_t dataSize = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 1; // levels present in the data, or 0 with generateMips
    bool generateMips = false;
    bool decodedBc = false; // BC7 decoded on the CPU
    std::string error;      // non-empty: use a fallback texture
};

bool IsDds(std::span<const uint8_t> bytes)
{
    return bytes.size() >= 4 && std::memcmp(bytes.data(), "DDS ", 4) == 0;
}

// glTF decides the color space by usage (base color, emissive and specular-glossiness are sRGB, the rest linear),
// so the DDS format is switched to the matching variant where one exists.
VkFormat WithColorSpace(VkFormat format, bool srgb)
{
    struct Pair
    {
        VkFormat unorm;
        VkFormat srgb;
    };
    static constexpr std::array<Pair, 6> kPairs = {{
        {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB},
        {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB},
        {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_BC1_RGBA_SRGB_BLOCK},
        {VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_BC2_SRGB_BLOCK},
        {VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_BC3_SRGB_BLOCK},
        {VK_FORMAT_BC7_UNORM_BLOCK, VK_FORMAT_BC7_SRGB_BLOCK},
    }};
    for (const Pair& pair : kPairs)
    {
        if (format == pair.unorm || format == pair.srgb)
        {
            return srgb ? pair.srgb : pair.unorm;
        }
    }
    return format;
}

// DDS (MSFT_texture_dds): uploaded as stored when the device can sample the format; BC7 without textureCompressionBC
// is decoded to RGBA8 from the first mip level that fits kMaxDecodedBcExtent.
void PrepareDds(PreparedTexture& prepared, bool srgb, bool bcSupported)
{
    TextureData info;
    info.data = prepared.bytes;
    if (!LoadDDSTextureFromMemory(info))
    {
        prepared.error = "invalid or unsupported DDS file";
        return;
    }
    if (info.dimension != TextureDimension::Texture2D || info.arraySize != 1 || info.width == 0 || info.height == 0)
    {
        prepared.error = "only single 2D DDS textures are supported";
        return;
    }
    const std::vector<TextureSubresourceData>& levels = info.dataLayout[0];
    const VkFormat format = WithColorSpace(info.format, srgb);

    if (rhi::IsBlockCompressedFormat(format) && !bcSupported)
    {
        if (format != VK_FORMAT_BC7_UNORM_BLOCK && format != VK_FORMAT_BC7_SRGB_BLOCK)
        {
            prepared.error = "BC format " + std::to_string(static_cast<int>(format)) +
                             " needs textureCompressionBC (only BC7 is decoded on the CPU)";
            return;
        }
        uint32_t level = 0;
        while (level + 1 < info.mipLevels &&
               std::max(asset::MipExtent(info.width, level), asset::MipExtent(info.height, level)) > kMaxDecodedBcExtent)
        {
            ++level;
        }
        prepared.width = asset::MipExtent(info.width, level);
        prepared.height = asset::MipExtent(info.height, level);
        std::vector<uint8_t> rgba = asset::DecodeBc7(
            std::span<const uint8_t>(prepared.bytes.data() + levels[level].dataOffset, levels[level].dataSize),
            prepared.width, prepared.height);
        prepared.bytes = std::move(rgba);
        prepared.dataOffset = 0;
        prepared.dataSize = prepared.bytes.size();
        prepared.format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        prepared.mipLevels = 0;
        prepared.generateMips = true;
        prepared.decodedBc = true;
        return;
    }
    if (!rhi::IsBlockCompressedFormat(format) && rhi::FormatTexelSize(format) == 0)
    {
        prepared.error = "DDS format " + std::to_string(static_cast<int>(format)) + " is not supported";
        return;
    }

    // A single 2D slice: the levels are tightly packed one after another, as TextureCreation expects.
    prepared.format = format;
    prepared.width = info.width;
    prepared.height = info.height;
    prepared.mipLevels = info.mipLevels;
    prepared.dataOffset = static_cast<size_t>(levels.front().dataOffset);
    prepared.dataSize = static_cast<size_t>(levels.back().dataOffset - levels.front().dataOffset) + levels.back().dataSize;
}

void PrepareEncodedImage(PreparedTexture& prepared, bool srgb)
{
    std::optional<asset::Image8> image = asset::DecodeImageRgba8(prepared.bytes, &prepared.error);
    if (!image)
    {
        return;
    }
    prepared.width = image->width;
    prepared.height = image->height;
    prepared.bytes = std::move(image->rgba);
    prepared.dataOffset = 0;
    prepared.dataSize = prepared.bytes.size();
    prepared.format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    prepared.mipLevels = 0;
    prepared.generateMips = true;
}

struct TextureBinding
{
    rhi::TextureHandle texture;
    rhi::SamplerHandle sampler;
};

class GltfSceneBuilder
{
public:
    GltfSceneBuilder(rhi::GpuDevice& device, const tg3_model& model, std::string assetPath,
        rhi::DescriptorSetLayoutHandle materialLayout, Scene& scene)
        : m_device(device)
        , m_model(model)
        , m_path(std::move(assetPath))
        , m_baseDirectory(ParentPath(m_path))
        , m_materialLayout(materialLayout)
        , m_scene(scene)
    {
    }

    void Build()
    {
        WarnAboutExtensions();
        LoadTextures();
        CreateMaterials();
        CreatePrimitives();
        CreateDraws();
    }

private:
    struct AccessorLayout
    {
        const uint8_t* data = nullptr; // first element, null when the accessor has no buffer view (all zeros)
        uint64_t stride = 0;
        uint64_t elementSize = 0;
        uint32_t components = 0;
        int32_t componentSize = 0;
    };

    const uint8_t* BufferViewData(int32_t bufferViewIndex, uint64_t* length) const
    {
        if (bufferViewIndex < 0 || static_cast<uint32_t>(bufferViewIndex) >= m_model.buffer_views_count)
        {
            throw GltfError("invalid buffer view " + std::to_string(bufferViewIndex));
        }
        const tg3_buffer_view& view = m_model.buffer_views[bufferViewIndex];
        if (view.buffer < 0 || static_cast<uint32_t>(view.buffer) >= m_model.buffers_count)
        {
            throw GltfError("buffer view " + std::to_string(bufferViewIndex) + " references invalid buffer");
        }
        const tg3_buffer& buffer = m_model.buffers[view.buffer];
        if (buffer.data.data == nullptr || view.byte_offset > buffer.data.count ||
            view.byte_length > buffer.data.count - view.byte_offset)
        {
            throw GltfError("buffer view " + std::to_string(bufferViewIndex) + " exceeds buffer " +
                            std::to_string(view.buffer) + " (" + std::to_string(buffer.data.count) + " bytes loaded)");
        }
        *length = view.byte_length;
        return buffer.data.data + view.byte_offset;
    }

    const tg3_accessor& Accessor(int32_t index) const
    {
        if (index < 0 || static_cast<uint32_t>(index) >= m_model.accessors_count)
        {
            throw GltfError("invalid accessor " + std::to_string(index));
        }
        return m_model.accessors[index];
    }

    AccessorLayout Layout(int32_t accessorIndex) const
    {
        const tg3_accessor& accessor = Accessor(accessorIndex);
        AccessorLayout layout;
        layout.components = static_cast<uint32_t>(std::max(tg3_num_components(accessor.type), 0));
        layout.componentSize = tg3_component_size(accessor.component_type);
        if (layout.components == 0 || layout.componentSize <= 0)
        {
            throw GltfError("accessor " + std::to_string(accessorIndex) + " has an invalid type");
        }
        layout.elementSize = static_cast<uint64_t>(layout.components) * static_cast<uint64_t>(layout.componentSize);
        layout.stride = layout.elementSize;
        if (accessor.buffer_view < 0)
        {
            return layout;
        }

        uint64_t viewLength = 0;
        const uint8_t* viewData = BufferViewData(accessor.buffer_view, &viewLength);
        const uint32_t byteStride = m_model.buffer_views[accessor.buffer_view].byte_stride;
        if (byteStride != 0)
        {
            layout.stride = byteStride;
        }
        if (accessor.count > 0)
        {
            const uint64_t last = accessor.byte_offset + layout.stride * (accessor.count - 1) + layout.elementSize;
            if (accessor.byte_offset > viewLength || last > viewLength || last < accessor.byte_offset)
            {
                throw GltfError("accessor " + std::to_string(accessorIndex) + " exceeds its buffer view");
            }
        }
        layout.data = viewData + accessor.byte_offset;
        return layout;
    }

    std::vector<float> ReadFloats(int32_t accessorIndex, uint32_t expectedComponents) const
    {
        const tg3_accessor& accessor = Accessor(accessorIndex);
        const AccessorLayout layout = Layout(accessorIndex);
        if (layout.components != expectedComponents)
        {
            throw GltfError("accessor " + std::to_string(accessorIndex) + " has " + std::to_string(layout.components) +
                            " components, expected " + std::to_string(expectedComponents));
        }
        std::vector<float> values(accessor.count * expectedComponents, 0.0f);
        if (layout.data != nullptr)
        {
            for (uint64_t i = 0; i < accessor.count; ++i)
            {
                const uint8_t* element = layout.data + i * layout.stride;
                for (uint32_t c = 0; c < expectedComponents; ++c)
                {
                    values[i * expectedComponents + c] =
                        ReadComponent(element + c * layout.componentSize, accessor.component_type, accessor.normalized != 0);
                }
            }
        }

        if (accessor.sparse.is_sparse && accessor.sparse.count > 0)
        {
            const auto count = static_cast<uint64_t>(accessor.sparse.count);
            const tg3_accessor_sparse_indices& sparseIndices = accessor.sparse.indices;
            const tg3_accessor_sparse_values& sparseValues = accessor.sparse.values;
            const int32_t indexSize = tg3_component_size(sparseIndices.component_type);
            uint64_t indexLength = 0;
            uint64_t valueLength = 0;
            const uint8_t* indexData = BufferViewData(sparseIndices.buffer_view, &indexLength);
            const uint8_t* valueData = BufferViewData(sparseValues.buffer_view, &valueLength);
            if (indexSize <= 0 || sparseIndices.byte_offset + count * indexSize > indexLength ||
                sparseValues.byte_offset + count * layout.elementSize > valueLength)
            {
                throw GltfError("sparse accessor " + std::to_string(accessorIndex) + " exceeds its buffer views");
            }
            indexData += sparseIndices.byte_offset;
            valueData += sparseValues.byte_offset;
            for (uint64_t i = 0; i < count; ++i)
            {
                const uint32_t target = ReadIndexComponent(indexData + i * indexSize, sparseIndices.component_type);
                if (target >= accessor.count)
                {
                    throw GltfError("sparse accessor " + std::to_string(accessorIndex) + " index out of range");
                }
                for (uint32_t c = 0; c < expectedComponents; ++c)
                {
                    values[target * expectedComponents + c] = ReadComponent(valueData + i * layout.elementSize +
                                                                                c * layout.componentSize,
                        accessor.component_type, accessor.normalized != 0);
                }
            }
        }
        return values;
    }

    std::vector<uint32_t> ReadIndices(int32_t accessorIndex) const
    {
        const tg3_accessor& accessor = Accessor(accessorIndex);
        if (accessor.type != TG3_TYPE_SCALAR || accessor.sparse.is_sparse)
        {
            // Sparse index accessors are legal but exotic; read them through the float path (exact up to 2^24).
            const std::vector<float> values = ReadFloats(accessorIndex, 1);
            std::vector<uint32_t> indices(values.size());
            for (size_t i = 0; i < values.size(); ++i)
            {
                indices[i] = static_cast<uint32_t>(values[i]);
            }
            return indices;
        }
        const AccessorLayout layout = Layout(accessorIndex);
        std::vector<uint32_t> indices(accessor.count, 0);
        if (layout.data != nullptr)
        {
            for (uint64_t i = 0; i < accessor.count; ++i)
            {
                indices[i] = ReadIndexComponent(layout.data + i * layout.stride, accessor.component_type);
            }
        }
        return indices;
    }

    rhi::BufferHandle CreateBuffer(VkBufferUsageFlags usage, const void* data, VkDeviceSize size, const std::string& name)
    {
        rhi::BufferCreation creation;
        creation.usage = usage;
        creation.size = size;
        creation.initialData = data;
        creation.name = name.c_str();
        const rhi::BufferHandle buffer = m_device.CreateBuffer(creation);
        m_scene.buffers.push_back(buffer);
        return buffer;
    }

    // A glTF buffer view becomes one GPU buffer, shared by every tightly packed accessor inside it.
    rhi::BufferHandle BufferViewBuffer(int32_t bufferViewIndex)
    {
        const auto it = m_bufferViewBuffers.find(bufferViewIndex);
        if (it != m_bufferViewBuffers.end())
        {
            return it->second;
        }
        uint64_t length = 0;
        const uint8_t* data = BufferViewData(bufferViewIndex, &length);
        const rhi::BufferHandle buffer = CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            data, length, "glTF bufferView " + std::to_string(bufferViewIndex));
        m_bufferViewBuffers.emplace(bufferViewIndex, buffer);
        return buffer;
    }

    StreamBinding AcquireFloatStream(int32_t accessorIndex, uint32_t components, uint64_t vertexCount, const char* name)
    {
        const tg3_accessor& accessor = Accessor(accessorIndex);
        if (accessor.count != vertexCount)
        {
            throw GltfError(std::string(name) + " accessor has " + std::to_string(accessor.count) + " elements, POSITION has " +
                            std::to_string(vertexCount));
        }
        const AccessorLayout layout = Layout(accessorIndex);
        if (layout.components != components)
        {
            throw GltfError(std::string(name) + " accessor has an unexpected type");
        }
        if (accessor.component_type == TG3_COMPONENT_TYPE_FLOAT && !accessor.sparse.is_sparse && layout.data != nullptr &&
            layout.stride == layout.elementSize && accessor.byte_offset % 4 == 0)
        {
            return {BufferViewBuffer(accessor.buffer_view), accessor.byte_offset};
        }
        const std::vector<float> values = ReadFloats(accessorIndex, components);
        return {CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, values.data(), values.size() * sizeof(float),
                    std::string("glTF ") + name),
            0};
    }

    StreamBinding CreateFloatStream(const std::vector<float>& values, const char* name)
    {
        return {CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, values.data(), values.size() * sizeof(float),
                    std::string("glTF ") + name),
            0};
    }

    IndexBinding CreateIndexStream(const std::vector<uint32_t>& indices)
    {
        return {CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data(), indices.size() * sizeof(uint32_t), "glTF indices"),
            0, VK_INDEX_TYPE_UINT32, static_cast<uint32_t>(indices.size())};
    }

    IndexBinding AcquireIndexStream(int32_t accessorIndex, uint64_t vertexCount)
    {
        const tg3_accessor& accessor = Accessor(accessorIndex);
        std::vector<uint32_t> indices = ReadIndices(accessorIndex);
        for (uint32_t index : indices)
        {
            if (index >= vertexCount)
            {
                throw GltfError("index " + std::to_string(index) + " exceeds vertex count " + std::to_string(vertexCount));
            }
        }

        const AccessorLayout layout = Layout(accessorIndex);
        const bool direct = !accessor.sparse.is_sparse && layout.data != nullptr && accessor.type == TG3_TYPE_SCALAR &&
                            layout.stride == layout.elementSize &&
                            (accessor.component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT ||
                                accessor.component_type == TG3_COMPONENT_TYPE_UNSIGNED_INT);
        if (direct && accessor.byte_offset % layout.componentSize == 0)
        {
            return {BufferViewBuffer(accessor.buffer_view), accessor.byte_offset,
                accessor.component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32,
                static_cast<uint32_t>(accessor.count)};
        }
        if (accessor.component_type == TG3_COMPONENT_TYPE_UNSIGNED_BYTE)
        {
            // Vulkan 1.1 has no 8 bit indices without an extension.
            const std::vector<uint16_t> converted(indices.begin(), indices.end());
            return {CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, converted.data(), converted.size() * sizeof(uint16_t),
                        "glTF indices"),
                0, VK_INDEX_TYPE_UINT16, static_cast<uint32_t>(converted.size())};
        }
        return CreateIndexStream(indices);
    }

    std::optional<std::vector<uint8_t>> ReadImageBytes(uint32_t imageIndex, std::string* error) const
    {
        const tg3_image& image = m_model.images[imageIndex];
        if (image.buffer_view >= 0)
        {
            uint64_t length = 0;
            const uint8_t* data = BufferViewData(image.buffer_view, &length);
            return std::vector<uint8_t>(data, data + length);
        }

        const std::string_view uri = ToView(image.uri);
        if (uri.empty())
        {
            *error = "image has neither uri nor bufferView";
            return std::nullopt;
        }
        if (uri.substr(0, 5) == "data:")
        {
            const size_t comma = uri.find(',');
            if (comma == std::string_view::npos || uri.substr(0, comma).find(";base64") == std::string_view::npos)
            {
                *error = "unsupported data URI (only base64 is supported)";
                return std::nullopt;
            }
            std::optional<std::vector<uint8_t>> bytes = DecodeBase64(uri.substr(comma + 1));
            if (!bytes)
            {
                *error = "invalid base64 data URI";
            }
            return bytes;
        }

        const std::string relative = PercentDecode(uri);
        if (!IsSafeRelativePath(relative))
        {
            *error = "image uri '" + relative + "' leaves the asset directory";
            return std::nullopt;
        }
        std::optional<std::vector<uint8_t>> bytes = ReadAssetFile(JoinPath(m_baseDirectory, relative));
        if (!bytes)
        {
            *error = "cannot read '" + JoinPath(m_baseDirectory, relative) + "'";
        }
        return bytes;
    }

    // Collects every (image, color space) pair referenced by a material, prepares them on worker threads (a bounded
    // window keeps memory in check) and records the uploads into the device's upload batch.
    void LoadTextures()
    {
        auto request = [&](const tg3_texture_info& info, bool srgb) { RequestTexture(info.index, srgb); };
        for (uint32_t i = 0; i < m_model.materials_count; ++i)
        {
            const tg3_material& material = m_model.materials[i];
            request(material.pbr_metallic_roughness.base_color_texture, true);
            request(material.pbr_metallic_roughness.metallic_roughness_texture, false);
            RequestTexture(material.normal_texture.index, false);
            RequestTexture(material.occlusion_texture.index, false);
            request(material.emissive_texture, true);
            if (const tg3_value* specularGlossiness = FindExtension(material.ext, kExtensionSpecularGlossiness))
            {
                // The specular color is sRGB like the diffuse texture; glossiness in alpha stays linear either way.
                RequestTexture(ReadTextureReference(FindMember(specularGlossiness, "diffuseTexture")).index, true);
                RequestTexture(ReadTextureReference(FindMember(specularGlossiness, "specularGlossinessTexture")).index, true);
            }
        }
        if (m_imageRequests.empty())
        {
            return;
        }

        const bool bcSupported = m_device.SupportsTextureCompressionBc();
        const size_t window = std::max<size_t>(2, std::thread::hardware_concurrency());
        std::deque<std::future<PreparedTexture>> pending;
        size_t launched = 0;
        auto launch = [&]() {
            const auto [imageIndex, srgb] = m_imageRequests[launched++];
            PreparedTexture prepared;
            std::optional<std::vector<uint8_t>> bytes;
            try
            {
                bytes = ReadImageBytes(imageIndex, &prepared.error);
            }
            catch (const std::exception& exception)
            {
                prepared.error = exception.what();
            }
            if (bytes)
            {
                prepared.bytes = std::move(*bytes);
            }
            const bool hasBytes = bytes.has_value();
            pending.push_back(std::async(std::launch::async,
                [prepared = std::move(prepared), hasBytes, srgb, bcSupported]() mutable {
                    if (hasBytes)
                    {
                        if (IsDds(prepared.bytes))
                        {
                            PrepareDds(prepared, srgb, bcSupported);
                        }
                        else
                        {
                            PrepareEncodedImage(prepared, srgb);
                        }
                    }
                    return std::move(prepared);
                }));
        };

        size_t decodedBcCount = 0;
        m_imageTextures.resize(m_imageRequests.size());
        for (size_t i = 0; i < m_imageRequests.size(); ++i)
        {
            while (launched < m_imageRequests.size() && launched < i + window)
            {
                launch();
            }
            PreparedTexture prepared = pending.front().get();
            pending.pop_front();

            const uint32_t imageIndex = m_imageRequests[i].first;
            const std::string label = Describe(m_model.images[imageIndex].name, "image", imageIndex);
            if (prepared.format == VK_FORMAT_UNDEFINED)
            {
                MENGINE_LOG_WARN("%s: %s: %s, using a fallback texture", m_path.c_str(), label.c_str(),
                    prepared.error.empty() ? "no image data" : prepared.error.c_str());
                continue;
            }
            decodedBcCount += prepared.decodedBc ? 1 : 0;

            rhi::TextureCreation creation;
            creation.width = prepared.width;
            creation.height = prepared.height;
            creation.mipLevels = prepared.mipLevels;
            creation.format = prepared.format;
            creation.flags = prepared.generateMips ? rhi::kTextureFlagGenerateMips : 0;
            creation.initialData = prepared.bytes.data() + prepared.dataOffset;
            creation.initialDataSize = prepared.dataSize;
            creation.name = label.c_str();
            try
            {
                m_imageTextures[i] = m_device.CreateTexture(creation);
                m_scene.textures.push_back(m_imageTextures[i]);
            }
            catch (const std::exception& exception)
            {
                MENGINE_LOG_WARN("%s: %s: %s, using a fallback texture", m_path.c_str(), label.c_str(), exception.what());
            }
        }
        if (decodedBcCount > 0)
        {
            MENGINE_LOG_INFO("%s: the device lacks textureCompressionBC, decoded %zu BC7 textures to RGBA8 (at most %ux%u)",
                m_path.c_str(), decodedBcCount, kMaxDecodedBcExtent, kMaxDecodedBcExtent);
        }
    }

    // The image a texture samples: MSFT_texture_dds's DDS source when present (Bistro ships only the DDS files),
    // otherwise texture.source. -1 when neither is valid.
    int32_t TextureImage(int32_t textureIndex) const
    {
        if (textureIndex < 0 || static_cast<uint32_t>(textureIndex) >= m_model.textures_count)
        {
            return -1;
        }
        const tg3_texture& texture = m_model.textures[textureIndex];
        auto valid = [&](int32_t image) { return image >= 0 && static_cast<uint32_t>(image) < m_model.images_count; };
        if (const std::optional<double> dds = AsNumber(FindMember(FindExtension(texture.ext, kExtensionTextureDds), "source")))
        {
            if (valid(static_cast<int32_t>(*dds)))
            {
                return static_cast<int32_t>(*dds);
            }
        }
        return valid(texture.source) ? texture.source : -1;
    }

    void RequestTexture(int32_t textureIndex, bool srgb)
    {
        const int32_t image = TextureImage(textureIndex);
        if (image < 0)
        {
            return;
        }
        const std::pair<uint32_t, bool> key{static_cast<uint32_t>(image), srgb};
        if (m_imageRequestIndex.emplace(key, m_imageRequests.size()).second)
        {
            m_imageRequests.push_back(key);
        }
    }

    rhi::SamplerHandle Sampler(int32_t samplerIndex)
    {
        if (samplerIndex < 0 || static_cast<uint32_t>(samplerIndex) >= m_model.samplers_count)
        {
            samplerIndex = -1;
        }
        const auto it = m_samplers.find(samplerIndex);
        if (it != m_samplers.end())
        {
            return it->second;
        }

        const std::string name = "glTF sampler " + std::to_string(samplerIndex);
        rhi::SamplerCreation creation;
        creation.maxAnisotropy = kMaxAnisotropy;
        creation.name = name.c_str();
        if (samplerIndex >= 0)
        {
            const tg3_sampler& sampler = m_model.samplers[samplerIndex];
            creation.magFilter = sampler.mag_filter == TG3_TEXTURE_FILTER_NEAREST ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            switch (sampler.min_filter)
            {
            case TG3_TEXTURE_FILTER_NEAREST:
                creation.minFilter = VK_FILTER_NEAREST;
                creation.maxLod = 0.0f;
                break;
            case TG3_TEXTURE_FILTER_LINEAR:
                creation.minFilter = VK_FILTER_LINEAR;
                creation.maxLod = 0.0f;
                break;
            case TG3_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
                creation.minFilter = VK_FILTER_NEAREST;
                creation.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                break;
            case TG3_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
                creation.minFilter = VK_FILTER_LINEAR;
                creation.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                break;
            case TG3_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
                creation.minFilter = VK_FILTER_NEAREST;
                creation.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                break;
            default:
                break;
            }
            auto wrap = [](int32_t mode) {
                switch (mode)
                {
                case TG3_TEXTURE_WRAP_CLAMP_TO_EDGE:
                    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                case TG3_TEXTURE_WRAP_MIRRORED_REPEAT:
                    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                default:
                    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
                }
            };
            creation.addressModeU = wrap(sampler.wrap_s);
            creation.addressModeV = wrap(sampler.wrap_t);
            if (creation.minFilter == VK_FILTER_NEAREST && creation.magFilter == VK_FILTER_NEAREST)
            {
                creation.maxAnisotropy = 1.0f;
            }
        }

        const rhi::SamplerHandle handle = m_device.CreateSampler(creation);
        m_scene.samplers.push_back(handle);
        m_samplers.emplace(samplerIndex, handle);
        return handle;
    }

    // Resolves a material texture slot; an invalid result means "use the device's dummy texture".
    TextureBinding ResolveTexture(int32_t textureIndex, int32_t texCoord, bool srgb, const std::string& materialLabel)
    {
        if (textureIndex < 0 || static_cast<uint32_t>(textureIndex) >= m_model.textures_count)
        {
            return {};
        }
        if (texCoord != 0)
        {
            MENGINE_LOG_WARN("%s: %s uses TEXCOORD_%d, only TEXCOORD_0 is supported", m_path.c_str(), materialLabel.c_str(),
                texCoord);
        }
        const tg3_texture& texture = m_model.textures[textureIndex];
        const int32_t image = TextureImage(textureIndex);
        if (image < 0)
        {
            return {};
        }
        const auto it = m_imageRequestIndex.find({static_cast<uint32_t>(image), srgb});
        if (it == m_imageRequestIndex.end() || !m_imageTextures[it->second].IsValid())
        {
            return {};
        }
        return {m_imageTextures[it->second], Sampler(texture.sampler)};
    }

    void CreateMaterials()
    {
        for (uint32_t i = 0; i < m_model.materials_count; ++i)
        {
            CreateMaterial(&m_model.materials[i], i);
        }
    }

    uint32_t DefaultMaterial()
    {
        if (!m_defaultMaterial)
        {
            m_defaultMaterial = static_cast<uint32_t>(m_scene.materials.size());
            CreateMaterial(nullptr, m_model.materials_count);
        }
        return *m_defaultMaterial;
    }

    // A null material is the glTF default material (white, fully metallic and rough).
    void CreateMaterial(const tg3_material* source, uint32_t index)
    {
        SceneMaterial material;
        material.name = source != nullptr ? Describe(source->name, "material", index) : std::string("default material");

        MaterialData data;
        TextureBinding baseColor;
        TextureBinding metallicRoughness;
        TextureBinding normal;
        TextureBinding occlusion;
        TextureBinding emissive;
        if (source != nullptr)
        {
            const tg3_pbr_metallic_roughness& pbr = source->pbr_metallic_roughness;
            data.baseColorFactor = glm::vec4(static_cast<float>(pbr.base_color_factor[0]),
                static_cast<float>(pbr.base_color_factor[1]), static_cast<float>(pbr.base_color_factor[2]),
                static_cast<float>(pbr.base_color_factor[3]));
            data.emissiveFactor = glm::vec4(static_cast<float>(source->emissive_factor[0]),
                static_cast<float>(source->emissive_factor[1]), static_cast<float>(source->emissive_factor[2]), 0.0f);
            data.metallicFactor = static_cast<float>(pbr.metallic_factor);
            data.roughnessFactor = static_cast<float>(pbr.roughness_factor);
            data.normalScale = static_cast<float>(source->normal_texture.scale);
            data.occlusionStrength = static_cast<float>(source->occlusion_texture.strength);
            data.alphaCutoff = static_cast<float>(source->alpha_cutoff);

            const std::string_view alphaMode = ToView(source->alpha_mode);
            if (alphaMode == "MASK")
            {
                material.alphaMode = AlphaMode::Mask;
            }
            else if (alphaMode == "BLEND")
            {
                material.alphaMode = AlphaMode::Blend;
                MENGINE_LOG_WARN("%s: %s uses alphaMode BLEND, rendered as MASK (cutoff %.2f)", m_path.c_str(),
                    material.name.c_str(), data.alphaCutoff);
            }
            material.doubleSided = source->double_sided != 0;

            baseColor = ResolveTexture(pbr.base_color_texture.index, pbr.base_color_texture.tex_coord, true, material.name);
            metallicRoughness = ResolveTexture(pbr.metallic_roughness_texture.index, pbr.metallic_roughness_texture.tex_coord,
                false, material.name);
            normal = ResolveTexture(source->normal_texture.index, source->normal_texture.tex_coord, false, material.name);
            occlusion = ResolveTexture(source->occlusion_texture.index, source->occlusion_texture.tex_coord, false, material.name);
            emissive = ResolveTexture(source->emissive_texture.index, source->emissive_texture.tex_coord, true, material.name);
            if (normal.texture.IsValid())
            {
                data.flags |= kMaterialFlagNormalTexture;
            }

            // The extension replaces pbrMetallicRoughness: diffuse goes to the base color slot, specular-glossiness
            // to the metallic-roughness slot (see kMaterialFlagSpecularGlossiness).
            if (const tg3_value* specularGlossiness = FindExtension(source->ext, kExtensionSpecularGlossiness))
            {
                data.flags |= kMaterialFlagSpecularGlossiness;
                data.baseColorFactor = glm::vec4(1.0f);
                ReadNumberArray(FindMember(specularGlossiness, "diffuseFactor"), 4, &data.baseColorFactor.x);
                data.specularGlossinessFactor = glm::vec4(1.0f);
                ReadNumberArray(FindMember(specularGlossiness, "specularFactor"), 3, &data.specularGlossinessFactor.x);
                data.specularGlossinessFactor.w = ReadFloat(FindMember(specularGlossiness, "glossinessFactor"), 1.0f);

                const TextureReference diffuse = ReadTextureReference(FindMember(specularGlossiness, "diffuseTexture"));
                const TextureReference specular =
                    ReadTextureReference(FindMember(specularGlossiness, "specularGlossinessTexture"));
                baseColor = ResolveTexture(diffuse.index, diffuse.texCoord, true, material.name);
                metallicRoughness = ResolveTexture(specular.index, specular.texCoord, true, material.name);
            }
            if (const tg3_value* transmission = FindExtension(source->ext, kExtensionTransmission))
            {
                material.transmissive = ReadFloat(FindMember(transmission, "transmissionFactor"), 0.0f) > 0.0f;
            }
        }

        rhi::BufferCreation bufferCreation;
        bufferCreation.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferCreation.size = sizeof(MaterialData);
        bufferCreation.initialData = &data;
        bufferCreation.name = material.name.c_str();
        material.uniformBuffer = m_device.CreateBuffer(bufferCreation);

        // Missing textures bind the device's 1x1 white texture: it leaves base color, metallic-roughness, occlusion
        // and emissive equal to their factors; the normal map is skipped through kMaterialFlagNormalTexture.
        rhi::DescriptorSetCreation setCreation;
        setCreation.layout = m_materialLayout;
        setCreation.name = material.name.c_str();
        setCreation.AddBuffer(material.uniformBuffer, kMaterialBindingData, sizeof(MaterialData))
            .AddTexture(baseColor.texture, kMaterialBindingBaseColor, baseColor.sampler)
            .AddTexture(metallicRoughness.texture, kMaterialBindingMetallicRoughness, metallicRoughness.sampler)
            .AddTexture(normal.texture, kMaterialBindingNormal, normal.sampler)
            .AddTexture(occlusion.texture, kMaterialBindingOcclusion, occlusion.sampler)
            .AddTexture(emissive.texture, kMaterialBindingEmissive, emissive.sampler);
        material.descriptorSet = m_device.CreateDescriptorSet(setCreation);
        m_scene.materials.push_back(std::move(material));
    }

    static int32_t FindAttribute(const tg3_primitive& primitive, std::string_view name)
    {
        for (uint32_t i = 0; i < primitive.attributes_count; ++i)
        {
            if (ToView(primitive.attributes[i].key) == name)
            {
                return primitive.attributes[i].value;
            }
        }
        return -1;
    }

    void ComputeBounds(int32_t positionAccessor, const std::vector<float>* positions, ScenePrimitive& primitive) const
    {
        const tg3_accessor& accessor = Accessor(positionAccessor);
        if (accessor.min_values_count == 3 && accessor.max_values_count == 3 && !accessor.sparse.is_sparse)
        {
            primitive.boundsMin = glm::vec3(static_cast<float>(accessor.min_values[0]),
                static_cast<float>(accessor.min_values[1]), static_cast<float>(accessor.min_values[2]));
            primitive.boundsMax = glm::vec3(static_cast<float>(accessor.max_values[0]),
                static_cast<float>(accessor.max_values[1]), static_cast<float>(accessor.max_values[2]));
            return;
        }
        std::vector<float> loaded;
        if (positions == nullptr)
        {
            loaded = ReadFloats(positionAccessor, 3);
            positions = &loaded;
        }
        primitive.boundsMin = glm::vec3(std::numeric_limits<float>::max());
        primitive.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
        for (size_t i = 0; i + 2 < positions->size(); i += 3)
        {
            const glm::vec3 p((*positions)[i], (*positions)[i + 1], (*positions)[i + 2]);
            primitive.boundsMin = glm::min(primitive.boundsMin, p);
            primitive.boundsMax = glm::max(primitive.boundsMax, p);
        }
    }

    void CreatePrimitives()
    {
        m_meshPrimitives.resize(m_model.meshes_count);
        for (uint32_t meshIndex = 0; meshIndex < m_model.meshes_count; ++meshIndex)
        {
            const tg3_mesh& mesh = m_model.meshes[meshIndex];
            for (uint32_t primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex)
            {
                const std::string label = Describe(mesh.name, "mesh", meshIndex) + " primitive " + std::to_string(primitiveIndex);
                try
                {
                    std::optional<ScenePrimitive> primitive = CreatePrimitive(mesh.primitives[primitiveIndex], label);
                    if (primitive)
                    {
                        m_meshPrimitives[meshIndex].push_back(static_cast<uint32_t>(m_scene.primitives.size()));
                        m_scene.primitives.push_back(*primitive);
                    }
                }
                catch (const GltfError& error)
                {
                    MENGINE_LOG_WARN("%s: %s skipped: %s", m_path.c_str(), label.c_str(), error.what());
                }
            }
        }
    }

    std::optional<ScenePrimitive> CreatePrimitive(const tg3_primitive& source, const std::string& label)
    {
        const int32_t mode = source.mode < 0 ? TG3_MODE_TRIANGLES : source.mode;
        if (mode != TG3_MODE_TRIANGLES)
        {
            MENGINE_LOG_WARN("%s: %s skipped: primitive mode %d is not supported (TRIANGLES only)", m_path.c_str(),
                label.c_str(), mode);
            return std::nullopt;
        }
        const int32_t positionAccessor = FindAttribute(source, "POSITION");
        if (positionAccessor < 0)
        {
            throw GltfError("missing POSITION");
        }
        const uint64_t vertexCount = Accessor(positionAccessor).count;
        if (vertexCount == 0 || vertexCount > std::numeric_limits<uint32_t>::max())
        {
            throw GltfError("invalid vertex count " + std::to_string(vertexCount));
        }
        const int32_t normalAccessor = FindAttribute(source, "NORMAL");
        const int32_t tangentAccessor = FindAttribute(source, "TANGENT");
        const int32_t texCoordAccessor = FindAttribute(source, "TEXCOORD_0");

        ScenePrimitive primitive;
        primitive.material = source.material >= 0 && static_cast<uint32_t>(source.material) < m_model.materials_count
                                 ? static_cast<uint32_t>(source.material)
                                 : DefaultMaterial();

        if (normalAccessor < 0)
        {
            CreateFlatPrimitive(source, positionAccessor, texCoordAccessor, primitive);
            return primitive;
        }

        auto bind = [&](VertexStreamIndex stream, const StreamBinding& binding) {
            primitive.vertexBuffers[stream] = binding.buffer;
            primitive.vertexOffsets[stream] = binding.offset;
        };
        bind(kVertexStreamPosition, AcquireFloatStream(positionAccessor, 3, vertexCount, "POSITION"));
        bind(kVertexStreamNormal, AcquireFloatStream(normalAccessor, 3, vertexCount, "NORMAL"));
        if (texCoordAccessor >= 0)
        {
            bind(kVertexStreamTexCoord, AcquireFloatStream(texCoordAccessor, 2, vertexCount, "TEXCOORD_0"));
        }
        else
        {
            bind(kVertexStreamTexCoord, CreateFloatStream(std::vector<float>(vertexCount * 2, 0.0f), "TEXCOORD_0 (zero)"));
        }
        if (tangentAccessor >= 0)
        {
            bind(kVertexStreamTangent, AcquireFloatStream(tangentAccessor, 4, vertexCount, "TANGENT"));
            primitive.hasTangents = true;
        }

        IndexBinding indices;
        if (source.indices >= 0)
        {
            indices = AcquireIndexStream(source.indices, vertexCount);
        }
        else
        {
            std::vector<uint32_t> sequential(vertexCount);
            for (uint32_t i = 0; i < sequential.size(); ++i)
            {
                sequential[i] = i;
            }
            indices = CreateIndexStream(sequential);
        }
        if (indices.count < 3)
        {
            throw GltfError("fewer than three indices");
        }
        primitive.indexBuffer = indices.buffer;
        primitive.indexOffset = indices.offset;
        primitive.indexType = indices.type;
        primitive.indexCount = indices.count - indices.count % 3;
        ComputeBounds(positionAccessor, nullptr, primitive);
        return primitive;
    }

    // glTF 2.0 3.7.2.1: without normals the client must use flat normals and ignore tangents, so the triangles are
    // expanded to unshared vertices.
    void CreateFlatPrimitive(const tg3_primitive& source, int32_t positionAccessor, int32_t texCoordAccessor,
        ScenePrimitive& primitive)
    {
        const std::vector<float> positions = ReadFloats(positionAccessor, 3);
        const uint64_t vertexCount = positions.size() / 3;
        std::vector<float> texCoords;
        if (texCoordAccessor >= 0)
        {
            texCoords = ReadFloats(texCoordAccessor, 2);
            if (texCoords.size() / 2 != vertexCount)
            {
                throw GltfError("TEXCOORD_0 count does not match POSITION");
            }
        }
        std::vector<uint32_t> indices;
        if (source.indices >= 0)
        {
            indices = ReadIndices(source.indices);
        }
        else
        {
            indices.resize(vertexCount);
            for (uint32_t i = 0; i < indices.size(); ++i)
            {
                indices[i] = i;
            }
        }
        const size_t triangleCount = indices.size() / 3;
        if (triangleCount == 0)
        {
            throw GltfError("no triangles");
        }

        std::vector<float> flatPositions(triangleCount * 9);
        std::vector<float> flatNormals(triangleCount * 9);
        std::vector<float> flatTexCoords(triangleCount * 6, 0.0f);
        for (size_t t = 0; t < triangleCount; ++t)
        {
            glm::vec3 corners[3];
            for (size_t k = 0; k < 3; ++k)
            {
                const uint32_t index = indices[t * 3 + k];
                if (index >= vertexCount)
                {
                    throw GltfError("index " + std::to_string(index) + " exceeds vertex count");
                }
                corners[k] = glm::vec3(positions[index * 3], positions[index * 3 + 1], positions[index * 3 + 2]);
                std::memcpy(&flatPositions[(t * 3 + k) * 3], &positions[index * 3], 3 * sizeof(float));
                if (!texCoords.empty())
                {
                    std::memcpy(&flatTexCoords[(t * 3 + k) * 2], &texCoords[index * 2], 2 * sizeof(float));
                }
            }
            glm::vec3 normal = glm::cross(corners[1] - corners[0], corners[2] - corners[0]);
            const float length = glm::length(normal);
            normal = length > 0.0f ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
            for (size_t k = 0; k < 3; ++k)
            {
                std::memcpy(&flatNormals[(t * 3 + k) * 3], &normal, 3 * sizeof(float));
            }
        }

        auto bind = [&](VertexStreamIndex stream, const StreamBinding& binding) {
            primitive.vertexBuffers[stream] = binding.buffer;
            primitive.vertexOffsets[stream] = binding.offset;
        };
        bind(kVertexStreamPosition, CreateFloatStream(flatPositions, "POSITION (flat)"));
        bind(kVertexStreamNormal, CreateFloatStream(flatNormals, "NORMAL (flat)"));
        bind(kVertexStreamTexCoord, CreateFloatStream(flatTexCoords, "TEXCOORD_0 (flat)"));

        std::vector<uint32_t> sequential(triangleCount * 3);
        for (uint32_t i = 0; i < sequential.size(); ++i)
        {
            sequential[i] = i;
        }
        const IndexBinding indexBinding = CreateIndexStream(sequential);
        primitive.indexBuffer = indexBinding.buffer;
        primitive.indexType = indexBinding.type;
        primitive.indexCount = indexBinding.count;
        ComputeBounds(positionAccessor, &positions, primitive);
    }

    static glm::mat4 LocalTransform(const tg3_node& node)
    {
        if (node.has_matrix)
        {
            glm::mat4 matrix;
            float* values = glm::value_ptr(matrix);
            for (int i = 0; i < 16; ++i)
            {
                values[i] = static_cast<float>(node.matrix[i]); // both column-major
            }
            return matrix;
        }
        const glm::vec3 translation(static_cast<float>(node.translation[0]), static_cast<float>(node.translation[1]),
            static_cast<float>(node.translation[2]));
        const glm::quat rotation(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
            static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]));
        const glm::vec3 scale(static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]),
            static_cast<float>(node.scale[2]));
        glm::mat4 matrix = glm::mat4_cast(rotation);
        matrix[0] *= scale.x;
        matrix[1] *= scale.y;
        matrix[2] *= scale.z;
        matrix[3] = glm::vec4(translation, 1.0f);
        return matrix;
    }

    void VisitNode(int32_t nodeIndex, const glm::mat4& parent, uint32_t depth)
    {
        if (nodeIndex < 0 || static_cast<uint32_t>(nodeIndex) >= m_model.nodes_count)
        {
            return;
        }
        if (depth > kMaxNodeDepth)
        {
            MENGINE_LOG_WARN("%s: node hierarchy deeper than %u (cycle?), truncated", m_path.c_str(), kMaxNodeDepth);
            return;
        }
        const tg3_node& node = m_model.nodes[nodeIndex];
        const glm::mat4 world = parent * LocalTransform(node);
        if (node.mesh >= 0 && static_cast<uint32_t>(node.mesh) < m_model.meshes_count)
        {
            for (uint32_t primitiveIndex : m_meshPrimitives[node.mesh])
            {
                AddDraw(primitiveIndex, world);
            }
        }
        for (uint32_t i = 0; i < node.children_count; ++i)
        {
            VisitNode(node.children[i], world, depth + 1);
        }
    }

    void AddDraw(uint32_t primitiveIndex, const glm::mat4& world)
    {
        const ScenePrimitive& primitive = m_scene.primitives[primitiveIndex];
        const SceneMaterial& material = m_scene.materials[primitive.material];
        if (material.transmissive)
        {
            ++m_skippedTransmissiveDraws;
            return;
        }

        SceneDraw draw;
        draw.primitive = primitiveIndex;
        draw.material = primitive.material;
        draw.constants.model = world;
        draw.constants.normalMatrix = glm::inverseTranspose(world);
        if (material.alphaMode != AlphaMode::Opaque)
        {
            draw.pipelineKey |= kPipelineKeyAlphaMask;
        }
        if (material.doubleSided)
        {
            draw.pipelineKey |= kPipelineKeyDoubleSided;
        }
        if (glm::determinant(world) < 0.0f)
        {
            draw.pipelineKey |= kPipelineKeyMirrored;
        }
        if (primitive.hasTangents)
        {
            draw.pipelineKey |= kPipelineKeyTangents;
        }

        // An empty primitive (no positions) keeps empty bounds and stays out of the scene bounds.
        if (primitive.boundsMin.x <= primitive.boundsMax.x)
        {
            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 local((corner & 1) ? primitive.boundsMax.x : primitive.boundsMin.x,
                    (corner & 2) ? primitive.boundsMax.y : primitive.boundsMin.y,
                    (corner & 4) ? primitive.boundsMax.z : primitive.boundsMin.z);
                const glm::vec3 point = glm::vec3(world * glm::vec4(local, 1.0f));
                draw.boundsMin = glm::min(draw.boundsMin, point);
                draw.boundsMax = glm::max(draw.boundsMax, point);
            }
            m_scene.boundsMin = glm::min(m_scene.boundsMin, draw.boundsMin);
            m_scene.boundsMax = glm::max(m_scene.boundsMax, draw.boundsMax);
        }
        m_scene.draws.push_back(draw);
    }

    void CreateDraws()
    {
        std::vector<int32_t> roots;
        if (m_model.scenes_count > 0)
        {
            const uint32_t sceneIndex =
                m_model.default_scene >= 0 && static_cast<uint32_t>(m_model.default_scene) < m_model.scenes_count
                    ? static_cast<uint32_t>(m_model.default_scene)
                    : 0u;
            const tg3_scene& scene = m_model.scenes[sceneIndex];
            roots.assign(scene.nodes, scene.nodes + scene.nodes_count);
        }
        else
        {
            // No scenes: render every node that is not somebody's child.
            std::vector<bool> isChild(m_model.nodes_count, false);
            for (uint32_t i = 0; i < m_model.nodes_count; ++i)
            {
                for (uint32_t c = 0; c < m_model.nodes[i].children_count; ++c)
                {
                    const int32_t child = m_model.nodes[i].children[c];
                    if (child >= 0 && static_cast<uint32_t>(child) < m_model.nodes_count)
                    {
                        isChild[child] = true;
                    }
                }
            }
            for (uint32_t i = 0; i < m_model.nodes_count; ++i)
            {
                if (!isChild[i])
                {
                    roots.push_back(static_cast<int32_t>(i));
                }
            }
        }
        for (int32_t root : roots)
        {
            VisitNode(root, glm::mat4(1.0f), 0);
        }
        if (m_skippedTransmissiveDraws > 0)
        {
            MENGINE_LOG_WARN("%s: KHR_materials_transmission is not supported, skipped %zu transmissive (glass) draws",
                m_path.c_str(), m_skippedTransmissiveDraws);
        }

        std::stable_sort(m_scene.draws.begin(), m_scene.draws.end(), [](const SceneDraw& a, const SceneDraw& b) {
            const uint32_t maskA = a.pipelineKey & kPipelineKeyAlphaMask;
            const uint32_t maskB = b.pipelineKey & kPipelineKeyAlphaMask;
            if (maskA != maskB)
            {
                return maskA < maskB;
            }
            if (a.pipelineKey != b.pipelineKey)
            {
                return a.pipelineKey < b.pipelineKey;
            }
            return a.material < b.material;
        });
    }

    void WarnAboutExtensions() const
    {
        static constexpr std::array<std::string_view, 3> kSupported{
            kExtensionSpecularGlossiness, kExtensionTextureDds, kExtensionTransmission};
        for (uint32_t i = 0; i < m_model.extensions_required_count; ++i)
        {
            const std::string_view name = ToView(m_model.extensions_required[i]);
            if (std::find(kSupported.begin(), kSupported.end(), name) == kSupported.end())
            {
                MENGINE_LOG_WARN("%s: required extension %.*s is not supported, the scene may render incorrectly",
                    m_path.c_str(), static_cast<int>(name.size()), name.data());
            }
        }
    }

    rhi::GpuDevice& m_device;
    const tg3_model& m_model;
    std::string m_path;
    std::string m_baseDirectory;
    rhi::DescriptorSetLayoutHandle m_materialLayout;
    Scene& m_scene;

    std::map<int32_t, rhi::BufferHandle> m_bufferViewBuffers;
    std::map<int32_t, rhi::SamplerHandle> m_samplers;
    std::vector<std::pair<uint32_t, bool>> m_imageRequests;
    std::map<std::pair<uint32_t, bool>, size_t> m_imageRequestIndex;
    std::vector<rhi::TextureHandle> m_imageTextures;
    std::optional<uint32_t> m_defaultMaterial;
    std::vector<std::vector<uint32_t>> m_meshPrimitives;
    size_t m_skippedTransmissiveDraws = 0;
};

// Keeps the device's upload batch balanced even when loading throws halfway.
class UploadBatchScope
{
public:
    explicit UploadBatchScope(rhi::GpuDevice& device) : m_device(device) { m_device.BeginUploadBatch(); }
    ~UploadBatchScope()
    {
        if (m_open)
        {
            try
            {
                m_device.EndUploadBatch();
            }
            catch (const std::exception& exception)
            {
                MENGINE_LOG_ERROR("Upload batch failed during cleanup: %s", exception.what());
            }
        }
    }
    void End()
    {
        m_open = false;
        m_device.EndUploadBatch();
    }
    UploadBatchScope(const UploadBatchScope&) = delete;
    UploadBatchScope& operator=(const UploadBatchScope&) = delete;

private:
    rhi::GpuDevice& m_device;
    bool m_open = true;
};

} // namespace

bool LoadGltfScene(rhi::GpuDevice& device, std::string_view assetPath, rhi::DescriptorSetLayoutHandle materialLayout, Scene& scene)
{
    const std::string path(assetPath);
    const auto start = std::chrono::steady_clock::now();
    scene.Destroy(device);
    scene.path = path;

    const std::optional<std::vector<uint8_t>> bytes = ReadAssetFile(path);
    if (!bytes)
    {
        MENGINE_LOG_ERROR("Failed to load scene %s: file not found", path.c_str());
        scene.path.clear();
        return false;
    }

    ParsedModel parsed;
    tg3_parse_options options;
    tg3_parse_options_init(&options);
    options.fs.read_file = ReadFileCallback;
    options.fs.free_file = FreeFileCallback;
    options.images_as_is = 1;

    const std::string baseDirectory(ParentPath(path));
    const tg3_error_code result = tg3_parse_auto(&parsed.model, &parsed.errors, bytes->data(), bytes->size(),
        baseDirectory.c_str(), static_cast<uint32_t>(baseDirectory.size()), &options);
    for (uint32_t i = 0; i < parsed.errors.count; ++i)
    {
        const tg3_error_entry& entry = parsed.errors.entries[i];
        const char* message = entry.message != nullptr ? entry.message : "(no message)";
        const char* where = entry.json_path != nullptr ? entry.json_path : "";
        if (entry.severity == TG3_SEVERITY_ERROR)
        {
            MENGINE_LOG_ERROR("%s: %s %s", path.c_str(), message, where);
        }
        else
        {
            MENGINE_LOG_WARN("%s: %s %s", path.c_str(), message, where);
        }
    }
    if (result != TG3_OK)
    {
        MENGINE_LOG_ERROR("Failed to load scene %s: glTF parse error %d", path.c_str(), static_cast<int>(result));
        scene.path.clear();
        return false;
    }

    try
    {
        UploadBatchScope batch(device);
        GltfSceneBuilder(device, parsed.model, path, materialLayout, scene).Build();
        batch.End();
    }
    catch (const std::exception& exception)
    {
        MENGINE_LOG_ERROR("Failed to load scene %s: %s", path.c_str(), exception.what());
        scene.Destroy(device);
        return false;
    }

    if (scene.IsEmpty())
    {
        MENGINE_LOG_WARN("%s: the scene contains nothing renderable", path.c_str());
    }
    const double milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    MENGINE_LOG_INFO("Loaded %s in %.0f ms: %zu primitives, %zu materials, %zu textures, %zu draws", path.c_str(),
        milliseconds, scene.primitives.size(), scene.materials.size(), scene.textures.size(), scene.draws.size());
    return true;
}

} // namespace mengine::render
