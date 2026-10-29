#pragma once

#include "render/GltfLoader.hpp"
#include "rhi/GpuResources.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>

namespace enki {
class TaskScheduler;
}

namespace mengine::rhi {
class GpuDevice;
}

namespace mengine::render {

struct Scene;

enum class SceneLoadStatus : uint8_t
{
    Idle,
    Loading,
    // Returned by the Update that completed the load, then Idle.
    Finished,
    // Returned by the Update that gave up (the scene is left empty), then Idle.
    Failed,
};

struct SceneLoadProgress
{
    bool parsing = false;
    size_t texturesUploaded = 0;
    size_t textureCount = 0;
};

// Loads a glTF scene over several frames. Parsing and texture decoding run as enkiTS tasks (CPU only); the main thread
// uploads what is ready in Update, within a byte budget per call, so frames keep going while a large scene loads.
// The target scene is only complete after Update returns Finished; until then it has resources but no draws.
class SceneLoader
{
public:
    // Out of line: the task and scene data types are private to SceneLoader.cpp.
    SceneLoader();
    ~SceneLoader();
    SceneLoader(SceneLoader&&) noexcept;
    SceneLoader& operator=(SceneLoader&&) noexcept;

    void Init(rhi::GpuDevice& device, enki::TaskScheduler& tasks, rhi::DescriptorSetLayoutHandle materialLayout);
    void Shutdown();

    // `scene` must be empty and must not be touched by anyone else until the load ends. Cancels a load in flight.
    void Start(std::string_view assetPath, Scene& scene);
    // Waits for the running tasks and destroys the partially loaded scene.
    void Cancel();
    // Main thread, outside BeginFrame / EndFrame.
    SceneLoadStatus Update(VkDeviceSize uploadBudget);

    bool IsLoading() const { return m_scene != nullptr; }
    SceneLoadProgress GetProgress() const;

private:
    struct ParseTask;
    struct DecodeTask;

    void LaunchDecodes();
    void WaitForTasks();
    void Reset();
    SceneLoadStatus Fail(const char* reason);

    rhi::GpuDevice* m_device = nullptr;
    enki::TaskScheduler* m_tasks = nullptr;
    rhi::DescriptorSetLayoutHandle m_materialLayout;
    size_t m_decodeWindow = 2;

    Scene* m_scene = nullptr;
    std::string m_path;
    std::unique_ptr<ParseTask> m_parse;
    std::unique_ptr<GltfSceneData> m_data;
    std::unique_ptr<GltfSceneUploader> m_uploader;
    std::deque<std::unique_ptr<DecodeTask>> m_decoding; // in request order
    size_t m_nextRequest = 0;
    size_t m_texturesUploaded = 0;
    bool m_buffersUploaded = false;
    uint32_t m_updateCount = 0;
    double m_longestUpdateMilliseconds = 0.0;
};

} // namespace mengine::render
