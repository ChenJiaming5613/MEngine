#pragma once

#include "core/CpuProfiler.hpp"
#include "rhi/GpuResources.hpp"

#include <glm/vec4.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mengine::rhi {
class CommandBuffer;
class GpuDevice;
} // namespace mengine::rhi

namespace mengine::render {

// - Passes run in AddPass order, so dependencies only point backwards and there are no cycles; reading a transient
//   before any write is a declaration error.
// - Passes that neither write the backbuffer nor have a side effect, directly or through a reader, are culled.
// - Transients with the same description and disjoint lifetimes share one texture (G9).
// - Load / store come from producers and consumers (G5), adjacent graphics passes with the same attachments share one
//   render pass (G10), barriers for non-attachment accesses are generated (G4).
// - Compile() runs on structural changes (start, resize, enabled flags); Execute() only walks the schedule.

struct RenderView;
struct RenderSettings;
class FrameGraph;
class FrameGraphBuilder;
class FrameGraphResources;

enum class FrameGraphNodeType : uint8_t
{
    Graphics,
    Compute,
};

struct FrameGraphResourceId
{
    uint32_t index = ~0u;
    bool IsValid() const { return index != ~0u; }
};

struct FrameGraphTextureDesc
{
    VkFormat format = VK_FORMAT_UNDEFINED;
    // 0 means the swapchain size times scale.
    uint32_t width = 0;
    uint32_t height = 0;
    float scale = 1.0f;
};

struct FrameGraphContext
{
    rhi::CommandBuffer& commandBuffer;
    const FrameGraphResources& resources;
    const RenderView& view;
};

// Vulkan render pass compatibility: pipelines built for one output work in the other when the attachment formats
// match, whatever the load / store operations and layouts.
bool IsRenderPassCompatible(const rhi::RenderPassOutput& a, const rhi::RenderPassOutput& b);

// The graph does not own passes.
class FrameGraphPass
{
public:
    virtual ~FrameGraphPass() = default;

    // Declares the resources the pass creates, imports and accesses. Called by every Compile(), also for disabled
    // passes, so it must only declare.
    virtual void Setup(FrameGraphBuilder& builder) = 0;
    // Records the pass. Graphics passes run inside their render pass, compute passes outside any.
    virtual void Execute(FrameGraphContext& context) = 0;
    // Checked every frame. An inactive pass is skipped (with its barriers); textures it writes keep their content.
    virtual bool IsActive() const { return true; }
    // Called after every Compile() that schedules the pass: textures and render passes may have been recreated.
    virtual void OnCompiled(const FrameGraphResources& resources) { (void)resources; }

    // Shown by the frame graph viewer; nullptr for none.
    virtual const char* Description() const { return nullptr; }
    // ImGui editor for the settings the pass consumes. Returns false when it has none.
    virtual bool DrawInspector(RenderSettings& settings)
    {
        (void)settings;
        return false;
    }
};

// Passed to FrameGraphPass::Setup. Resources are referenced by name; the ids stay valid until the next Compile().
class FrameGraphBuilder
{
public:
    // Owned by the graph; only available while the creating pass is enabled and valid.
    FrameGraphResourceId CreateTexture(std::string_view name, const FrameGraphTextureDesc& desc);
    // Owned by someone else; the content persists across frames. Re-importing a name with the same handle is allowed.
    FrameGraphResourceId ImportTexture(std::string_view name, rhi::TextureHandle texture);
    FrameGraphResourceId ImportBuffer(std::string_view name, rhi::BufferHandle buffer, rhi::ResourceStates initialState);
    // The current swapchain image. Passes writing it are the roots of the graph.
    FrameGraphResourceId ImportBackbuffer(std::string_view name);

    // Attachments of a graphics pass, in color slot order. Load on a texture nothing wrote this frame becomes Clear.
    FrameGraphResourceId ColorAttachment(std::string_view name, rhi::RenderPassOperation operation,
        const glm::vec4& clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    FrameGraphResourceId DepthAttachment(std::string_view name, rhi::RenderPassOperation operation, float clearDepth = 1.0f);

    // Non-attachment accesses; the graph moves the resource into `state` before the pass.
    FrameGraphResourceId Read(std::string_view name, rhi::ResourceStates state);
    // Discards the previous content: the pass does not depend on earlier writers.
    FrameGraphResourceId Write(std::string_view name, rhi::ResourceStates state);
    FrameGraphResourceId ReadWrite(std::string_view name, rhi::ResourceStates state);
    // The pass still runs when the producer is disabled; GetTexture / GetBuffer then return an invalid handle.
    FrameGraphResourceId ReadOptional(std::string_view name, rhi::ResourceStates state);

    // Keeps the pass even when nothing reads its output.
    void SetSideEffect();

private:
    friend class FrameGraph;
    FrameGraphBuilder(FrameGraph& graph, uint32_t node) : m_graph(graph), m_node(node) {}

    FrameGraph& m_graph;
    uint32_t m_node;
};

// Physical resources of one pass. Valid from OnCompiled until the next Compile().
class FrameGraphResources
{
public:
    rhi::TextureHandle GetTexture(FrameGraphResourceId id) const;
    rhi::BufferHandle GetBuffer(FrameGraphResourceId id) const;
    // For pipeline creation; empty for compute passes.
    const rhi::RenderPassOutput& GetRenderPassOutput() const;

private:
    friend class FrameGraph;
    FrameGraphResources(const FrameGraph& graph, uint32_t node) : m_graph(graph), m_node(node) {}

    const FrameGraph& m_graph;
    uint32_t m_node;
};

// Snapshot of the last Compile() for the debug panel.
struct FrameGraphDebugInfo
{
    // A resource read and written (ReadWrite, loaded attachments) has both an input and an output pin.
    struct Pin
    {
        uint32_t resource = 0; // index into resources
        bool output = false;
    };
    // From the last earlier writer to a reader (the culling dependency rule), over every node including disabled ones.
    struct Link
    {
        uint32_t fromNode = 0;
        uint32_t toNode = 0;
        uint32_t resource = 0;
        bool live = false; // both ends scheduled
    };
    struct Node
    {
        std::string name;
        FrameGraphNodeType type = FrameGraphNodeType::Graphics;
        bool enabled = true;
        bool scheduled = false;
        // "scheduled", "disabled", "culled", "skipped: ..." or "invalid: ...".
        std::string status;
        // Index into renderPasses, -1 for compute and unscheduled passes.
        int renderPass = -1;
        // Inputs first, then outputs, each in declaration order.
        std::vector<Pin> pins;
    };
    struct Resource
    {
        std::string name;
        // "transient", "texture", "buffer" or "backbuffer".
        const char* kind = "";
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t width = 0;
        uint32_t height = 0;
        // Schedule positions of the first and last access, -1 when unused.
        int firstUse = -1;
        int lastUse = -1;
        // Index of the physical texture for transients, -1 otherwise.
        int physical = -1;
    };
    struct RenderPass
    {
        std::string name;
        std::string operations;
    };

    std::vector<Node> nodes;
    std::vector<Resource> resources;
    std::vector<RenderPass> renderPasses;
    std::vector<Link> links;
    uint32_t physicalTextures = 0;
    uint32_t compileCount = 0;
};

class FrameGraph
{
public:
    void Init(rhi::GpuDevice& device);
    // Destroys the graph's textures and render passes; passes are left alone.
    void Shutdown();

    void AddPass(std::string_view name, FrameGraphNodeType type, FrameGraphPass& pass, bool enabled = true);

    void SetEnabled(std::string_view name, bool enabled);
    bool IsEnabled(std::string_view name) const;
    // Enabled, valid, not skipped or culled by the last Compile. Imported resources keep stale content while their
    // writer is not scheduled; readers that must not see it check this.
    bool IsScheduled(std::string_view name) const;
    void SetAliasingEnabled(bool enabled);
    bool IsAliasingEnabled() const { return m_aliasingEnabled; }
    // A structural change, or a swapchain size or format other than the last compile's.
    bool NeedsCompile() const;

    // Declaration errors throw with MENGINE_ENABLE_VALIDATION_LAYERS; otherwise they are logged and the offending passes
    // are skipped. Calls OnCompiled on every scheduled pass.
    void Compile();
    // Call outside a render pass. Every pass and render pass group is timed on the CPU (recording) and the GPU, inside
    // one root scope named kProfileRootName.
    void Execute(rhi::CommandBuffer& commandBuffer, const RenderView& view);
    const CpuProfiler& GetCpuProfiler() const { return m_cpuProfiler; }
    static constexpr const char* kProfileRootName = "Frame Graph";

    const FrameGraphDebugInfo& GetDebugInfo() const { return m_debugInfo; }
    // Indexed like FrameGraphDebugInfo::nodes (AddPass order); nullptr when out of range.
    FrameGraphPass* GetPass(uint32_t node) const { return node < m_nodes.size() ? m_nodes[node].pass : nullptr; }

private:
    friend class FrameGraphBuilder;
    friend class FrameGraphResources;

    static constexpr uint32_t kNone = ~0u;
    // Execute tracks a group's active members in a 64-bit mask.
    static constexpr size_t kMaxPassesPerGroup = 64;

    enum class ResourceKind : uint8_t
    {
        Unresolved,
        Transient,
        Texture,
        Buffer,
        Backbuffer,
    };

    enum class AccessKind : uint8_t
    {
        Read,
        Write,
        ReadWrite,
        ColorAttachment,
        DepthAttachment,
    };

    struct Access
    {
        uint32_t resource = kNone;
        AccessKind kind = AccessKind::Read;
        rhi::ResourceStates state = rhi::kResourceStateUndefined;
        rhi::RenderPassOperation operation = rhi::RenderPassOperation::Load;
        glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
        float clearDepth = 1.0f;
        bool optional = false;
    };

    struct Resource
    {
        std::string name;
        ResourceKind kind = ResourceKind::Unresolved;
        FrameGraphTextureDesc desc;
        rhi::TextureHandle texture; // imported
        rhi::BufferHandle buffer;
        rhi::ResourceStates initialBufferState = rhi::kResourceStateUndefined;
        uint32_t owner = kNone; // node that created or first imported it

        // Compile results.
        bool importDestroyed = false;
        bool available = false;
        uint32_t firstUse = kNone;
        uint32_t lastUse = kNone;
        uint8_t textureFlags = rhi::kTextureFlagNone;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t physical = kNone;
    };

    struct Node
    {
        std::string name;
        FrameGraphNodeType type = FrameGraphNodeType::Graphics;
        FrameGraphPass* pass = nullptr;
        bool enabled = true;

        // Compile results.
        std::vector<Access> accesses;
        bool sideEffect = false;
        bool valid = true;
        bool active = false;
        bool scheduled = false;
        std::string status;
        uint32_t renderPass = kNone;
    };

    struct PhysicalTexture
    {
        rhi::TextureHandle handle;
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t width = 0;
        uint32_t height = 0;
        uint8_t flags = rhi::kTextureFlagNone;
        std::string name;
    };

    struct RenderPassGroup
    {
        std::string name;
        std::vector<uint32_t> nodes;
        std::vector<uint32_t> colors; // resources, in slot order
        uint32_t depth = kNone;
        rhi::RenderPassHandle handle;
        bool swapchain = false;
        rhi::RenderPassOutput output;
        std::vector<glm::vec4> clearColors;
        float clearDepth = 1.0f;
    };

    // One schedule entry is a compute pass or a render pass group.
    struct ScheduleEntry
    {
        uint32_t node = kNone;
        uint32_t group = kNone;
    };

    uint32_t FindOrAddResource(std::string_view name);
    FrameGraphResourceId AddAccess(uint32_t node, std::string_view name, const Access& access);
    void DeclareError(uint32_t node, const std::string& message);
    uint32_t FindNode(std::string_view name) const;
    rhi::TextureHandle TextureOf(uint32_t resource) const;

    void ReleaseResources();
    void ValidateDeclarations();
    void ResolveAvailability();
    // False after invalidating a pass; availability must then be resolved again.
    bool CheckReadsBeforeWrites();
    void CullAndSchedule();
    void ComputeLifetimes();
    void CreateTextures();
    void BuildRenderPasses();
    void BuildDebugInfo();

    void ExecuteSchedule(rhi::CommandBuffer& commandBuffer, const RenderView& view);
    void RecordBarriers(rhi::CommandBuffer& commandBuffer, const Node& node);

    rhi::GpuDevice* m_device = nullptr;
    CpuProfiler m_cpuProfiler;
    std::vector<Node> m_nodes;
    std::vector<Resource> m_resources;
    std::unordered_map<std::string, uint32_t> m_resourceNames;
    std::vector<PhysicalTexture> m_physicalTextures;
    std::vector<RenderPassGroup> m_groups;
    std::vector<ScheduleEntry> m_schedule;
    std::vector<uint32_t> m_order; // scheduled nodes in execution order
    std::vector<std::string> m_errors;
    // Last state of every imported buffer, kept across frames and compiles (the RHI does not track buffers).
    std::unordered_map<rhi::BufferHandle, rhi::ResourceStates> m_bufferStates;
    VkExtent2D m_extent{};
    VkFormat m_backbufferFormat = VK_FORMAT_UNDEFINED;
    bool m_aliasingEnabled = true;
    bool m_dirty = true;
    FrameGraphDebugInfo m_debugInfo;
};

} // namespace mengine::render
