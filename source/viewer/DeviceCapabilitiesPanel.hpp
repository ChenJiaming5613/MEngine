#pragma once

#include "rhi/DeviceCapabilities.hpp"

#include <vector>

namespace mengine {

namespace rhi {
class GpuDevice;
} // namespace rhi

// Read-only window listing rhi::DeviceCapabilities, with memory heaps' live budgets.
class DeviceCapabilitiesPanel
{
public:
    void Draw(const rhi::GpuDevice& device);

    bool IsOpen() const { return m_open; }
    void SetOpen(bool open) { m_open = open; }
    // (x, y) in window points, against the window drawn last frame.
    bool Contains(float x, float y) const;

private:
    void DrawHeader(const rhi::GpuDevice& device);
    void DrawOverview(const rhi::DeviceCapabilities& caps);
    void DrawFeatures(const rhi::GpuDevice& device);
    void DrawExtensions(const rhi::DeviceCapabilities& caps);
    void DrawLimits(const rhi::DeviceCapabilities& caps);
    void DrawMemory(const rhi::GpuDevice& device);
    void DrawQueues(const rhi::DeviceCapabilities& caps);
    void DrawFormats(const rhi::DeviceCapabilities& caps);

    bool m_open = false;
    char m_filter[64] = {};
    bool m_onlySupported = false;
    int m_extensionList = 0; // 0: device, 1: instance

    std::vector<rhi::HeapUsage> m_heaps;
    double m_heapsTime = -1.0;

    float m_windowMin[2] = {0.0f, 0.0f};
    float m_windowMax[2] = {-1.0f, -1.0f};
};

} // namespace mengine
