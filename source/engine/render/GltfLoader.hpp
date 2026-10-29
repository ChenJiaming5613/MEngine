#pragma once

#include "render/Scene.hpp"
#include "rhi/GpuResources.hpp"

#include <string_view>

namespace mengine::rhi {
class GpuDevice;
}

namespace mengine::render {

// Loads a glTF 2.0 scene (.gltf or .glb) from the asset root; URIs resolve relative to the file. Missing normals are
// computed flat (as the spec requires), missing tangents are built in the shader, alpha BLEND falls back to MASK.
// materialLayout is the forward pass's set 1 layout. On failure returns false with `scene` empty; every problem is
// logged with the file path.
bool LoadGltfScene(rhi::GpuDevice& device, std::string_view assetPath, rhi::DescriptorSetLayoutHandle materialLayout, Scene& scene);

} // namespace mengine::render
