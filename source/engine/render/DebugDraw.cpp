#include "render/DebugDraw.hpp"

#include "core/Log.hpp"
#include "rhi/GpuDevice.hpp"

#include <glm/common.hpp>
#include <glm/matrix.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <numbers>

namespace mengine::render {

namespace {

constexpr VkDeviceSize kSegmentBytes = VkDeviceSize{DebugDraw::kMaxLines} * 2 * sizeof(DebugVertex);

} // namespace

void DebugDraw::Init(rhi::GpuDevice& device)
{
    m_device = &device;
    // One segment per frame in flight, written through the persistent mapping.
    m_buffer = device.CreateBuffer({
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .resourceUsage = rhi::ResourceUsage::Dynamic,
        .size = kSegmentBytes * rhi::kMaxFramesInFlight,
        .name = "DebugLines",
    });
    m_vertices.reserve(size_t{kMaxLines} * 2);
}

void DebugDraw::Shutdown()
{
    if (m_device != nullptr)
    {
        m_device->DestroyBuffer(m_buffer);
    }
    *this = DebugDraw{};
}

uint32_t DebugDraw::PackColor(const glm::vec4& color)
{
    const glm::vec4 scaled = glm::clamp(color, 0.0f, 1.0f) * 255.0f + 0.5f;
    return static_cast<uint32_t>(scaled.r) | (static_cast<uint32_t>(scaled.g) << 8) | (static_cast<uint32_t>(scaled.b) << 16)
        | (static_cast<uint32_t>(scaled.a) << 24);
}

void DebugDraw::Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    if (m_vertices.size() >= size_t{kMaxLines} * 2)
    {
        if (!m_overflowWarned)
        {
            MENGINE_LOG_WARN("DebugDraw: more than %u lines in a frame, the rest are dropped", kMaxLines);
            m_overflowWarned = true;
        }
        return;
    }
    const uint32_t packed = PackColor(color);
    m_vertices.push_back({from, packed});
    m_vertices.push_back({to, packed});
}

void DebugDraw::Aabb(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color)
{
    std::array<glm::vec3, 8> corners;
    for (uint32_t corner = 0; corner < 8; ++corner)
    {
        corners[corner] = glm::vec3((corner & 1) != 0 ? max.x : min.x, (corner & 2) != 0 ? max.y : min.y,
            (corner & 4) != 0 ? max.z : min.z);
    }
    // Corners differing in exactly one bit share an edge.
    for (uint32_t corner = 0; corner < 8; ++corner)
    {
        for (uint32_t bit = 1; bit < 8; bit <<= 1)
        {
            if ((corner & bit) == 0)
            {
                Line(corners[corner], corners[corner | bit], color);
            }
        }
    }
}

void DebugDraw::Sphere(const glm::vec3& center, float radius, const glm::vec4& color)
{
    constexpr float kStep = 2.0f * std::numbers::pi_v<float> / static_cast<float>(kSphereSegments);
    for (uint32_t segment = 0; segment < kSphereSegments; ++segment)
    {
        const float a0 = kStep * static_cast<float>(segment);
        const float a1 = kStep * static_cast<float>(segment + 1);
        const float c0 = radius * std::cos(a0);
        const float s0 = radius * std::sin(a0);
        const float c1 = radius * std::cos(a1);
        const float s1 = radius * std::sin(a1);
        Line(center + glm::vec3(c0, s0, 0.0f), center + glm::vec3(c1, s1, 0.0f), color);
        Line(center + glm::vec3(c0, 0.0f, s0), center + glm::vec3(c1, 0.0f, s1), color);
        Line(center + glm::vec3(0.0f, c0, s0), center + glm::vec3(0.0f, c1, s1), color);
    }
}

void DebugDraw::Frustum(const glm::mat4& viewProjection, const glm::vec4& color)
{
    const glm::mat4 inverse = glm::inverse(viewProjection);
    std::array<glm::vec3, 8> corners;
    for (uint32_t corner = 0; corner < 8; ++corner)
    {
        const glm::vec4 clip((corner & 1) != 0 ? 1.0f : -1.0f, (corner & 2) != 0 ? 1.0f : -1.0f,
            (corner & 4) != 0 ? 1.0f : 0.0f, 1.0f);
        const glm::vec4 world = inverse * clip;
        corners[corner] = glm::vec3(world) / world.w;
    }
    for (uint32_t corner = 0; corner < 8; ++corner)
    {
        for (uint32_t bit = 1; bit < 8; bit <<= 1)
        {
            if ((corner & bit) == 0)
            {
                Line(corners[corner], corners[corner | bit], color);
            }
        }
    }
}

void DebugDraw::Upload()
{
    m_uploadedVertices = static_cast<uint32_t>(m_vertices.size());
    m_uploadedOffset = kSegmentBytes * m_device->GetFrameSlot();
    if (m_uploadedVertices == 0)
    {
        return;
    }
    const rhi::Buffer* buffer = m_device->AccessBuffer(m_buffer);
    const VkDeviceSize bytes = VkDeviceSize{m_uploadedVertices} * sizeof(DebugVertex);
    std::memcpy(static_cast<uint8_t*>(buffer->mappedData) + m_uploadedOffset, m_vertices.data(), bytes);
    m_device->FlushBuffer(m_buffer, m_uploadedOffset, bytes);
}

void DebugDraw::Clear()
{
    m_vertices.clear();
    m_uploadedVertices = 0;
}

} // namespace mengine::render
