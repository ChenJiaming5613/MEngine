#pragma once

#include "render/Scene.hpp"
#include "rhi/GpuResources.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mengine::rhi {
class GpuDevice;
}

namespace mengine::render {

// Loading is split into a CPU stage (ParseGltfScene, PrepareGltfTexture: any thread, never touches the GPU) and a GPU
// stage (GltfSceneUploader: main thread only). The CPU result is defined in GltfSceneData.hpp.
struct GltfSceneData;

struct GltfParseOptions
{
    // GpuDevice::SupportsTextureCompressionBc(), queried by the caller on the main thread.
    bool bcSupported = false;
};

// A decoded texture ready for GpuDevice::CreateTexture.
struct PreparedTexture
{
    std::vector<uint8_t> bytes; // owns the pixel data, which starts at dataOffset
    size_t dataOffset = 0;
    size_t dataSize = 0;
    VkFormat format = VK_FORMAT_UNDEFINED; // UNDEFINED: failed, the material falls back to the dummy texture
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 1; // levels present in the data, or 0 with generateMips
    bool generateMips = false;
    bool decodedBc = false; // BC7 decoded on the CPU
    std::string error;
};

// Reads and parses a glTF 2.0 scene (.gltf or .glb) from the asset root; URIs resolve relative to the file. Missing
// normals are computed flat (as the spec requires), missing tangents are built in the shader, alpha BLEND falls back to
// MASK. Thread safe: only reads files and logs. Returns null on a file level error, which has been logged with the path.
std::unique_ptr<GltfSceneData> ParseGltfScene(std::string_view assetPath, const GltfParseOptions& options);

// Reads and decodes data.textureRequests[requestIndex]. Thread safe, also for concurrent calls on one GltfSceneData
// with different indices. Never throws: errors leave format UNDEFINED, which only costs this texture.
PreparedTexture PrepareGltfTexture(const GltfSceneData& data, size_t requestIndex);

// Creates the GPU resources of a parsed scene. Main thread only. Resources go into `scene` as soon as they exist, so
// when a method throws (device errors, a failed buffer creation) Scene::Destroy releases the partial scene; the caller
// must do so and treat the load as failed. `data` must outlive the uploader.
class GltfSceneUploader
{
public:
    // materialLayout is the forward pass's set 1 layout; `scene` must be empty.
    GltfSceneUploader(rhi::GpuDevice& device, const GltfSceneData& data, rhi::DescriptorSetLayoutHandle materialLayout,
        Scene& scene);

    // Uploads the next buffers, at least one, stopping before the one that would take this call over byteBudget.
    // Returns true when every buffer has been uploaded.
    bool UploadBuffers(VkDeviceSize byteBudget);
    // Once per request, in any order. A failed texture is logged and left out; its material slots use the dummy.
    void UploadTexture(size_t requestIndex, PreparedTexture&& texture);
    // After every buffer and texture: creates samplers, materials and descriptor sets, fills in the draw data and
    // logs the load statistics.
    void Finish();

    VkDeviceSize UploadedBufferBytes() const { return m_uploadedBufferBytes; }
    VkDeviceSize TotalBufferBytes() const { return m_totalBufferBytes; }

private:
    void CreateMaterial(size_t materialIndex, const std::vector<rhi::SamplerHandle>& samplers);

    rhi::GpuDevice& m_device;
    const GltfSceneData& m_data;
    rhi::DescriptorSetLayoutHandle m_materialLayout;
    Scene& m_scene;

    size_t m_nextBuffer = 0;
    VkDeviceSize m_uploadedBufferBytes = 0;
    VkDeviceSize m_totalBufferBytes = 0;
    std::vector<rhi::TextureHandle> m_textures; // per texture request, invalid when it failed
    std::vector<bool> m_textureHandled;
    size_t m_handledTextureCount = 0;
    size_t m_decodedBcCount = 0;
    bool m_finished = false;
};

} // namespace mengine::render
