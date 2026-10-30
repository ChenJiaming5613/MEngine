#pragma once

#include "render/GltfLoader.hpp"
#include "rhi/GpuResources.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

enum class SceneLoadStage : uint8_t
{
    Parsing,
    Uploading,
};

struct SceneLoadProgress
{
    SceneLoadStage stage = SceneLoadStage::Parsing;
    double elapsedSeconds = 0.0;
    VkDeviceSize geometryBytesUploaded = 0;
    VkDeviceSize geometryBytes = 0;
    size_t texturesUploaded = 0;
    size_t textureCount = 0;

    // 0..1 over the upload stage; negative while parsing, which tinygltf does not report progress for. Geometry takes
    // the first quarter: it uploads within a few frames, while decoding textures takes most of a load.
    float Fraction() const;
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
    // Destroys the partially loaded scene without waiting: tasks still running (a parse cannot be interrupted) finish
    // in the background and are freed by a later Update, or waited for by Shutdown.
    void Cancel();
    // Main thread, outside BeginFrame / EndFrame. Call every frame, loading or not, to free cancelled tasks.
    SceneLoadStatus Update(VkDeviceSize uploadBudget);

    bool IsLoading() const { return m_scene != nullptr; }
    SceneLoadProgress GetProgress() const;

private:
    struct ParseTask;
    struct DecodeTask;
    // The tasks of a cancelled load and the data they read, kept alive until they finish.
    struct OrphanedTasks;

    void LaunchDecodes();
    void OrphanTasks();
    void FreeFinishedOrphans();
    void Reset();

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
    std::vector<std::unique_ptr<OrphanedTasks>> m_orphans;
    std::chrono::steady_clock::time_point m_startTime;
    size_t m_nextRequest = 0;
    size_t m_texturesUploaded = 0;
    bool m_buffersUploaded = false;
    uint32_t m_updateCount = 0;
    double m_longestUpdateMilliseconds = 0.0;
};

} // namespace mengine::render
