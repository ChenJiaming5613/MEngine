#pragma once

#include "render/Scene.hpp"
#include "rhi/GpuResources.hpp"

#include <string_view>

namespace mengine::rhi {
class GpuDevice;
}

namespace mengine::render {

// Loads a glTF 2.0 scene (.gltf with external or embedded resources, or .glb) from the asset root. URIs inside the file
// resolve relative to its directory. Supports the node hierarchy, metallic-roughness materials with base color,
// metallic-roughness, normal, occlusion and emissive textures, alpha MASK (BLEND falls back to MASK) and double sided
// materials. Missing normals are computed (flat, as the spec requires); missing tangents are built in the shader.
//
// materialLayout is the set 1 layout of the forward pass. Returns false and leaves `scene` empty on failure;
// every problem is logged together with the file path.
bool LoadGltfScene(rhi::GpuDevice& device, std::string_view assetPath, rhi::DescriptorSetLayoutHandle materialLayout, Scene& scene);

} // namespace mengine::render
