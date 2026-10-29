#pragma once

#include "platform/Input.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_touch.h>

#include <string>
#include <string_view>
#include <vector>

namespace ax::NodeEditor {
struct EditorContext;
} // namespace ax::NodeEditor

namespace mengine {

namespace render {
class FrameGraph;
struct FrameGraphDebugInfo;
struct RenderSettings;
} // namespace render

// A window showing the frame graph of the last compile as a node graph (imgui-node-editor): one node per pass in
// AddPass order, inputs (resources it reads) on the left, outputs (resources it writes) on the right, links from each
// writer to its readers. Passes that are not scheduled (culled, disabled, skipped, invalid) are drawn greyed out. The
// inspector beside the canvas shows the selected pass, toggles it and draws its parameters (FrameGraphPass::DrawInspector).
// Touch: two fingers that land on the canvas pan and zoom it around their midpoint. The editor itself only zooms with
// the wheel and pans with the right button, and SDL turns only the first finger into a (left button) mouse, so the
// gesture is recognized from the fingers and the emulated mouse is kept from ImGui while it runs.
//
// Call Init after the ImGui context exists and Shutdown before it is destroyed; Draw between NewFrame and Render.
class FrameGraphViewer
{
public:
    void Init();
    void Shutdown();

    // Call for every event after Input has seen it and before ImGui does. Returns true when ImGui must not see the
    // event (the touch emulated mouse during a pinch).
    bool HandleEvent(const SDL_Event& event, const Input& input);

    // `lockedPass` names a pass that cannot be disabled (the one drawing the UI).
    void Draw(render::FrameGraph& graph, render::RenderSettings& settings, std::string_view lockedPass, const Input& input);

    bool IsOpen() const { return m_open; }
    void SetOpen(bool open) { m_open = open; }
    // Whether a point (in window pixels) lies on the window drawn this frame.
    bool Contains(float x, float y) const;

private:
    void DrawToolbar(const render::FrameGraphDebugInfo& info);
    void DrawCanvas(render::FrameGraph& graph, const render::FrameGraphDebugInfo& info, std::string_view lockedPass,
        const Input& input, float width, float height);
    void DrawContextMenu(render::FrameGraph& graph, const render::FrameGraphDebugInfo& info, std::string_view lockedPass);
    void DrawTooltips(const render::FrameGraphDebugInfo& info);
    void DrawInspector(render::FrameGraph& graph, const render::FrameGraphDebugInfo& info,
        render::RenderSettings& settings, std::string_view lockedPass);
    void ApplyAutoLayout(const render::FrameGraphDebugInfo& info);
    void SyncSelection(const render::FrameGraphDebugInfo& info);
    bool CanvasContains(const glm::vec2& point) const;
    // Pans and zooms the view by the pinch fingers' movement since the last frame. Call with the editor current,
    // before Begin.
    void UpdatePinch(const Input& input, float canvasX, float canvasY, float canvasWidth, float canvasHeight);

    struct Pinch
    {
        bool active = false;
        SDL_FingerID first = 0;
        SDL_FingerID second = 0;
        bool hasPrevious = false;
        glm::vec2 previousCenter{0.0f};
        float previousDistance = 0.0f;
    };

    ax::NodeEditor::EditorContext* m_editor = nullptr;
    bool m_open = true;

    // Index into FrameGraphDebugInfo::nodes, -1 when nothing is selected.
    int m_selected = -1;
    int m_contextNode = -1;

    // Node names the current layout was made for; a different set lays the graph out again.
    std::vector<std::string> m_layoutNames;
    bool m_layoutPending = true;
    // Frames until the view fits the content (0: none pending); nodes need one submitted frame for their bounds.
    int m_fitFrames = 0;
    float m_fitDuration = 0.0f;
    bool m_focusSelection = false;

    float m_inspectorWidth = 0.0f; // 0: the default, set from the font size
    float m_windowMin[2] = {0.0f, 0.0f};
    float m_windowMax[2] = {-1.0f, -1.0f};
    // Canvas rectangle of the last drawn frame, empty when it was not drawn.
    glm::vec2 m_canvasMin{0.0f};
    glm::vec2 m_canvasMax{-1.0f};

    Pinch m_pinch;
    // Selection when the gesture's first finger landed. That finger is a mouse press for the editor until the second
    // one lands, so it may have changed the selection; the pinch puts it back.
    int m_gestureSelection = -1;
    bool m_restoreSelection = false;
};

} // namespace mengine
