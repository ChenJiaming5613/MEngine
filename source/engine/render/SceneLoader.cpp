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

struct SceneLoader::ParseTask : enki::ITaskSet
{
    std::string path;
    GltfParseOptions options;
    std::unique_ptr<GltfSceneData> result;

    void ExecuteRange(enki::TaskSetPartition, uint32_t) override { result = ParseGltfScene(path, options); }
};

struct SceneLoader::DecodeTask : enki::ITaskSet
{
    const GltfSceneData* data = nullptr;
    size_t requestIndex = 0;
    PreparedTexture result;

    void ExecuteRange(enki::TaskSetPartition, uint32_t) override { result = PrepareGltfTexture(*data, requestIndex); }
};

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
    m_device = nullptr;
    m_tasks = nullptr;
}

void SceneLoader::Start(std::string_view assetPath, Scene& scene)
{
    Cancel();
    m_scene = &scene;
    m_path = std::string(assetPath);
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
    WaitForTasks();
    m_uploader.reset();
    m_scene->Destroy(*m_device);
    Reset();
}

void SceneLoader::WaitForTasks()
{
    if (m_parse)
    {
        m_tasks->WaitforTask(m_parse.get());
    }
    for (const std::unique_ptr<DecodeTask>& task : m_decoding)
    {
        m_tasks->WaitforTask(task.get());
    }
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

SceneLoadStatus SceneLoader::Fail(const char* reason)
{
    MENGINE_LOG_ERROR("Failed to load scene %s: %s", m_path.c_str(), reason);
    Cancel();
    return SceneLoadStatus::Failed;
}

SceneLoadStatus SceneLoader::Update(VkDeviceSize uploadBudget)
{
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
        // One submission per update: each upload batch waits for its fence.
        rhi::UploadBatchScope batch(*m_device);
        VkDeviceSize uploaded = 0;
        if (!m_buffersUploaded)
        {
            m_buffersUploaded = m_uploader->UploadBuffers(uploadBudget);
            uploaded = uploadBudget;
        }
        while (!m_decoding.empty() && m_decoding.front()->GetIsComplete() && uploaded < uploadBudget)
        {
            const std::unique_ptr<DecodeTask> task = std::move(m_decoding.front());
            m_decoding.pop_front();
            uploaded += task->result.dataSize;
            m_uploader->UploadTexture(task->requestIndex, std::move(task->result));
            ++m_texturesUploaded;
            LaunchDecodes();
        }
        batch.End();

        const bool done = m_buffersUploaded && m_texturesUploaded == m_data->textureRequests.size();
        if (done)
        {
            m_uploader->Finish();
        }
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
        WaitForTasks();
        m_uploader.reset();
        m_scene->Destroy(*m_device);
        rhi::RethrowIfDeviceUnusable(exception);
        return Fail(exception.what());
    }

    MENGINE_LOG_INFO("Uploaded %s over %u updates, longest %.1f ms", m_path.c_str(), m_updateCount, m_longestUpdateMilliseconds);
    Reset();
    return SceneLoadStatus::Finished;
}

SceneLoadProgress SceneLoader::GetProgress() const
{
    return SceneLoadProgress{
        .parsing = m_parse != nullptr,
        .texturesUploaded = m_texturesUploaded,
        .textureCount = m_data ? m_data->textureRequests.size() : 0,
    };
}

} // namespace mengine::render
