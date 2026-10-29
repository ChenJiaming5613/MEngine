#pragma once

#include "rhi/GpuResources.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <vector>

namespace mengine::rhi {
class GpuDevice;
} // namespace mengine::rhi

namespace mengine::render {

// One end of a debug line: 16 bytes, color as RGBA8 UNORM (sRGB encoded, like UI colors).
struct DebugVertex
{
    glm::vec3 position{0.0f};
    uint32_t color = 0;
};
static_assert(sizeof(DebugVertex) == 16);

// CPU side debug lines (Raptor's DebugRenderer, without GPU line generation). Lines added any time before
// Renderer::Render are uploaded, drawn into the HDR target and then cleared. Lines past kMaxLines are dropped with a
// single warning.
class DebugDraw
{
public:
    static constexpr uint32_t kMaxLines = 64u * 1024u;
    static constexpr uint32_t kSphereSegments = 32;

    void Init(rhi::GpuDevice& device);
    void Shutdown();

    void Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);
    void Aabb(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);
    // Three great circles in the XY, XZ and YZ planes.
    void Sphere(const glm::vec3& center, float radius, const glm::vec4& color);
    // The 12 edges of the volume a view projection maps to clip space (Vulkan depth range 0..1).
    void Frustum(const glm::mat4& viewProjection, const glm::vec4& color);

    // Copies the lines into the current frame slot's buffer range. Call between GpuDevice::BeginFrame and the draw.
    void Upload();
    // Forgets this frame's lines (after the frame is recorded).
    void Clear();

    uint32_t VertexCount() const { return m_uploadedVertices; }
    uint32_t LineCount() const { return static_cast<uint32_t>(m_vertices.size() / 2); }
    rhi::BufferHandle Buffer() const { return m_buffer; }
    VkDeviceSize BufferOffset() const { return m_uploadedOffset; }

    static uint32_t PackColor(const glm::vec4& color);

private:
    rhi::GpuDevice* m_device = nullptr;
    rhi::BufferHandle m_buffer;
    std::vector<DebugVertex> m_vertices;
    uint32_t m_uploadedVertices = 0;
    VkDeviceSize m_uploadedOffset = 0;
    bool m_overflowWarned = false;
};

} // namespace mengine::render
