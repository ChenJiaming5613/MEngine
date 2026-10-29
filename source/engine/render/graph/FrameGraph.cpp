#include "render/graph/FrameGraph.hpp"

#include "core/Assert.hpp"
#include "core/Log.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/GpuDevice.hpp"
#include "rhi/VulkanUtils.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace mengine::render {

namespace {

using rhi::RenderPassOperation;

bool IsReadOnly(rhi::ResourceStates states)
{
    return (states & rhi::kResourceStateWriteMask) == 0;
}

const char* OperationName(RenderPassOperation operation)
{
    switch (operation)
    {
    case RenderPassOperation::DontCare:
        return "DontCare";
    case RenderPassOperation::Load:
        return "Load";
    case RenderPassOperation::Clear:
        return "Clear";
    }
    return "?";
}

const rhi::RenderPassOutput kNoOutput{};

} // namespace

bool IsRenderPassCompatible(const rhi::RenderPassOutput& a, const rhi::RenderPassOutput& b)
{
    if (a.colorCount != b.colorCount || a.depthFormat != b.depthFormat)
    {
        return false;
    }
    for (uint32_t index = 0; index < a.colorCount; ++index)
    {
        if (a.colorFormats[index] != b.colorFormats[index])
        {
            return false;
        }
    }
    return true;
}

FrameGraphResourceId FrameGraphBuilder::CreateTexture(std::string_view name, const FrameGraphTextureDesc& desc)
{
    const uint32_t index = m_graph.FindOrAddResource(name);
    FrameGraph::Resource& resource = m_graph.m_resources[index];
    if (resource.kind != FrameGraph::ResourceKind::Unresolved)
    {
        m_graph.DeclareError(m_node, "creates '" + resource.name + "', which is already declared");
        return {index};
    }
    if (desc.format == VK_FORMAT_UNDEFINED || (desc.width == 0) != (desc.height == 0) || (desc.width == 0 && desc.scale <= 0.0f))
    {
        m_graph.DeclareError(m_node, "invalid description for '" + resource.name + "'");
    }
    resource.kind = FrameGraph::ResourceKind::Transient;
    resource.desc = desc;
    resource.owner = m_node;
    return {index};
}

FrameGraphResourceId FrameGraphBuilder::ImportTexture(std::string_view name, rhi::TextureHandle texture)
{
    const uint32_t index = m_graph.FindOrAddResource(name);
    FrameGraph::Resource& resource = m_graph.m_resources[index];
    if (resource.kind == FrameGraph::ResourceKind::Texture && resource.texture == texture)
    {
        return {index};
    }
    if (resource.kind != FrameGraph::ResourceKind::Unresolved || !texture.IsValid())
    {
        m_graph.DeclareError(m_node, "cannot import texture '" + resource.name + "'");
        return {index};
    }
    resource.kind = FrameGraph::ResourceKind::Texture;
    resource.texture = texture;
    resource.owner = m_node;
    return {index};
}

FrameGraphResourceId FrameGraphBuilder::ImportBuffer(std::string_view name, rhi::BufferHandle buffer, rhi::ResourceStates initialState)
{
    const uint32_t index = m_graph.FindOrAddResource(name);
    FrameGraph::Resource& resource = m_graph.m_resources[index];
    if (resource.kind == FrameGraph::ResourceKind::Buffer && resource.buffer == buffer)
    {
        return {index};
    }
    if (resource.kind != FrameGraph::ResourceKind::Unresolved || !buffer.IsValid())
    {
        m_graph.DeclareError(m_node, "cannot import buffer '" + resource.name + "'");
        return {index};
    }
    resource.kind = FrameGraph::ResourceKind::Buffer;
    resource.buffer = buffer;
    resource.initialBufferState = initialState;
    resource.owner = m_node;
    return {index};
}

FrameGraphResourceId FrameGraphBuilder::ImportBackbuffer(std::string_view name)
{
    const uint32_t index = m_graph.FindOrAddResource(name);
    FrameGraph::Resource& resource = m_graph.m_resources[index];
    if (resource.kind == FrameGraph::ResourceKind::Backbuffer)
    {
        return {index};
    }
    if (resource.kind != FrameGraph::ResourceKind::Unresolved)
    {
        m_graph.DeclareError(m_node, "cannot import backbuffer '" + resource.name + "'");
        return {index};
    }
    resource.kind = FrameGraph::ResourceKind::Backbuffer;
    resource.owner = m_node;
    return {index};
}

FrameGraphResourceId FrameGraphBuilder::ColorAttachment(std::string_view name, rhi::RenderPassOperation operation,
    const glm::vec4& clearColor)
{
    return m_graph.AddAccess(m_node, name,
        {.kind = FrameGraph::AccessKind::ColorAttachment, .state = rhi::kResourceStateRenderTarget, .operation = operation,
            .clearColor = clearColor});
}

FrameGraphResourceId FrameGraphBuilder::DepthAttachment(std::string_view name, rhi::RenderPassOperation operation, float clearDepth)
{
    return m_graph.AddAccess(m_node, name,
        {.kind = FrameGraph::AccessKind::DepthAttachment, .state = rhi::kResourceStateDepthWrite, .operation = operation,
            .clearDepth = clearDepth});
}

FrameGraphResourceId FrameGraphBuilder::Read(std::string_view name, rhi::ResourceStates state)
{
    return m_graph.AddAccess(m_node, name, {.kind = FrameGraph::AccessKind::Read, .state = state});
}

FrameGraphResourceId FrameGraphBuilder::Write(std::string_view name, rhi::ResourceStates state)
{
    return m_graph.AddAccess(m_node, name, {.kind = FrameGraph::AccessKind::Write, .state = state});
}

FrameGraphResourceId FrameGraphBuilder::ReadWrite(std::string_view name, rhi::ResourceStates state)
{
    return m_graph.AddAccess(m_node, name, {.kind = FrameGraph::AccessKind::ReadWrite, .state = state});
}

FrameGraphResourceId FrameGraphBuilder::ReadOptional(std::string_view name, rhi::ResourceStates state)
{
    return m_graph.AddAccess(m_node, name, {.kind = FrameGraph::AccessKind::Read, .state = state, .optional = true});
}

void FrameGraphBuilder::SetSideEffect()
{
    m_graph.m_nodes[m_node].sideEffect = true;
}

rhi::TextureHandle FrameGraphResources::GetTexture(FrameGraphResourceId id) const
{
    return id.index < m_graph.m_resources.size() ? m_graph.TextureOf(id.index) : rhi::TextureHandle{};
}

rhi::BufferHandle FrameGraphResources::GetBuffer(FrameGraphResourceId id) const
{
    if (id.index >= m_graph.m_resources.size())
    {
        return {};
    }
    const FrameGraph::Resource& resource = m_graph.m_resources[id.index];
    return resource.kind == FrameGraph::ResourceKind::Buffer && resource.available ? resource.buffer : rhi::BufferHandle{};
}

const rhi::RenderPassOutput& FrameGraphResources::GetRenderPassOutput() const
{
    const uint32_t group = m_graph.m_nodes[m_node].renderPass;
    return group < m_graph.m_groups.size() ? m_graph.m_groups[group].output : kNoOutput;
}

void FrameGraph::Init(rhi::GpuDevice& device)
{
    m_device = &device;
}

void FrameGraph::Shutdown()
{
    if (m_device == nullptr)
    {
        return;
    }
    ReleaseResources();
    *this = FrameGraph{};
}

void FrameGraph::AddPass(std::string_view name, FrameGraphNodeType type, FrameGraphPass& pass, bool enabled)
{
    MENGINE_ASSERT(FindNode(name) == kNone);
    Node node;
    node.name = std::string(name);
    node.type = type;
    node.pass = &pass;
    node.enabled = enabled;
    m_nodes.push_back(std::move(node));
    m_dirty = true;
}

void FrameGraph::SetEnabled(std::string_view name, bool enabled)
{
    const uint32_t node = FindNode(name);
    if (node != kNone && m_nodes[node].enabled != enabled)
    {
        m_nodes[node].enabled = enabled;
        m_dirty = true;
    }
}

bool FrameGraph::IsEnabled(std::string_view name) const
{
    const uint32_t node = FindNode(name);
    return node != kNone && m_nodes[node].enabled;
}

bool FrameGraph::IsScheduled(std::string_view name) const
{
    const uint32_t node = FindNode(name);
    return node != kNone && !m_dirty && m_nodes[node].scheduled;
}

void FrameGraph::SetAliasingEnabled(bool enabled)
{
    if (m_aliasingEnabled != enabled)
    {
        m_aliasingEnabled = enabled;
        m_dirty = true;
    }
}

bool FrameGraph::NeedsCompile() const
{
    if (m_dirty)
    {
        return true;
    }
    const VkExtent2D extent = m_device->GetSwapchainExtent();
    return std::max(extent.width, 1u) != m_extent.width || std::max(extent.height, 1u) != m_extent.height
        || m_device->GetSwapchainOutput().colorFormats[0] != m_backbufferFormat;
}

uint32_t FrameGraph::FindNode(std::string_view name) const
{
    for (uint32_t index = 0; index < m_nodes.size(); ++index)
    {
        if (m_nodes[index].name == name)
        {
            return index;
        }
    }
    return kNone;
}

uint32_t FrameGraph::FindOrAddResource(std::string_view name)
{
    const std::string key(name);
    if (const auto found = m_resourceNames.find(key); found != m_resourceNames.end())
    {
        return found->second;
    }
    const auto index = static_cast<uint32_t>(m_resources.size());
    m_resources.push_back(Resource{.name = key});
    m_resourceNames.emplace(key, index);
    return index;
}

FrameGraphResourceId FrameGraph::AddAccess(uint32_t node, std::string_view name, const Access& access)
{
    Access entry = access;
    entry.resource = FindOrAddResource(name);
    m_nodes[node].accesses.push_back(entry);
    return {entry.resource};
}

void FrameGraph::DeclareError(uint32_t node, const std::string& message)
{
    Node& entry = m_nodes[node];
    m_errors.push_back("pass '" + entry.name + "' " + message);
    if (entry.valid)
    {
        entry.valid = false;
        entry.status = "invalid: " + message;
    }
}

rhi::TextureHandle FrameGraph::TextureOf(uint32_t resource) const
{
    const Resource& entry = m_resources[resource];
    if (!entry.available)
    {
        return {};
    }
    switch (entry.kind)
    {
    case ResourceKind::Transient:
        return entry.physical < m_physicalTextures.size() ? m_physicalTextures[entry.physical].handle : rhi::TextureHandle{};
    case ResourceKind::Texture:
        return entry.texture;
    default:
        return {};
    }
}

void FrameGraph::ReleaseResources()
{
    for (const PhysicalTexture& texture : m_physicalTextures)
    {
        m_device->DestroyTexture(texture.handle);
    }
    for (const RenderPassGroup& group : m_groups)
    {
        if (!group.swapchain)
        {
            m_device->DestroyRenderPass(group.handle);
        }
    }
    m_physicalTextures.clear();
    m_groups.clear();
    m_schedule.clear();
    m_order.clear();
}

void FrameGraph::Compile()
{
    MENGINE_ASSERT(m_device != nullptr);
    // The previous textures and render passes go through deferred destruction: frames in flight may still use them.
    ReleaseResources();
    m_errors.clear();
    m_resources.clear();
    m_resourceNames.clear();

    const VkExtent2D extent = m_device->GetSwapchainExtent();
    m_extent = {std::max(extent.width, 1u), std::max(extent.height, 1u)};
    m_backbufferFormat = m_device->GetSwapchainOutput().colorFormats[0];

    for (uint32_t index = 0; index < m_nodes.size(); ++index)
    {
        Node& node = m_nodes[index];
        node.accesses.clear();
        node.sideEffect = false;
        node.valid = true;
        node.active = false;
        node.scheduled = false;
        node.status.clear();
        node.renderPass = kNone;
        FrameGraphBuilder builder(*this, index);
        node.pass->Setup(builder);
    }

    ValidateDeclarations();
    ResolveAvailability();
    // A read before any write invalidates the pass, which may make more resources unavailable.
    while (!CheckReadsBeforeWrites())
    {
        ResolveAvailability();
    }
    CullAndSchedule();

    if (!m_errors.empty())
    {
        std::string message = "FrameGraph: invalid declarations";
        for (const std::string& error : m_errors)
        {
            message += "\n  " + error;
        }
#ifdef MENGINE_ENABLE_VALIDATION_LAYERS
        throw std::runtime_error(message);
#else
        MENGINE_LOG_ERROR("%s", message.c_str());
#endif
    }

    ComputeLifetimes();
    CreateTextures();
    BuildRenderPasses();
    BuildDebugInfo();
    m_dirty = false;

    uint32_t culled = 0;
    uint32_t skipped = 0;
    uint32_t transients = 0;
    for (const Node& node : m_nodes)
    {
        culled += node.active && !node.scheduled ? 1u : 0u;
        skipped += !node.active ? 1u : 0u;
    }
    for (const Resource& resource : m_resources)
    {
        transients += resource.kind == ResourceKind::Transient && resource.physical != kNone ? 1u : 0u;
    }
    MENGINE_LOG_INFO("FrameGraph: %zu passes scheduled, %u culled, %u disabled or skipped; %zu render passes; %zu textures "
                     "for %u transients (%ux%u)",
        m_order.size(), culled, skipped, m_groups.size(), m_physicalTextures.size(), transients, m_extent.width,
        m_extent.height);

    for (const uint32_t node : m_order)
    {
        const FrameGraphResources resources(*this, node);
        m_nodes[node].pass->OnCompiled(resources);
    }
}

void FrameGraph::ValidateDeclarations()
{
    const rhi::RenderPassOutput& swapchainOutput = m_device->GetSwapchainOutput();
    for (Resource& resource : m_resources)
    {
        switch (resource.kind)
        {
        case ResourceKind::Transient:
            if (resource.desc.width != 0)
            {
                resource.width = resource.desc.width;
                resource.height = resource.desc.height;
            }
            else
            {
                resource.width = std::max(1u, static_cast<uint32_t>(std::lround(m_extent.width * resource.desc.scale)));
                resource.height = std::max(1u, static_cast<uint32_t>(std::lround(m_extent.height * resource.desc.scale)));
            }
            break;
        case ResourceKind::Texture:
            if (const rhi::Texture* texture = m_device->AccessTexture(resource.texture))
            {
                resource.desc.format = texture->format;
                resource.width = texture->width;
                resource.height = texture->height;
            }
            else
            {
                DeclareError(resource.owner, "imports a destroyed texture as '" + resource.name + "'");
            }
            break;
        case ResourceKind::Backbuffer:
            resource.desc.format = swapchainOutput.colorFormats[0];
            resource.width = m_extent.width;
            resource.height = m_extent.height;
            break;
        default:
            break;
        }
    }

    for (uint32_t index = 0; index < m_nodes.size(); ++index)
    {
        const Node& node = m_nodes[index];
        uint32_t colorCount = 0;
        uint32_t depthCount = 0;
        bool hasBackbuffer = false;
        uint32_t width = 0;
        uint32_t height = 0;
        for (size_t accessIndex = 0; accessIndex < node.accesses.size(); ++accessIndex)
        {
            const Access& access = node.accesses[accessIndex];
            const Resource& resource = m_resources[access.resource];
            for (size_t other = 0; other < accessIndex; ++other)
            {
                if (node.accesses[other].resource == access.resource)
                {
                    DeclareError(index, "accesses '" + resource.name + "' twice");
                }
            }
            if (resource.kind == ResourceKind::Unresolved)
            {
                DeclareError(index, "uses '" + resource.name + "', which no pass creates or imports");
                continue;
            }

            const bool attachment = access.kind == AccessKind::ColorAttachment || access.kind == AccessKind::DepthAttachment;
            if (attachment)
            {
                if (node.type != FrameGraphNodeType::Graphics)
                {
                    DeclareError(index, "is a compute pass but attaches '" + resource.name + "'");
                    continue;
                }
                if (resource.kind == ResourceKind::Buffer)
                {
                    DeclareError(index, "attaches buffer '" + resource.name + "'");
                    continue;
                }
                const bool depthFormat = rhi::IsDepthFormat(resource.desc.format);
                if ((access.kind == AccessKind::DepthAttachment) != depthFormat)
                {
                    DeclareError(index, "attaches '" + resource.name + "' with a format that does not match the slot");
                }
                colorCount += access.kind == AccessKind::ColorAttachment ? 1u : 0u;
                depthCount += access.kind == AccessKind::DepthAttachment ? 1u : 0u;
                hasBackbuffer = hasBackbuffer || resource.kind == ResourceKind::Backbuffer;
                if (width == 0)
                {
                    width = resource.width;
                    height = resource.height;
                }
                else if (resource.width != width || resource.height != height)
                {
                    DeclareError(index, "attaches textures of different sizes");
                }
                continue;
            }

            if (resource.kind == ResourceKind::Backbuffer)
            {
                DeclareError(index, "uses backbuffer '" + resource.name + "' other than as a color attachment");
            }
            if (access.state == rhi::kResourceStateUndefined)
            {
                DeclareError(index, "accesses '" + resource.name + "' without a state");
            }
            if (access.kind == AccessKind::Read && !IsReadOnly(access.state))
            {
                DeclareError(index, "reads '" + resource.name + "' with a writing state");
            }
            if (resource.kind == ResourceKind::Transient && (access.state & rhi::kResourceStateUnorderedAccess) != 0
                && !m_device->SupportsStorageImage(resource.desc.format))
            {
                DeclareError(index, "needs '" + resource.name + "' as a storage image, which its format does not support");
            }
        }

        if (node.type == FrameGraphNodeType::Graphics && colorCount + depthCount == 0)
        {
            DeclareError(index, "is a graphics pass without attachments");
        }
        if (colorCount > rhi::kMaxImageOutputs || depthCount > 1)
        {
            DeclareError(index, "has too many attachments");
        }
        if (hasBackbuffer && (colorCount != 1 || depthCount != 0))
        {
            DeclareError(index, "must attach the backbuffer alone");
        }
    }
}

void FrameGraph::ResolveAvailability()
{
    for (Node& node : m_nodes)
    {
        node.active = node.enabled && node.valid;
        if (!node.enabled && node.valid)
        {
            node.status = "disabled";
        }
    }

    // Disabling a pass removes the transients it creates, and with them every pass that needs them.
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (Resource& resource : m_resources)
        {
            resource.available = resource.kind == ResourceKind::Transient
                ? resource.owner != kNone && m_nodes[resource.owner].active
                : resource.kind != ResourceKind::Unresolved;
        }
        for (Node& node : m_nodes)
        {
            if (!node.active)
            {
                continue;
            }
            for (const Access& access : node.accesses)
            {
                const Resource& resource = m_resources[access.resource];
                if (!resource.available && !access.optional)
                {
                    node.active = false;
                    node.status = "skipped: needs '" + resource.name + "' of disabled pass '"
                        + (resource.owner != kNone ? m_nodes[resource.owner].name : std::string("?")) + "'";
                    changed = true;
                    break;
                }
            }
        }
    }
}

bool FrameGraph::CheckReadsBeforeWrites()
{
    std::vector<bool> written(m_resources.size(), false);
    for (uint32_t index = 0; index < m_nodes.size(); ++index)
    {
        Node& node = m_nodes[index];
        if (!node.active)
        {
            continue;
        }
        for (Access& access : node.accesses)
        {
            const Resource& resource = m_resources[access.resource];
            if (!resource.available || resource.kind == ResourceKind::Texture || resource.kind == ResourceKind::Buffer)
            {
                continue; // imported content persists across frames
            }
            if (written[access.resource])
            {
                continue;
            }
            const bool attachment = access.kind == AccessKind::ColorAttachment || access.kind == AccessKind::DepthAttachment;
            if (attachment && access.operation == RenderPassOperation::Load)
            {
                // Nothing to load: the first writer this frame clears instead.
                access.operation = RenderPassOperation::Clear;
            }
            else if (access.kind == AccessKind::Read || access.kind == AccessKind::ReadWrite)
            {
                DeclareError(index, "reads '" + resource.name + "' before any pass writes it");
                node.active = false;
                return false;
            }
        }
        for (const Access& access : node.accesses)
        {
            if (access.kind != AccessKind::Read)
            {
                written[access.resource] = true;
            }
        }
    }
    return true;
}

void FrameGraph::CullAndSchedule()
{
    // Dependencies point at the last earlier writer of everything a pass reads (including attachments it loads).
    std::vector<uint32_t> lastWriter(m_resources.size(), kNone);
    std::vector<std::vector<uint32_t>> dependencies(m_nodes.size());
    std::vector<uint32_t> stack;
    for (uint32_t index = 0; index < m_nodes.size(); ++index)
    {
        const Node& node = m_nodes[index];
        if (!node.active)
        {
            continue;
        }
        bool root = node.sideEffect;
        for (const Access& access : node.accesses)
        {
            const bool attachment = access.kind == AccessKind::ColorAttachment || access.kind == AccessKind::DepthAttachment;
            const bool reads = access.kind == AccessKind::Read || access.kind == AccessKind::ReadWrite
                || (attachment && access.operation == RenderPassOperation::Load);
            if (reads && lastWriter[access.resource] != kNone)
            {
                dependencies[index].push_back(lastWriter[access.resource]);
            }
            root = root || (attachment && m_resources[access.resource].kind == ResourceKind::Backbuffer);
        }
        for (const Access& access : node.accesses)
        {
            if (access.kind != AccessKind::Read)
            {
                lastWriter[access.resource] = index;
            }
        }
        if (root)
        {
            stack.push_back(index);
        }
    }

    while (!stack.empty())
    {
        const uint32_t index = stack.back();
        stack.pop_back();
        if (m_nodes[index].scheduled)
        {
            continue;
        }
        m_nodes[index].scheduled = true;
        for (const uint32_t dependency : dependencies[index])
        {
            stack.push_back(dependency);
        }
    }

    for (uint32_t index = 0; index < m_nodes.size(); ++index)
    {
        Node& node = m_nodes[index];
        if (node.scheduled)
        {
            node.status = "scheduled";
            m_order.push_back(index);
        }
        else if (node.active)
        {
            node.status = "culled";
        }
    }

    // Adjacent graphics passes share one render pass when they have the same attachments, the later ones load all of
    // them, none uses an attachment otherwise, and their other accesses do not conflict (only read-read).
    auto attachmentsOf = [](const Node& node, std::vector<uint32_t>& colors, uint32_t& depth) {
        colors.clear();
        depth = kNone;
        for (const Access& access : node.accesses)
        {
            if (access.kind == AccessKind::ColorAttachment)
            {
                colors.push_back(access.resource);
            }
            else if (access.kind == AccessKind::DepthAttachment)
            {
                depth = access.resource;
            }
        }
    };
    auto canMerge = [this](const RenderPassGroup& group, const Node& node, const std::vector<uint32_t>& colors, uint32_t depth) {
        if (colors != group.colors || depth != group.depth)
        {
            return false;
        }
        for (const Access& access : node.accesses)
        {
            const bool attachment = access.kind == AccessKind::ColorAttachment || access.kind == AccessKind::DepthAttachment;
            if (attachment)
            {
                if (access.operation != RenderPassOperation::Load)
                {
                    return false;
                }
                continue;
            }
            if (access.resource == group.depth
                || std::find(group.colors.begin(), group.colors.end(), access.resource) != group.colors.end())
            {
                return false;
            }
            for (const uint32_t member : group.nodes)
            {
                for (const Access& other : m_nodes[member].accesses)
                {
                    const bool otherWrites = other.kind == AccessKind::Write || other.kind == AccessKind::ReadWrite;
                    if (other.resource == access.resource && (otherWrites || access.kind != AccessKind::Read))
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    };

    std::vector<uint32_t> colors;
    uint32_t depth = kNone;
    for (const uint32_t index : m_order)
    {
        Node& node = m_nodes[index];
        if (node.type == FrameGraphNodeType::Compute)
        {
            m_schedule.push_back({.node = index});
            continue;
        }
        attachmentsOf(node, colors, depth);
        if (!m_schedule.empty() && m_schedule.back().group != kNone && canMerge(m_groups[m_schedule.back().group], node, colors, depth))
        {
            RenderPassGroup& group = m_groups[m_schedule.back().group];
            group.nodes.push_back(index);
            group.name += "+" + node.name;
            node.renderPass = m_schedule.back().group;
            continue;
        }

        RenderPassGroup group;
        group.name = node.name;
        group.nodes.push_back(index);
        group.colors = colors;
        group.depth = depth;
        group.swapchain = colors.size() == 1 && m_resources[colors[0]].kind == ResourceKind::Backbuffer;
        node.renderPass = static_cast<uint32_t>(m_groups.size());
        m_schedule.push_back({.group = node.renderPass});
        m_groups.push_back(std::move(group));
    }
}

void FrameGraph::ComputeLifetimes()
{
    // A render pass keeps every resource its passes touch alive from its first to its last pass, so aliasing never
    // hands a texture to two passes of the same render pass.
    std::vector<uint32_t> rangeBegin(m_order.size());
    std::vector<uint32_t> rangeEnd(m_order.size());
    for (uint32_t position = 0; position < m_order.size(); ++position)
    {
        rangeBegin[position] = position;
        rangeEnd[position] = position;
        const uint32_t group = m_nodes[m_order[position]].renderPass;
        if (group != kNone)
        {
            const std::vector<uint32_t>& nodes = m_groups[group].nodes;
            const auto first = std::find(m_order.begin(), m_order.end(), nodes.front());
            rangeBegin[position] = static_cast<uint32_t>(first - m_order.begin());
            rangeEnd[position] = rangeBegin[position] + static_cast<uint32_t>(nodes.size()) - 1;
        }
    }

    for (uint32_t position = 0; position < m_order.size(); ++position)
    {
        for (const Access& access : m_nodes[m_order[position]].accesses)
        {
            Resource& resource = m_resources[access.resource];
            if (!resource.available)
            {
                continue;
            }
            resource.firstUse = std::min(resource.firstUse == kNone ? rangeBegin[position] : resource.firstUse, rangeBegin[position]);
            resource.lastUse = resource.lastUse == kNone ? rangeEnd[position] : std::max(resource.lastUse, rangeEnd[position]);
            if (access.kind == AccessKind::ColorAttachment || access.kind == AccessKind::DepthAttachment)
            {
                resource.textureFlags |= rhi::kTextureFlagRenderTarget;
            }
            if ((access.state & rhi::kResourceStateUnorderedAccess) != 0)
            {
                resource.textureFlags |= rhi::kTextureFlagStorage;
            }
        }
    }
}

void FrameGraph::CreateTextures()
{
    std::vector<uint32_t> freeTextures;
    for (uint32_t position = 0; position < m_order.size(); ++position)
    {
        for (Resource& resource : m_resources)
        {
            if (resource.kind != ResourceKind::Transient || resource.firstUse != position)
            {
                continue;
            }
            auto reusable = freeTextures.end();
            if (m_aliasingEnabled)
            {
                reusable = std::find_if(freeTextures.begin(), freeTextures.end(), [&](uint32_t candidate) {
                    const PhysicalTexture& texture = m_physicalTextures[candidate];
                    return texture.format == resource.desc.format && texture.width == resource.width
                        && texture.height == resource.height && texture.flags == resource.textureFlags;
                });
            }
            if (reusable != freeTextures.end())
            {
                resource.physical = *reusable;
                freeTextures.erase(reusable);
                m_physicalTextures[resource.physical].name += "|" + resource.name;
                continue;
            }

            PhysicalTexture texture;
            texture.format = resource.desc.format;
            texture.width = resource.width;
            texture.height = resource.height;
            texture.flags = resource.textureFlags;
            texture.name = resource.name;
            texture.handle = m_device->CreateTexture({
                .width = texture.width,
                .height = texture.height,
                .format = texture.format,
                .flags = texture.flags,
                .name = resource.name.c_str(),
            });
            resource.physical = static_cast<uint32_t>(m_physicalTextures.size());
            m_physicalTextures.push_back(std::move(texture));
        }
        // Released after the allocations of the same position, so a pass never gets one of its own inputs.
        for (const Resource& resource : m_resources)
        {
            if (resource.kind == ResourceKind::Transient && resource.lastUse == position && resource.physical != kNone)
            {
                freeTextures.push_back(resource.physical);
            }
        }
    }
}

void FrameGraph::BuildRenderPasses()
{
    for (RenderPassGroup& group : m_groups)
    {
        const Node& first = m_nodes[group.nodes.front()];
        for (const Access& access : first.accesses)
        {
            if (access.kind == AccessKind::ColorAttachment)
            {
                group.clearColors.push_back(access.clearColor);
            }
            else if (access.kind == AccessKind::DepthAttachment)
            {
                group.clearDepth = access.clearDepth;
            }
        }

        if (group.swapchain)
        {
            group.handle = m_device->GetSwapchainPass();
            group.output = m_device->GetSwapchainOutput();
            continue;
        }

        rhi::RenderPassCreation creation;
        creation.name = group.name.c_str();
        RenderPassOperation colorOperation = RenderPassOperation::DontCare;
        RenderPassOperation depthOperation = RenderPassOperation::DontCare;
        bool firstColor = true;
        for (const Access& access : first.accesses)
        {
            if (access.kind == AccessKind::ColorAttachment)
            {
                if (!firstColor && access.operation != colorOperation)
                {
                    MENGINE_LOG_WARN("FrameGraph: render pass '%s' applies %s to every color attachment", group.name.c_str(),
                        OperationName(colorOperation));
                }
                colorOperation = firstColor ? access.operation : colorOperation;
                firstColor = false;
            }
            else if (access.kind == AccessKind::DepthAttachment)
            {
                depthOperation = access.operation;
            }
        }
        for (const uint32_t color : group.colors)
        {
            creation.AddRenderTexture(TextureOf(color));
        }
        if (group.depth != kNone)
        {
            creation.SetDepthStencilTexture(TextureOf(group.depth));
            // Stored only for a later reader (sampled: shader read only layout) or when the depth outlives the frame.
            const Resource& depth = m_resources[group.depth];
            creation.storeDepth = depth.kind == ResourceKind::Texture;
            const auto groupEnd = std::find(m_order.begin(), m_order.end(), group.nodes.back());
            for (auto later = groupEnd + 1; later < m_order.end(); ++later)
            {
                for (const Access& access : m_nodes[*later].accesses)
                {
                    if (access.resource != group.depth)
                    {
                        continue;
                    }
                    creation.storeDepth = true;
                    if (access.kind != AccessKind::DepthAttachment && (access.state & rhi::kResourceStateShaderResource) != 0)
                    {
                        creation.sampleDepth = true;
                    }
                }
            }
        }
        creation.SetOperations(colorOperation, depthOperation);
        group.handle = m_device->CreateRenderPass(creation);
        group.output = m_device->AccessRenderPass(group.handle)->output;
    }
}

void FrameGraph::BuildDebugInfo()
{
    FrameGraphDebugInfo info;
    info.compileCount = m_debugInfo.compileCount + 1;
    info.physicalTextures = static_cast<uint32_t>(m_physicalTextures.size());
    for (const Node& node : m_nodes)
    {
        info.nodes.push_back({
            .name = node.name,
            .type = node.type,
            .enabled = node.enabled,
            .scheduled = node.scheduled,
            .status = node.status,
            .renderPass = node.renderPass == kNone ? -1 : static_cast<int>(node.renderPass),
        });
    }

    // Same read / write classification as CullAndSchedule. Links go to the last earlier writer of any state so disabled
    // passes stay connected; when that writer is inactive, the last active writer, which really provides the data, gets
    // a link too.
    std::vector<uint32_t> lastWriter(m_resources.size(), kNone);
    std::vector<uint32_t> lastActiveWriter(m_resources.size(), kNone);
    auto addLink = [&](uint32_t from, uint32_t to, uint32_t resource) {
        for (const FrameGraphDebugInfo::Link& link : info.links)
        {
            if (link.fromNode == from && link.toNode == to && link.resource == resource)
            {
                return;
            }
        }
        info.links.push_back({
            .fromNode = from,
            .toNode = to,
            .resource = resource,
            .live = m_nodes[from].scheduled && m_nodes[to].scheduled,
        });
    };
    for (uint32_t index = 0; index < m_nodes.size(); ++index)
    {
        const Node& node = m_nodes[index];
        std::vector<FrameGraphDebugInfo::Pin>& pins = info.nodes[index].pins;
        auto addPin = [&pins](uint32_t resource, bool output) {
            for (const FrameGraphDebugInfo::Pin& pin : pins)
            {
                if (pin.resource == resource && pin.output == output)
                {
                    return;
                }
            }
            pins.push_back({.resource = resource, .output = output});
        };

        for (const Access& access : node.accesses)
        {
            const bool attachment = access.kind == AccessKind::ColorAttachment || access.kind == AccessKind::DepthAttachment;
            const bool reads = access.kind == AccessKind::Read || access.kind == AccessKind::ReadWrite
                || (attachment && access.operation == RenderPassOperation::Load);
            if (!reads)
            {
                continue;
            }
            addPin(access.resource, false);
            const uint32_t writer = lastWriter[access.resource];
            if (writer != kNone)
            {
                addLink(writer, index, access.resource);
                const uint32_t activeWriter = lastActiveWriter[access.resource];
                if (!m_nodes[writer].active && activeWriter != kNone)
                {
                    addLink(activeWriter, index, access.resource);
                }
            }
        }
        for (const Access& access : node.accesses)
        {
            if (access.kind == AccessKind::Read)
            {
                continue;
            }
            addPin(access.resource, true);
            lastWriter[access.resource] = index;
            if (node.active)
            {
                lastActiveWriter[access.resource] = index;
            }
        }
    }

    for (const Resource& resource : m_resources)
    {
        const char* kind = "unresolved";
        switch (resource.kind)
        {
        case ResourceKind::Transient:
            kind = "transient";
            break;
        case ResourceKind::Texture:
            kind = "texture";
            break;
        case ResourceKind::Buffer:
            kind = "buffer";
            break;
        case ResourceKind::Backbuffer:
            kind = "backbuffer";
            break;
        default:
            break;
        }
        info.resources.push_back({
            .name = resource.name,
            .kind = kind,
            .format = resource.desc.format,
            .width = resource.width,
            .height = resource.height,
            .firstUse = resource.firstUse == kNone ? -1 : static_cast<int>(resource.firstUse),
            .lastUse = resource.lastUse == kNone ? -1 : static_cast<int>(resource.lastUse),
            .physical = resource.physical == kNone ? -1 : static_cast<int>(resource.physical),
        });
        FrameGraphDebugInfo::Resource& entry = info.resources.back();
        if (resource.kind == ResourceKind::Texture)
        {
            if (const rhi::Texture* texture = m_device->AccessTexture(resource.texture))
            {
                entry.format = texture->format;
                entry.width = texture->width;
                entry.height = texture->height;
            }
        }
        else if (resource.kind == ResourceKind::Backbuffer)
        {
            entry.format = m_backbufferFormat;
            entry.width = m_extent.width;
            entry.height = m_extent.height;
        }
    }
    for (const RenderPassGroup& group : m_groups)
    {
        std::string operations;
        if (group.swapchain)
        {
            operations = "swapchain";
        }
        else
        {
            operations = std::string("color ") + OperationName(group.output.colorOperation);
            if (group.output.depthFormat != VK_FORMAT_UNDEFINED)
            {
                operations += std::string(", depth ") + OperationName(group.output.depthOperation)
                    + (group.output.sampleDepth ? " / sampled" : group.output.storeDepth ? " / stored" : " / discarded");
            }
        }
        info.renderPasses.push_back({.name = group.name, .operations = std::move(operations)});
    }
    m_debugInfo = std::move(info);
}

void FrameGraph::RecordBarriers(rhi::CommandBuffer& commandBuffer, const Node& node)
{
    rhi::ExecutionBarrier barrier;
    auto flush = [&]() {
        if (!barrier.IsEmpty())
        {
            commandBuffer.Barrier(barrier);
            barrier = {};
        }
    };

    for (const Access& access : node.accesses)
    {
        if (access.kind == AccessKind::ColorAttachment || access.kind == AccessKind::DepthAttachment)
        {
            continue; // BindPass and the subpass dependencies handle attachments
        }
        const Resource& resource = m_resources[access.resource];
        if (!resource.available)
        {
            continue;
        }
        if (resource.kind == ResourceKind::Buffer)
        {
            const auto state = m_bufferStates.try_emplace(resource.buffer, resource.initialBufferState).first;
            const rhi::ResourceStates oldState = state->second;
            if (IsReadOnly(oldState) && IsReadOnly(access.state))
            {
                if ((access.state & ~oldState) == 0)
                {
                    continue;
                }
                barrier.AddBuffer(resource.buffer, oldState, access.state);
                state->second = oldState | access.state;
            }
            else
            {
                barrier.AddBuffer(resource.buffer, oldState, access.state);
                state->second = access.state;
            }
            if (barrier.bufferCount == rhi::kMaxBarriersPerBatch)
            {
                flush();
            }
            continue;
        }

        const rhi::TextureHandle texture = TextureOf(access.resource);
        if (texture.IsValid())
        {
            barrier.AddImage(texture, access.state);
            if (barrier.imageCount == rhi::kMaxBarriersPerBatch)
            {
                flush();
            }
        }
    }
    flush();
}

void FrameGraph::Execute(rhi::CommandBuffer& commandBuffer, const RenderView& view)
{
    MENGINE_ASSERT(!m_dirty);
    for (const ScheduleEntry& entry : m_schedule)
    {
        if (entry.group == kNone)
        {
            Node& node = m_nodes[entry.node];
            if (!node.pass->IsActive())
            {
                continue;
            }
            RecordBarriers(commandBuffer, node);
            const FrameGraphResources resources(*this, entry.node);
            FrameGraphContext context{.commandBuffer = commandBuffer, .resources = resources, .view = view};
            commandBuffer.PushTimestamp(node.name.c_str());
            node.pass->Execute(context);
            commandBuffer.PopTimestamp();
            continue;
        }

        const RenderPassGroup& group = m_groups[entry.group];
        MENGINE_ASSERT(group.nodes.size() <= 64);
        uint64_t activeMask = 0;
        for (size_t member = 0; member < group.nodes.size() && member < 64; ++member)
        {
            activeMask |= m_nodes[group.nodes[member]].pass->IsActive() ? 1ull << member : 0ull;
        }
        if (activeMask == 0)
        {
            continue;
        }
        for (size_t member = 0; member < group.nodes.size(); ++member)
        {
            if ((activeMask & (1ull << member)) != 0)
            {
                RecordBarriers(commandBuffer, m_nodes[group.nodes[member]]);
            }
        }

        for (uint32_t slot = 0; slot < group.clearColors.size(); ++slot)
        {
            commandBuffer.SetClearColor(slot, group.clearColors[slot]);
        }
        commandBuffer.SetClearDepth(group.clearDepth);

        const bool nested = group.nodes.size() > 1;
        commandBuffer.PushTimestamp(group.name.c_str());
        commandBuffer.BindPass(group.handle);
        for (size_t member = 0; member < group.nodes.size(); ++member)
        {
            if ((activeMask & (1ull << member)) == 0)
            {
                continue;
            }
            Node& node = m_nodes[group.nodes[member]];
            const FrameGraphResources resources(*this, group.nodes[member]);
            FrameGraphContext context{.commandBuffer = commandBuffer, .resources = resources, .view = view};
            if (nested)
            {
                commandBuffer.PushTimestamp(node.name.c_str());
            }
            node.pass->Execute(context);
            if (nested)
            {
                commandBuffer.PopTimestamp();
            }
        }
        commandBuffer.EndPass();
        commandBuffer.PopTimestamp();
    }
}

} // namespace mengine::render
