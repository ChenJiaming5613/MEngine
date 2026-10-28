#include "render/Scene.hpp"

#include "rhi/GpuDevice.hpp"

namespace mengine::render {

void Scene::Destroy(rhi::GpuDevice& device)
{
    for (const SceneMaterial& material : materials)
    {
        device.DestroyDescriptorSet(material.descriptorSet);
        device.DestroyBuffer(material.uniformBuffer);
    }
    for (rhi::BufferHandle buffer : buffers)
    {
        device.DestroyBuffer(buffer);
    }
    for (rhi::TextureHandle texture : textures)
    {
        device.DestroyTexture(texture);
    }
    for (rhi::SamplerHandle sampler : samplers)
    {
        device.DestroySampler(sampler);
    }
    *this = Scene{};
}

} // namespace mengine::render
