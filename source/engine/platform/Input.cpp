#include "platform/Input.hpp"

#include "platform/Window.hpp"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>

namespace mengine {

void Input::EndFrame()
{
    m_mouseDelta = glm::vec2(0.0f);
    m_wheelDelta = 0.0f;
    for (Touch& touch : m_touches)
    {
        touch.delta = glm::vec2(0.0f);
    }
}

void Input::Reset()
{
    m_mouseHeld.fill(false);
    m_mouseDelta = glm::vec2(0.0f);
    m_wheelDelta = 0.0f;
    m_touches.clear();
}

void Input::HandleEvent(const SDL_Event& event, const Window& window)
{
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.which != SDL_TOUCH_MOUSEID && event.button.button < m_mouseHeld.size())
        {
            m_mouseHeld[event.button.button] = !HitsUi(event.button.x, event.button.y);
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.which != SDL_TOUCH_MOUSEID && event.button.button < m_mouseHeld.size())
        {
            m_mouseHeld[event.button.button] = false;
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        if (event.motion.which != SDL_TOUCH_MOUSEID)
        {
            m_mouseDelta += glm::vec2(event.motion.xrel, event.motion.yrel);
        }
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        if (event.wheel.which != SDL_TOUCH_MOUSEID && !HitsUi(event.wheel.mouse_x, event.wheel.mouse_y))
        {
            m_wheelDelta += event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
        }
        break;

    case SDL_EVENT_FINGER_DOWN:
    {
        int width = 0;
        int height = 0;
        window.GetSize(width, height);
        const glm::vec2 position(event.tfinger.x * static_cast<float>(width), event.tfinger.y * static_cast<float>(height));
        m_touches.push_back(Touch{
            .id = event.tfinger.fingerID,
            .position = position,
            .delta = glm::vec2(0.0f),
            .startedOverUi = HitsUi(position.x, position.y),
        });
        break;
    }

    case SDL_EVENT_FINGER_MOTION:
    {
        int width = 0;
        int height = 0;
        window.GetSize(width, height);
        for (Touch& touch : m_touches)
        {
            if (touch.id == event.tfinger.fingerID)
            {
                touch.position = glm::vec2(event.tfinger.x * static_cast<float>(width), event.tfinger.y * static_cast<float>(height));
                touch.delta += glm::vec2(event.tfinger.dx * static_cast<float>(width), event.tfinger.dy * static_cast<float>(height));
            }
        }
        break;
    }

    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        std::erase_if(m_touches, [&](const Touch& touch) { return touch.id == event.tfinger.fingerID; });
        break;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        Reset();
        break;

    default:
        break;
    }
}

bool Input::IsKeyDown(SDL_Scancode scancode) const
{
    int keyCount = 0;
    const bool* keys = SDL_GetKeyboardState(&keyCount);
    return keys != nullptr && scancode < keyCount && keys[scancode];
}

bool Input::IsMouseButtonHeld(uint8_t button) const
{
    return button < m_mouseHeld.size() && m_mouseHeld[button];
}

} // namespace mengine
