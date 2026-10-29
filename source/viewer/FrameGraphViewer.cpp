#include "FrameGraphViewer.hpp"

#include "render/RenderTypes.hpp"
#include "render/graph/FrameGraph.hpp"

#include <glm/geometric.hpp>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_node_editor.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ed = ax::NodeEditor;

namespace mengine {

namespace {

using Info = render::FrameGraphDebugInfo;

// Pin ids encode node, resource and side, so they need no table. The editor keeps node, pin and link ids apart; the
// bases only help in debuggers. 32 bit safe.
constexpr uintptr_t kPinIdBase = 0x40000000u;
constexpr uintptr_t kLinkIdBase = 0x20000000u;
constexpr uint32_t kMaxPinResources = 1u << 15;

ed::NodeId NodeIdOf(uint32_t node)
{
    return ed::NodeId(static_cast<uintptr_t>(node) + 1);
}

int NodeIndexOf(ed::NodeId id, size_t count)
{
    const uintptr_t value = id.Get();
    return value >= 1 && value <= count ? static_cast<int>(value - 1) : -1;
}

ed::PinId PinIdOf(uint32_t node, uint32_t resource, bool output)
{
    return ed::PinId(kPinIdBase | (static_cast<uintptr_t>(node + 1) << 16) | (static_cast<uintptr_t>(resource) << 1)
        | (output ? 1u : 0u));
}

ed::LinkId LinkIdOf(size_t link)
{
    return ed::LinkId(kLinkIdBase + static_cast<uintptr_t>(link) + 1);
}

constexpr std::array<ImU32, 8> kHeaderPalette = {
    IM_COL32(0x1F, 0x3A, 0x93, 255),
    IM_COL32(0x6A, 0x1B, 0x9A, 255),
    IM_COL32(0x1B, 0x5E, 0x20, 255),
    IM_COL32(0x8B, 0x00, 0x00, 255),
    IM_COL32(0x8C, 0x8C, 0x1A, 255),
    IM_COL32(0x00, 0x69, 0x5C, 255),
    IM_COL32(0xA0, 0x4A, 0x00, 255),
    IM_COL32(0x0D, 0x47, 0xA1, 255),
};
constexpr ImU32 kHeaderDisabled = IM_COL32(0x5A, 0x5C, 0x61, 255);

constexpr ImU32 kCanvasBg = IM_COL32(0x3C, 0x3F, 0x47, 255);
constexpr ImU32 kCanvasGrid = IM_COL32(255, 255, 255, 22);
constexpr ImU32 kNodeBg = IM_COL32(0x6E, 0x71, 0x78, 240);
constexpr ImU32 kNodeBgDisabled = IM_COL32(0x4A, 0x4C, 0x52, 220);
constexpr ImU32 kNodeBorder = IM_COL32(0x2B, 0x2D, 0x31, 255);
constexpr ImU32 kNodeBorderDisabled = IM_COL32(0x2B, 0x2D, 0x31, 160);
constexpr ImU32 kSelected = IM_COL32(0xF5, 0xD1, 0x42, 255);
constexpr ImU32 kHovered = IM_COL32(255, 255, 255, 150);

constexpr ImU32 kText = IM_COL32(0xFF, 0xFF, 0xFF, 255);
constexpr ImU32 kTextSecondary = IM_COL32(0xE0, 0xE0, 0xE0, 200);
constexpr ImU32 kTextDisabled = IM_COL32(0x9A, 0x9A, 0x9A, 255);
constexpr ImU32 kStatusText = IM_COL32(0xFF, 0x9A, 0x4D, 220);

constexpr ImVec4 kLinkLive = ImVec4(1.0f, 1.0f, 1.0f, 0.95f);
constexpr ImVec4 kLinkDead = ImVec4(0.43f, 0.43f, 0.43f, 0.55f);

ImU32 HeaderColor(uint32_t node, bool scheduled)
{
    return scheduled ? kHeaderPalette[node % kHeaderPalette.size()] : kHeaderDisabled;
}

ImU32 ResourceColor(const Info::Resource& resource, bool live)
{
    ImU32 color = IM_COL32(0xE0, 0xE0, 0xE0, 255);
    if (std::strcmp(resource.kind, "backbuffer") == 0)
    {
        color = IM_COL32(0xE0, 0x48, 0x48, 255);
    }
    else if (std::strcmp(resource.kind, "texture") == 0)
    {
        color = IM_COL32(0x78, 0xBE, 0xFF, 255);
    }
    else if (std::strcmp(resource.kind, "buffer") == 0)
    {
        color = IM_COL32(0xFF, 0x9A, 0x4D, 255);
    }
    else if (std::strcmp(resource.kind, "transient") != 0)
    {
        color = IM_COL32(0x7A, 0x7A, 0x7A, 255);
    }
    return live ? color : (color & IM_COL32(255, 255, 255, 0)) | IM_COL32(0, 0, 0, 110);
}

// "skipped: needs ..." -> "skipped".
std::string_view ShortStatus(std::string_view status)
{
    return status.substr(0, status.find(':'));
}

const char* ResourceName(const Info& info, uint32_t resource)
{
    return resource < info.resources.size() ? info.resources[resource].name.c_str() : "?";
}

void DrawPinIcon(ImDrawList* drawList, const ImVec2& center, float radius, ImU32 color, bool connected, bool hovered)
{
    if (hovered)
    {
        radius *= 1.25f;
    }
    if (connected)
    {
        drawList->AddCircleFilled(center, radius, color, 16);
    }
    else
    {
        drawList->AddCircle(center, radius - 0.75f, color, 16, 1.6f);
    }
}

// The "expanded" mark of the node headers.
void DrawHeaderArrow(ImDrawList* drawList, const ImVec2& center, float size, ImU32 color)
{
    drawList->AddTriangleFilled(ImVec2(center.x - size, center.y - size * 0.55f), ImVec2(center.x + size, center.y - size * 0.55f),
        ImVec2(center.x, center.y + size * 0.65f), color);
}

void DrawHeaderGloss(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float rounding)
{
    const float top = std::min(min.y + rounding, max.y);
    drawList->AddRectFilledMultiColor(ImVec2(min.x, top), max, IM_COL32(255, 255, 255, 20), IM_COL32(255, 255, 255, 20),
        IM_COL32(0, 0, 0, 50), IM_COL32(0, 0, 0, 50));
}

void LegendItem(ImU32 color, const char* label, bool circle)
{
    const float size = ImGui::GetTextLineHeight() * 0.7f;
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float offsetY = (ImGui::GetTextLineHeight() - size) * 0.5f + ImGui::GetStyle().FramePadding.y;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (circle)
    {
        drawList->AddCircleFilled(ImVec2(position.x + size * 0.5f, position.y + offsetY + size * 0.5f), size * 0.5f, color, 12);
    }
    else
    {
        drawList->AddRectFilled(ImVec2(position.x, position.y + offsetY), ImVec2(position.x + size, position.y + offsetY + size),
            color, 2.0f);
    }
    ImGui::Dummy(ImVec2(size, ImGui::GetFrameHeight()));
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
}

struct NodeLayout
{
    std::vector<uint32_t> inputResources;
    std::vector<uint32_t> outputResources;
    float contentWidth = 0.0f;
    float contentHeight = 0.0f;
    float headerHeight = 0.0f;
    float lineHeight = 0.0f;
    float rowHeight = 0.0f;
    float iconSize = 0.0f;
    float spacing = 0.0f;
    bool statusLine = false;
};

float PinLabelWidth(const Info& info, uint32_t resource)
{
    return ImGui::CalcTextSize(ResourceName(info, resource)).x;
}

NodeLayout MeasureNode(const Info& info, const Info::Node& node)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    NodeLayout layout;
    for (const Info::Pin& pin : node.pins)
    {
        if (pin.resource >= kMaxPinResources)
        {
            continue;
        }
        (pin.output ? layout.outputResources : layout.inputResources).push_back(pin.resource);
    }

    const float textHeight = ImGui::GetTextLineHeight();
    layout.iconSize = textHeight * 0.75f;
    layout.lineHeight = std::max(textHeight, layout.iconSize);
    layout.rowHeight = layout.lineHeight + style.ItemSpacing.y;
    layout.spacing = style.ItemInnerSpacing.x;
    layout.headerHeight = ImGui::GetFrameHeight() + 2.0f;
    layout.statusLine = !node.scheduled;

    const float arrowWidth = textHeight * 0.6f;
    const char* tag = node.type == render::FrameGraphNodeType::Compute ? "CS" : "GFX";
    const float titleWidth = arrowWidth + layout.spacing + ImGui::CalcTextSize(node.name.c_str()).x + layout.spacing * 4.0f
        + ImGui::CalcTextSize(tag).x;

    float inputWidth = 0.0f;
    for (const uint32_t resource : layout.inputResources)
    {
        inputWidth = std::max(inputWidth, layout.iconSize + layout.spacing + PinLabelWidth(info, resource));
    }
    float outputWidth = 0.0f;
    for (const uint32_t resource : layout.outputResources)
    {
        outputWidth = std::max(outputWidth, layout.iconSize + layout.spacing + PinLabelWidth(info, resource));
    }
    const float pinsWidth = inputWidth + outputWidth + ImGui::GetFontSize() * 2.0f;

    float statusWidth = 0.0f;
    if (layout.statusLine)
    {
        const std::string_view status = ShortStatus(node.status);
        statusWidth = ImGui::CalcTextSize(status.data(), status.data() + status.size()).x;
    }

    layout.contentWidth = std::max({ImGui::GetFontSize() * 9.0f, titleWidth, pinsWidth, statusWidth});
    const size_t rows = std::max(layout.inputResources.size(), layout.outputResources.size());
    layout.contentHeight = layout.headerHeight + layout.spacing + (layout.statusLine ? layout.rowHeight : 0.0f)
        + static_cast<float>(rows) * layout.rowHeight + 1.0f;
    return layout;
}

bool PinConnected(const Info& info, uint32_t node, uint32_t resource, bool output)
{
    for (const Info::Link& link : info.links)
    {
        if (link.resource == resource && (output ? link.fromNode == node : link.toNode == node))
        {
            return true;
        }
    }
    return false;
}

void DrawPin(const Info& info, const NodeLayout& layout, uint32_t node, uint32_t resource, bool output, bool live,
    const ImVec2& rowOrigin)
{
    const ed::PinId id = PinIdOf(node, resource, output);
    const char* label = ResourceName(info, resource);
    const float labelWidth = ImGui::CalcTextSize(label).x;
    const float pinWidth = layout.iconSize + layout.spacing + labelWidth;
    const ImVec2 start = output ? ImVec2(rowOrigin.x + layout.contentWidth - pinWidth, rowOrigin.y) : rowOrigin;
    const ImVec2 iconMin = output ? ImVec2(start.x + labelWidth + layout.spacing, start.y) : start;
    const ImVec2 center(iconMin.x + layout.iconSize * 0.5f, iconMin.y + layout.lineHeight * 0.5f);

    ImGui::SetCursorScreenPos(start);
    ed::BeginPin(id, output ? ed::PinKind::Output : ed::PinKind::Input);
    if (output)
    {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(0.0f, layout.spacing);
        ImGui::Dummy(ImVec2(layout.iconSize, layout.lineHeight));
    }
    else
    {
        ImGui::Dummy(ImVec2(layout.iconSize, layout.lineHeight));
        ImGui::SameLine(0.0f, layout.spacing);
        ImGui::TextUnformatted(label);
    }
    ed::PinPivotRect(center, center);
    ed::EndPin();

    const ImU32 color = resource < info.resources.size() ? ResourceColor(info.resources[resource], live) : kTextDisabled;
    DrawPinIcon(ImGui::GetWindowDrawList(), center, layout.iconSize * 0.5f, color, PinConnected(info, node, resource, output),
        ed::GetHoveredPin() == id);
}

void DrawNode(const Info& info, uint32_t index)
{
    const Info::Node& node = info.nodes[index];
    const NodeLayout layout = MeasureNode(info, node);
    const bool live = node.scheduled;
    const ed::NodeId id = NodeIdOf(index);

    ed::PushStyleColor(ed::StyleColor_NodeBg, ImGui::ColorConvertU32ToFloat4(live ? kNodeBg : kNodeBgDisabled));
    ed::PushStyleColor(ed::StyleColor_NodeBorder, ImGui::ColorConvertU32ToFloat4(live ? kNodeBorder : kNodeBorderDisabled));
    ImGui::PushStyleColor(ImGuiCol_Text, live ? kText : kTextDisabled);

    ed::BeginNode(id);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // The header background is drawn behind the node once its size is known.
    const float textHeight = ImGui::GetTextLineHeight();
    const float headerTextY = origin.y + (layout.headerHeight - textHeight) * 0.5f;
    const float arrowSize = textHeight * 0.3f;
    DrawHeaderArrow(drawList, ImVec2(origin.x + arrowSize, headerTextY + textHeight * 0.5f), arrowSize, live ? kText : kTextDisabled);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + arrowSize * 2.0f + layout.spacing, headerTextY));
    ImGui::TextUnformatted(node.name.c_str());
    const char* tag = node.type == render::FrameGraphNodeType::Compute ? "CS" : "GFX";
    ImGui::SetCursorScreenPos(ImVec2(origin.x + layout.contentWidth - ImGui::CalcTextSize(tag).x, headerTextY));
    ImGui::PushStyleColor(ImGuiCol_Text, live ? kTextSecondary : kTextDisabled);
    ImGui::TextUnformatted(tag);
    ImGui::PopStyleColor();

    float y = origin.y + layout.headerHeight + layout.spacing;
    if (layout.statusLine)
    {
        const std::string_view status = ShortStatus(node.status);
        ImGui::SetCursorScreenPos(ImVec2(origin.x, y));
        ImGui::PushStyleColor(ImGuiCol_Text, kStatusText);
        ImGui::TextUnformatted(status.data(), status.data() + status.size());
        ImGui::PopStyleColor();
        y += layout.rowHeight;
    }

    const size_t rows = std::max(layout.inputResources.size(), layout.outputResources.size());
    for (size_t row = 0; row < rows; ++row)
    {
        const ImVec2 rowOrigin(origin.x, y);
        if (row < layout.inputResources.size())
        {
            DrawPin(info, layout, index, layout.inputResources[row], false, live, rowOrigin);
        }
        if (row < layout.outputResources.size())
        {
            DrawPin(info, layout, index, layout.outputResources[row], true, live, rowOrigin);
        }
        y += layout.rowHeight;
    }

    // Fixes the content size, so outputs line up with the right edge.
    ImGui::SetCursorScreenPos(ImVec2(origin.x, y));
    ImGui::Dummy(ImVec2(layout.contentWidth, 1.0f));
    ed::EndNode();

    ImGui::PopStyleColor();
    ed::PopStyleColor(2);

    if (ImGui::IsItemVisible())
    {
        if (ImDrawList* background = ed::GetNodeBackgroundDrawList(id))
        {
            const ImVec2 nodeMin = ImGui::GetItemRectMin();
            const ImVec2 nodeMax = ImGui::GetItemRectMax();
            const float halfBorder = ed::GetStyle().NodeBorderWidth * 0.5f;
            const float rounding = ed::GetStyle().NodeRounding;
            const ImVec2 headerMin(nodeMin.x + halfBorder, nodeMin.y + halfBorder);
            const ImVec2 headerMax(nodeMax.x - halfBorder, nodeMin.y + layout.headerHeight);
            background->AddRectFilled(headerMin, headerMax, HeaderColor(index, live), rounding, ImDrawFlags_RoundCornersTop);
            DrawHeaderGloss(background, headerMin, headerMax, rounding);
            background->AddLine(ImVec2(headerMin.x, headerMax.y), headerMax, IM_COL32(0, 0, 0, 100), 1.0f);
        }
    }
}

ImVec2 NodeExtent(const Info& info, const Info::Node& node)
{
    const NodeLayout layout = MeasureNode(info, node);
    const ImVec4& padding = ed::GetStyle().NodePadding;
    return ImVec2(layout.contentWidth + padding.x + padding.z, layout.contentHeight + padding.y + padding.w);
}

} // namespace

void FrameGraphViewer::Init()
{
    if (m_editor != nullptr)
    {
        return;
    }
    ed::Config config;
    // No settings file: the layout comes from ApplyAutoLayout and lives for the session, like the ImGui windows.
    config.SettingsName = nullptr;
    m_editor = ed::CreateEditor(&config);

    ed::SetCurrentEditor(m_editor);
    ed::Style& style = ed::GetStyle();
    style.NodePadding = ImVec4(10.0f, 0.0f, 10.0f, 8.0f);
    style.NodeRounding = 4.0f;
    style.NodeBorderWidth = 1.0f;
    style.HoveredNodeBorderWidth = 2.0f;
    style.SelectedNodeBorderWidth = 2.5f;
    style.PinRounding = 3.0f;
    style.LinkStrength = 120.0f;
    style.FlowMarkerDistance = 24.0f;
    style.FlowSpeed = 220.0f;
    style.FlowDuration = 1.5f;
    style.Colors[ed::StyleColor_Bg] = ImGui::ColorConvertU32ToFloat4(kCanvasBg);
    style.Colors[ed::StyleColor_Grid] = ImGui::ColorConvertU32ToFloat4(kCanvasGrid);
    style.Colors[ed::StyleColor_SelNodeBorder] = ImGui::ColorConvertU32ToFloat4(kSelected);
    style.Colors[ed::StyleColor_HovNodeBorder] = ImGui::ColorConvertU32ToFloat4(kHovered);
    style.Colors[ed::StyleColor_SelLinkBorder] = ImGui::ColorConvertU32ToFloat4(kSelected);
    style.Colors[ed::StyleColor_HovLinkBorder] = ImGui::ColorConvertU32ToFloat4(kHovered);
    style.Colors[ed::StyleColor_PinRect] = ImVec4(1.0f, 1.0f, 1.0f, 0.12f);
    style.Colors[ed::StyleColor_PinRectBorder] = ImVec4(1.0f, 1.0f, 1.0f, 0.25f);
    style.Colors[ed::StyleColor_Flow] = ImGui::ColorConvertU32ToFloat4(kSelected);
    style.Colors[ed::StyleColor_FlowMarker] = ImGui::ColorConvertU32ToFloat4(kSelected);
    // Read only graph: no delete, copy or paste.
    ed::EnableShortcuts(false);
    ed::SetCurrentEditor(nullptr);
}

void FrameGraphViewer::Shutdown()
{
    if (m_editor != nullptr)
    {
        ed::DestroyEditor(m_editor);
        m_editor = nullptr;
    }
    m_layoutNames.clear();
    m_layoutPending = true;
    m_selected = -1;
}

bool FrameGraphViewer::Contains(float x, float y) const
{
    return x >= m_windowMin[0] && y >= m_windowMin[1] && x <= m_windowMax[0] && y <= m_windowMax[1];
}

bool FrameGraphViewer::CanvasContains(const glm::vec2& point) const
{
    return point.x >= m_canvasMin.x && point.y >= m_canvasMin.y && point.x <= m_canvasMax.x && point.y <= m_canvasMax.y;
}

bool FrameGraphViewer::HandleEvent(const SDL_Event& event, const Input& input)
{
    // Input has already seen the event: a finger that went down is in Touches(), one that went up is gone.
    const std::vector<Input::Touch>& touches = input.Touches();
    switch (event.type)
    {
    case SDL_EVENT_FINGER_DOWN:
        if (touches.size() == 1)
        {
            m_gestureSelection = m_selected;
        }
        if (!m_pinch.active && touches.size() >= 2 && m_editor != nullptr && CanvasContains(touches[0].position)
            && CanvasContains(touches[1].position))
        {
            m_pinch = Pinch{.active = true, .first = touches[0].id, .second = touches[1].id};
            // Ends the first finger's press without a click: released with the mouse gone, nothing is hovered, so a
            // node drag or box selection ends and nothing gets selected.
            ImGuiIO& io = ImGui::GetIO();
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        }
        return false;

    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        // The gesture lasts until every finger is up; SDL does not hand the mouse to the remaining finger.
        if (m_pinch.active && touches.empty())
        {
            m_pinch = Pinch{};
            m_restoreSelection = true;
        }
        return false;

    case SDL_EVENT_MOUSE_MOTION:
        return m_pinch.active && event.motion.which == SDL_TOUCH_MOUSEID;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return m_pinch.active && event.button.which == SDL_TOUCH_MOUSEID;

    case SDL_EVENT_MOUSE_WHEEL:
        return m_pinch.active && event.wheel.which == SDL_TOUCH_MOUSEID;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        // Input drops every finger here without finger up events.
        m_pinch = Pinch{};
        return false;

    default:
        return false;
    }
}

void FrameGraphViewer::UpdatePinch(const Input& input, float canvasX, float canvasY, float canvasWidth, float canvasHeight)
{
    const Input::Touch* first = nullptr;
    const Input::Touch* second = nullptr;
    for (const Input::Touch& touch : input.Touches())
    {
        if (touch.id == m_pinch.first)
        {
            first = &touch;
        }
        else if (touch.id == m_pinch.second)
        {
            second = &touch;
        }
    }
    if (first == nullptr || second == nullptr)
    {
        // One of the pair lifted: continue with the first two fingers still down, from their current positions.
        const std::vector<Input::Touch>& touches = input.Touches();
        m_pinch.hasPrevious = false;
        if (touches.size() >= 2)
        {
            m_pinch.first = touches[0].id;
            m_pinch.second = touches[1].id;
        }
        return;
    }

    const glm::vec2 center = (first->position + second->position) * 0.5f;
    const float distance = glm::length(second->position - first->position);
    constexpr float kMinDistance = 8.0f; // fingers this close give no usable scale
    if (m_pinch.hasPrevious && m_pinch.previousDistance >= kMinDistance && distance >= kMinDistance && canvasWidth > 0.0f
        && canvasHeight > 0.0f)
    {
        const ed::ViewState view = ed::GetViewState();
        const glm::vec2 viewMin(view.VisibleMin.x, view.VisibleMin.y);
        const glm::vec2 viewSize(view.VisibleMax.x - view.VisibleMin.x, view.VisibleMax.y - view.VisibleMin.y);
        const glm::vec2 canvasOrigin(canvasX, canvasY);
        const glm::vec2 canvasSize(canvasWidth, canvasHeight);
        if (viewSize.x > 0.0f && viewSize.y > 0.0f)
        {
            // Keeps the canvas point under the previous midpoint under the current one; fingers apart zoom in.
            const glm::vec2 anchor = viewMin + (m_pinch.previousCenter - canvasOrigin) * (viewSize / canvasSize);
            const glm::vec2 newSize = viewSize * (m_pinch.previousDistance / distance);
            const glm::vec2 newMin = anchor - (center - canvasOrigin) * (newSize / canvasSize);
            ed::ViewState target;
            target.VisibleMin = ImVec2(newMin.x, newMin.y);
            target.VisibleMax = ImVec2(newMin.x + newSize.x, newMin.y + newSize.y);
            // The editor clamps this to its zoom levels.
            ed::SetViewState(target, 0.0f);
        }
    }
    m_pinch.hasPrevious = true;
    m_pinch.previousCenter = center;
    m_pinch.previousDistance = distance;
}

void FrameGraphViewer::Draw(render::FrameGraph& graph, render::RenderSettings& settings, std::string_view lockedPass,
    const Input& input)
{
    m_windowMin[0] = m_windowMin[1] = 0.0f;
    m_windowMax[0] = m_windowMax[1] = -1.0f;
    m_canvasMin = glm::vec2(0.0f);
    m_canvasMax = glm::vec2(-1.0f);
    if (!m_open || m_editor == nullptr)
    {
        m_pinch = Pinch{};
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.30f, io.DisplaySize.y * 0.40f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.68f, io.DisplaySize.y * 0.57f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 200.0f), ImVec2(FLT_MAX, FLT_MAX));
    const bool visible = ImGui::Begin("Frame Graph Viewer", &m_open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    m_windowMin[0] = position.x;
    m_windowMin[1] = position.y;
    m_windowMax[0] = position.x + size.x;
    m_windowMax[1] = position.y + size.y;
    if (!visible)
    {
        ImGui::End();
        return;
    }

    const Info& info = graph.GetDebugInfo();
    if (m_selected >= static_cast<int>(info.nodes.size()))
    {
        m_selected = -1;
    }

    // Only a different set of passes lays out again; recompiles (toggles, resizes) keep the user's node positions.
    if (m_layoutNames.size() != info.nodes.size()
        || !std::equal(m_layoutNames.begin(), m_layoutNames.end(), info.nodes.begin(),
            [](const std::string& name, const Info::Node& node) { return name == node.name; }))
    {
        m_layoutNames.clear();
        for (const Info::Node& node : info.nodes)
        {
            m_layoutNames.push_back(node.name);
        }
        m_layoutPending = true;
    }

    DrawToolbar(info);

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float splitterWidth = std::max(4.0f, style.ItemSpacing.x * 0.5f);
    if (m_inspectorWidth <= 0.0f)
    {
        m_inspectorWidth = ImGui::GetFontSize() * 18.0f;
    }
    const float minInspector = ImGui::GetFontSize() * 12.0f;
    const float maxInspector = std::max(minInspector, available.x * 0.6f);
    m_inspectorWidth = std::clamp(m_inspectorWidth, minInspector, maxInspector);
    const float canvasWidth = std::max(1.0f, available.x - m_inspectorWidth - splitterWidth);

    DrawCanvas(graph, info, lockedPass, input, canvasWidth, available.y);

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::InvisibleButton("##InspectorSplitter", ImVec2(splitterWidth, std::max(1.0f, available.y)));
    const bool splitterActive = ImGui::IsItemActive();
    const bool splitterHovered = ImGui::IsItemHovered();
    if (splitterActive)
    {
        m_inspectorWidth = std::clamp(m_inspectorWidth - io.MouseDelta.x, minInspector, maxInspector);
    }
    if (splitterActive || splitterHovered)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImU32 color = ImGui::GetColorU32(splitterActive ? ImGuiCol_SeparatorActive
                : splitterHovered                             ? ImGuiCol_SeparatorHovered
                                                              : ImGuiCol_Separator);
        const float centerX = (min.x + max.x) * 0.5f;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(centerX - (splitterActive || splitterHovered ? 1.5f : 0.5f), min.y),
            ImVec2(centerX + (splitterActive || splitterHovered ? 1.5f : 0.5f), max.y), color);
    }

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x, style.WindowPadding.y * 0.5f));
    if (ImGui::BeginChild("Inspector", ImVec2(0.0f, available.y), ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        DrawInspector(graph, info, settings, lockedPass);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
}

void FrameGraphViewer::DrawToolbar(const Info& info)
{
    if (ImGui::Button("Auto Layout"))
    {
        m_layoutPending = true;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Arrange the passes in columns by dependency depth");
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit View"))
    {
        m_fitFrames = 1;
        m_fitDuration = -1.0f;
    }

    uint32_t scheduled = 0;
    for (const Info::Node& node : info.nodes)
    {
        scheduled += node.scheduled ? 1u : 0u;
    }
    ImGui::SameLine(0.0f, ImGui::GetFontSize() * 1.5f);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Compiles: %u", info.compileCount);
    ImGui::SameLine(0.0f, ImGui::GetFontSize());
    ImGui::Text("Passes: %u / %zu scheduled", scheduled, info.nodes.size());

    ImGui::SameLine(0.0f, ImGui::GetFontSize() * 1.5f);
    LegendItem(kHeaderPalette[0], "Scheduled", false);
    ImGui::SameLine(0.0f, ImGui::GetFontSize());
    LegendItem(kHeaderDisabled, "Culled / Disabled", false);
    ImGui::SameLine(0.0f, ImGui::GetFontSize());
    LegendItem(IM_COL32(0xE0, 0xE0, 0xE0, 255), "Transient", true);
    ImGui::SameLine(0.0f, ImGui::GetFontSize());
    LegendItem(IM_COL32(0x78, 0xBE, 0xFF, 255), "Imported", true);
    ImGui::SameLine(0.0f, ImGui::GetFontSize());
    LegendItem(IM_COL32(0xE0, 0x48, 0x48, 255), "Backbuffer", true);
}

void FrameGraphViewer::DrawCanvas(render::FrameGraph& graph, const Info& info, std::string_view lockedPass,
    const Input& input, float width, float height)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::BeginChild("Canvas", ImVec2(width, height), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!visible)
    {
        ImGui::EndChild();
        return;
    }

    // The editor fills the child (no padding), so this is the screen rectangle its view maps to.
    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    m_canvasMin = glm::vec2(canvasOrigin.x, canvasOrigin.y);
    m_canvasMax = glm::vec2(canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y);

    ed::SetCurrentEditor(m_editor);
    if (m_pinch.active)
    {
        UpdatePinch(input, canvasOrigin.x, canvasOrigin.y, canvasSize.x, canvasSize.y);
        // A pinch overrides a pending fit, which would otherwise jump the view mid-gesture.
        m_fitFrames = 0;
    }
    if (m_layoutPending)
    {
        ApplyAutoLayout(info);
        m_layoutPending = false;
        // Fit once the nodes have been submitted with their new positions.
        m_fitFrames = 2;
        m_fitDuration = 0.0f;
    }

    ed::Begin("FrameGraph", ImVec2(0.0f, 0.0f));

    for (uint32_t index = 0; index < info.nodes.size(); ++index)
    {
        DrawNode(info, index);
    }
    for (size_t index = 0; index < info.links.size(); ++index)
    {
        const Info::Link& link = info.links[index];
        if (link.fromNode >= info.nodes.size() || link.toNode >= info.nodes.size() || link.resource >= kMaxPinResources)
        {
            continue;
        }
        const bool selected = m_selected >= 0
            && (link.fromNode == static_cast<uint32_t>(m_selected) || link.toNode == static_cast<uint32_t>(m_selected));
        ImVec4 color = link.live ? kLinkLive : kLinkDead;
        if (selected && link.live)
        {
            color = ImGui::ColorConvertU32ToFloat4(kSelected);
        }
        ed::Link(LinkIdOf(index), PinIdOf(link.fromNode, link.resource, true), PinIdOf(link.toNode, link.resource, false),
            color, link.live ? 2.5f : 1.5f);
    }

    SyncSelection(info);
    if (m_restoreSelection)
    {
        m_restoreSelection = false;
        if (m_gestureSelection >= 0 && m_gestureSelection < static_cast<int>(info.nodes.size()))
        {
            ed::SelectNode(NodeIdOf(static_cast<uint32_t>(m_gestureSelection)));
            m_selected = m_gestureSelection;
        }
        else
        {
            ed::ClearSelection();
            m_selected = -1;
        }
    }

    if (m_fitFrames > 0 && --m_fitFrames == 0)
    {
        ed::NavigateToContent(m_fitDuration);
    }
    if (m_focusSelection)
    {
        m_focusSelection = false;
        ed::NavigateToSelection(false);
    }

    ed::Suspend();
    DrawContextMenu(graph, info, lockedPass);
    DrawTooltips(info);
    ed::Resume();

    ed::End();
    ed::SetCurrentEditor(nullptr);
    ImGui::EndChild();
}

void FrameGraphViewer::SyncSelection(const Info& info)
{
    // What the first finger did to the selection is undone when the pinch ends; the inspector keeps its pass meanwhile.
    if (!ed::HasSelectionChanged() || m_pinch.active)
    {
        return;
    }
    if (ed::GetSelectedObjectCount() == 0)
    {
        m_selected = -1;
        return;
    }
    ed::NodeId selected;
    if (ed::GetSelectedNodes(&selected, 1) > 0)
    {
        m_selected = NodeIndexOf(selected, info.nodes.size());
        // Animates the data flowing through the selected pass.
        for (size_t index = 0; index < info.links.size(); ++index)
        {
            const Info::Link& link = info.links[index];
            if (link.live
                && (link.fromNode == static_cast<uint32_t>(m_selected) || link.toNode == static_cast<uint32_t>(m_selected)))
            {
                ed::Flow(LinkIdOf(index));
            }
        }
    }
}

void FrameGraphViewer::DrawContextMenu(render::FrameGraph& graph, const Info& info, std::string_view lockedPass)
{
    ed::NodeId contextNode;
    if (ed::ShowNodeContextMenu(&contextNode))
    {
        m_contextNode = NodeIndexOf(contextNode, info.nodes.size());
        if (m_contextNode >= 0)
        {
            ImGui::OpenPopup("FrameGraphNodeMenu");
        }
    }

    if (ImGui::BeginPopup("FrameGraphNodeMenu"))
    {
        if (m_contextNode >= 0 && m_contextNode < static_cast<int>(info.nodes.size()))
        {
            const Info::Node& node = info.nodes[m_contextNode];
            ImGui::TextDisabled("%s", node.name.c_str());
            ImGui::Separator();
            const bool enabled = graph.IsEnabled(node.name);
            const bool locked = node.name == lockedPass;
            if (ImGui::MenuItem("Enabled", nullptr, enabled, !locked))
            {
                graph.SetEnabled(node.name, !enabled);
            }
            if (ImGui::MenuItem("Inspect"))
            {
                ed::SelectNode(NodeIdOf(static_cast<uint32_t>(m_contextNode)));
                m_selected = m_contextNode;
            }
            if (ImGui::MenuItem("Focus"))
            {
                ed::SelectNode(NodeIdOf(static_cast<uint32_t>(m_contextNode)));
                m_selected = m_contextNode;
                m_focusSelection = true;
            }
        }
        else
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void FrameGraphViewer::DrawTooltips(const Info& info)
{
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
    {
        return;
    }
    const ed::PinId hoveredPin = ed::GetHoveredPin();
    if (hoveredPin)
    {
        const uintptr_t value = hoveredPin.Get();
        const uint32_t resource = static_cast<uint32_t>((value >> 1) & (kMaxPinResources - 1));
        if (resource < info.resources.size())
        {
            const Info::Resource& entry = info.resources[resource];
            ImGui::BeginTooltip();
            ImGui::Text("%s (%s)", entry.name.c_str(), entry.kind);
            if (entry.width > 0)
            {
                ImGui::Text("%u x %u", entry.width, entry.height);
            }
            if (entry.firstUse >= 0)
            {
                ImGui::Text("Lifetime: %d..%d", entry.firstUse, entry.lastUse);
            }
            ImGui::EndTooltip();
        }
        return;
    }

    const ed::LinkId hoveredLink = ed::GetHoveredLink();
    if (hoveredLink)
    {
        const uintptr_t index = hoveredLink.Get() - kLinkIdBase - 1;
        if (index < info.links.size())
        {
            const Info::Link& link = info.links[index];
            ImGui::SetTooltip("%s: %s -> %s%s", ResourceName(info, link.resource), info.nodes[link.fromNode].name.c_str(),
                info.nodes[link.toNode].name.c_str(), link.live ? "" : " (inactive)");
        }
        return;
    }

    const int hoveredNode = NodeIndexOf(ed::GetHoveredNode(), info.nodes.size());
    if (hoveredNode >= 0 && !info.nodes[hoveredNode].scheduled)
    {
        ImGui::SetTooltip("%s", info.nodes[hoveredNode].status.c_str());
    }
}

void FrameGraphViewer::ApplyAutoLayout(const Info& info)
{
    // Columns by dependency depth. Links run from earlier to later passes and are ordered by reader, so one pass over
    // them finishes every writer's column before its readers'.
    const size_t count = info.nodes.size();
    std::vector<uint32_t> column(count, 0);
    for (const Info::Link& link : info.links)
    {
        if (link.fromNode < count && link.toNode < count)
        {
            column[link.toNode] = std::max(column[link.toNode], column[link.fromNode] + 1);
        }
    }
    const uint32_t columns = count == 0 ? 0 : *std::max_element(column.begin(), column.end()) + 1;

    std::vector<ImVec2> extents(count);
    std::vector<float> columnWidth(columns, 0.0f);
    std::vector<float> columnHeight(columns, 0.0f);
    const float gapX = ImGui::GetFontSize() * 3.0f;
    const float gapY = ImGui::GetFontSize() * 2.0f;
    for (size_t index = 0; index < count; ++index)
    {
        extents[index] = NodeExtent(info, info.nodes[index]);
        const uint32_t c = column[index];
        columnWidth[c] = std::max(columnWidth[c], extents[index].x);
        columnHeight[c] += (columnHeight[c] > 0.0f ? gapY : 0.0f) + extents[index].y;
    }

    std::vector<float> columnX(columns, 0.0f);
    for (uint32_t c = 1; c < columns; ++c)
    {
        columnX[c] = columnX[c - 1] + columnWidth[c - 1] + gapX;
    }
    // Columns are centered on y = 0, passes stacked in AddPass order.
    std::vector<float> cursorY(columns, 0.0f);
    for (uint32_t c = 0; c < columns; ++c)
    {
        cursorY[c] = -columnHeight[c] * 0.5f;
    }
    for (size_t index = 0; index < count; ++index)
    {
        const uint32_t c = column[index];
        ed::SetNodePosition(NodeIdOf(static_cast<uint32_t>(index)), ImVec2(columnX[c], cursorY[c]));
        cursorY[c] += extents[index].y + gapY;
    }
}

void FrameGraphViewer::DrawInspector(render::FrameGraph& graph, const Info& info, render::RenderSettings& settings,
    std::string_view lockedPass)
{
    if (m_selected < 0 || m_selected >= static_cast<int>(info.nodes.size()))
    {
        const char* hint = "Select a pass to inspect it";
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 textSize = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + std::max(0.0f, (available.x - textSize.x) * 0.5f),
            ImGui::GetCursorPosY() + std::max(0.0f, (available.y - textSize.y) * 0.4f)));
        ImGui::TextDisabled("%s", hint);
        return;
    }

    const uint32_t index = static_cast<uint32_t>(m_selected);
    const Info::Node& node = info.nodes[index];
    const ImGuiStyle& style = ImGui::GetStyle();

    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = ImGui::GetFrameHeight() + 4.0f;
        const ImVec2 max(min.x + width, min.y + height);
        drawList->AddRectFilled(min, max, HeaderColor(index, node.scheduled), 3.0f);
        DrawHeaderGloss(drawList, min, max, 3.0f);
        const float textHeight = ImGui::GetTextLineHeight();
        const float arrowSize = textHeight * 0.3f;
        const float textY = min.y + (height - textHeight) * 0.5f;
        DrawHeaderArrow(drawList, ImVec2(min.x + style.FramePadding.x + arrowSize, textY + textHeight * 0.5f), arrowSize, kText);
        drawList->AddText(ImVec2(min.x + style.FramePadding.x + arrowSize * 2.0f + style.ItemInnerSpacing.x, textY), kText,
            node.name.c_str());
        const char* tag = node.type == render::FrameGraphNodeType::Compute ? "Compute" : "Graphics";
        drawList->AddText(ImVec2(max.x - style.FramePadding.x - ImGui::CalcTextSize(tag).x, textY), kTextSecondary, tag);
        ImGui::Dummy(ImVec2(width, height));
    }

    render::FrameGraphPass* pass = graph.GetPass(index);
    if (const char* description = pass != nullptr ? pass->Description() : nullptr)
    {
        ImGui::TextWrapped("Description: %s", description);
    }

    ImGui::TextUnformatted("Status:");
    ImGui::SameLine();
    if (node.scheduled)
    {
        ImGui::TextUnformatted(node.status.c_str());
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, kStatusText);
        ImGui::TextWrapped("%s", node.status.c_str());
        ImGui::PopStyleColor();
    }
    if (node.renderPass >= 0 && static_cast<size_t>(node.renderPass) < info.renderPasses.size())
    {
        const Info::RenderPass& renderPass = info.renderPasses[node.renderPass];
        ImGui::TextWrapped("Render pass: #%d %s (%s)", node.renderPass, renderPass.name.c_str(), renderPass.operations.c_str());
    }

    bool enabled = graph.IsEnabled(node.name);
    const bool locked = node.name == lockedPass;
    ImGui::BeginDisabled(locked);
    if (ImGui::Checkbox("Enabled", &enabled))
    {
        graph.SetEnabled(node.name, enabled);
    }
    ImGui::EndDisabled();
    if (locked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("This pass draws the UI and cannot be disabled");
    }

    if (ImGui::CollapsingHeader("Resources", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto resourceList = [&](const char* label, bool output) {
            ImGui::TextDisabled("%s", label);
            bool any = false;
            for (const Info::Pin& pin : node.pins)
            {
                if (pin.output != output || pin.resource >= info.resources.size())
                {
                    continue;
                }
                any = true;
                const Info::Resource& resource = info.resources[pin.resource];
                const float radius = ImGui::GetTextLineHeight() * 0.3f;
                const ImVec2 position = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(position.x + style.IndentSpacing * 0.5f, position.y + ImGui::GetTextLineHeight() * 0.5f), radius,
                    ResourceColor(resource, true), 12);
                ImGui::Dummy(ImVec2(style.IndentSpacing, ImGui::GetTextLineHeight()));
                ImGui::SameLine(0.0f, 0.0f);
                if (resource.width > 0)
                {
                    ImGui::Text("%s  (%s, %ux%u)", resource.name.c_str(), resource.kind, resource.width, resource.height);
                }
                else
                {
                    ImGui::Text("%s  (%s)", resource.name.c_str(), resource.kind);
                }
            }
            if (!any)
            {
                ImGui::Indent();
                ImGui::TextDisabled("none");
                ImGui::Unindent();
            }
        };
        resourceList("Inputs", false);
        resourceList("Outputs", true);
    }

    if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool drawn = false;
        if (pass != nullptr)
        {
            ImGui::PushID(node.name.c_str());
            ImGui::PushItemWidth(std::max(ImGui::GetFontSize() * 6.0f, ImGui::GetContentRegionAvail().x * 0.58f));
            drawn = pass->DrawInspector(settings);
            ImGui::PopItemWidth();
            ImGui::PopID();
        }
        if (!drawn)
        {
            ImGui::TextDisabled("This pass has no parameters");
        }
    }
}

} // namespace mengine
