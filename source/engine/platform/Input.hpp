#pragma once

#include <glm/vec2.hpp>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_touch.h>

#include <array>
#include <functional>
#include <vector>

namespace mengine {

class Window;

// Collects mouse, keyboard and touch state from SDL events. Coordinates are window points (same space as ImGui).
// Touch generated mouse events are ignored here; fingers are reported through Touches().
class Input
{
public:
    struct Touch
    {
        SDL_FingerID id = 0;
        glm::vec2 position{0.0f};
        glm::vec2 delta{0.0f};
        bool startedOverUi = false;
    };

    // Returns true when (x, y) lies on UI; presses that start there are not forwarded as camera input.
    using UiHitTest = std::function<bool(float x, float y)>;

    void SetUiHitTest(UiHitTest hitTest) { m_uiHitTest = std::move(hitTest); }

    // Clears per-frame deltas; call once per frame after the deltas have been consumed.
    void EndFrame();
    void HandleEvent(const SDL_Event& event, const Window& window);
    void Reset();

    bool IsKeyDown(SDL_Scancode scancode) const;

    // True while the button is held, unless the press started over UI.
    bool IsMouseButtonHeld(uint8_t button) const;
    glm::vec2 MouseDelta() const { return m_mouseDelta; }
    // Wheel notches this frame, positive away from the user; wheel input over UI is dropped.
    float WheelDelta() const { return m_wheelDelta; }

    const std::vector<Touch>& Touches() const { return m_touches; }

private:
    bool HitsUi(float x, float y) const { return m_uiHitTest && m_uiHitTest(x, y); }

    UiHitTest m_uiHitTest;
    std::array<bool, 8> m_mouseHeld{};
    glm::vec2 m_mouseDelta{0.0f};
    float m_wheelDelta = 0.0f;
    std::vector<Touch> m_touches;
};

} // namespace mengine
