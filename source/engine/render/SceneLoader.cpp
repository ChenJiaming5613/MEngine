#include "render/SceneLoader.hpp"

#include "core/Log.hpp"
#include "render/GltfSceneData.hpp"
#include "render/Scene.hpp"
#include "rhi/GpuDevice.hpp"
#include "rhi/VulkanUtils.hpp"

#include <TaskScheduler.h>

#include <algorithm>
#include <chrono>
#include <exception>

namespace mengine::render {

// Loading yields to frame work: a thread waiting for high priority tasks (parallel recording) never picks these up.
struct SceneLoader::ParseTask : enki::ITaskSet
{
    ParseTask() { m_Priority = enki::TASK_PRIORITY_LOW; }

    std::string path;
    GltfParseOptions options;
    std::unique_ptr<GltfSceneData> result;

    void ExecuteRange(enki::TaskSetPartition, uint32_t) override { result = ParseGltfScene(path, options); }
};

struct SceneLoader::DecodeTask : enki::ITaskSet
{
    DecodeTask() { m_Priority = enki::TASK_PRIORITY_LOW; }

    const GltfSceneData* data = nullptr;
    size_t requestIndex = 0;
    PreparedTexture result;

    void ExecuteRange(enki::TaskSetPartition, uint32_t) override { result = PrepareGltfTexture(*data, requestIndex); }
};

struct SceneLoader::OrphanedTasks
{
    std::unique_ptr<ParseTask> parse;
    std::unique_ptr<GltfSceneData> data;
    std::deque<std::unique_ptr<DecodeTask>> decoding;

    bool IsComplete() const
    {
        return (!parse || parse->GetIsComplete())
            && std::all_of(decoding.begin(), decoding.end(), [](const auto& task) { return task->GetIsComplete(); });
    }
};

float SceneLoadProgress::Fraction() const
{
    if (stage == SceneLoadStage::Parsing)
    {
        return -1.0f;
    }
    constexpr float kGeometryShare = 0.25f;
    const float geometry = geometryBytes > 0 ? static_cast<float>(geometryBytesUploaded) / static_cast<float>(geometryBytes) : 1.0f;
    if (textureCount == 0)
    {
        return geometry;
    }
    const float textures = static_cast<float>(texturesUploaded) / static_cast<float>(textureCount);
    return kGeometryShare * geometry + (1.0f - kGeometryShare) * textures;
}

SceneLoader::SceneLoader() = default;
SceneLoader::~SceneLoader() = default;
SceneLoader::SceneLoader(SceneLoader&&) noexcept = default;
SceneLoader& SceneLoader::operator=(SceneLoader&&) noexcept = default;

void SceneLoader::Init(rhi::GpuDevice& device, enki::TaskScheduler& tasks, rhi::DescriptorSetLayoutHandle materialLayout)
{
    m_device = &device;
    m_tasks = &tasks;
    m_materialLayout = materialLayout;
    // Decoding runs ahead of the uploads by this many textures, which caps the decoded pixels held in memory.
    m_decodeWindow = std::max<size_t>(2, tasks.GetNumTaskThreads());
}

void SceneLoader::Shutdown()
{
    if (m_device != nullptr)
    {
        Cancel();
    }
    for (const std::unique_ptr<OrphanedTasks>& orphan : m_orphans)
    {
        if (orphan->parse)
        {
            m_tasks->WaitforTask(orphan->parse.get());
        }
        for (const std::unique_ptr<DecodeTask>& task : orphan->decoding)
        {
            m_tasks->WaitforTask(task.get());
        }
    }
    m_orphans.clear();
    m_device = nullptr;
    m_tasks = nullptr;
}

void SceneLoader::Start(std::string_view assetPath, Scene& scene)
{
    Cancel();
    m_scene = &scene;
    m_path = std::string(assetPath);
    m_startTime = std::chrono::steady_clock::now();
    m_parse = std::make_unique<ParseTask>();
    m_parse->path = m_path;
    m_parse->options.bcSupported = m_device->SupportsTextureCompressionBc();
    m_tasks->AddTaskSetToPipe(m_parse.get());
}

void SceneLoader::Cancel()
{
    if (m_scene == nullptr)
    {
        return;
    }
    m_uploader.reset();
    OrphanTasks();
    m_scene->Destroy(*m_device);
    Reset();
}

void SceneLoader::OrphanTasks()
{
    if (!m_parse && m_decoding.empty())
    {
        return;
    }
    auto orphan = std::make_unique<OrphanedTasks>();
    orphan->parse = std::move(m_parse);
    orphan->data = std::move(m_data);
    orphan->decoding = std::move(m_decoding);
    m_orphans.push_back(std::move(orphan));
}

void SceneLoader::FreeFinishedOrphans()
{
    std::erase_if(m_orphans, [](const std::unique_ptr<OrphanedTasks>& orphan) { return orphan->IsComplete(); });
}

void SceneLoader::Reset()
{
    m_decoding.clear();
    m_uploader.reset();
    m_data.reset();
    m_parse.reset();
    m_scene = nullptr;
    m_path.clear();
    m_nextRequest = 0;
    m_texturesUploaded = 0;
    m_buffersUploaded = false;
    m_updateCount = 0;
    m_longestUpdateMilliseconds = 0.0;
}

void SceneLoader::LaunchDecodes()
{
    const size_t requestCount = m_data->textureRequests.size();
    while (m_nextRequest < requestCount && m_decoding.size() < m_decodeWindow)
    {
        auto task = std::make_unique<DecodeTask>();
        task->data = m_data.get();
        task->requestIndex = m_nextRequest++;
        m_tasks->AddTaskSetToPipe(task.get());
        m_decoding.push_back(std::move(task));
    }
}

SceneLoadStatus SceneLoader::Update(VkDeviceSize uploadBudget)
{
    FreeFinishedOrphans();
    if (m_scene == nullptr)
    {
        return SceneLoadStatus::Idle;
    }
    if (m_parse)
    {
        if (!m_parse->GetIsComplete())
        {
            return SceneLoadStatus::Loading;
        }
        m_data = std::move(m_parse->result);
        m_parse.reset();
        if (!m_data)
        {
            // ParseGltfScene has logged the reason.
            Cancel();
            return SceneLoadStatus::Failed;
        }
        m_uploader = std::make_unique<GltfSceneUploader>(*m_device, *m_data, m_materialLayout, *m_scene);
        LaunchDecodes();
    }

    const auto updateStart = std::chrono::steady_clock::now();
    try
    {
        // One submission per update, sized to fit one upload slot: crossing into the next slot could wait for the GPU.
        rhi::UploadBatchScope batch(*m_device);
        VkDeviceSize uploaded = 0;
        if (!m_buffersUploaded)
        {
            m_buffersUploaded = m_uploader->UploadBuffers(uploadBudget);
            uploaded = uploadBudget;
        }
        while (!m_decoding.empty() && m_decoding.front()->GetIsComplete() && uploaded < uploadBudget)
        {
            if (uploaded > 0 && !m_device->UploadBatchHasRoomFor(m_decoding.front()->result.dataSize))
            {
                break;
            }
            const std::unique_ptr<DecodeTask> task = std::move(m_decoding.front());
            m_decoding.pop_front();
            uploaded += task->result.dataSize;
            m_uploader->UploadTexture(task->requestIndex, std::move(task->result));
            ++m_texturesUploaded;
            LaunchDecodes();
        }
        const bool done = m_buffersUploaded && m_texturesUploaded == m_data->textureRequests.size();
        if (done)
        {
            // In the same batch: material constants are tiny, and a second batch would reuse an older upload slot.
            m_uploader->Finish();
        }
        batch.End();
        ++m_updateCount;
        m_longestUpdateMilliseconds = std::max(m_longestUpdateMilliseconds,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - updateStart).count());
        if (!done)
        {
            return SceneLoadStatus::Loading;
        }
    }
    catch (const std::exception& exception)
    {
        const std::string path = m_path;
        Cancel();
        rhi::RethrowIfDeviceUnusable(exception);
        MENGINE_LOG_ERROR("Failed to load scene %s: %s", path.c_str(), exception.what());
        return SceneLoadStatus::Failed;
    }

    MENGINE_LOG_INFO("Uploaded %s over %u updates, longest %.1f ms", m_path.c_str(), m_updateCount, m_longestUpdateMilliseconds);
    Reset();
    return SceneLoadStatus::Finished;
}

SceneLoadProgress SceneLoader::GetProgress() const
{
    return SceneLoadProgress{
        .stage = m_uploader ? SceneLoadStage::Uploading : SceneLoadStage::Parsing,
        .elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_startTime).count(),
        .geometryBytesUploaded = m_uploader ? m_uploader->UploadedBufferBytes() : 0,
        .geometryBytes = m_uploader ? m_uploader->TotalBufferBytes() : 0,
        .texturesUploaded = m_texturesUploaded,
        .textureCount = m_data ? m_data->textureRequests.size() : 0,
    };
}

} // namespace mengine::render
