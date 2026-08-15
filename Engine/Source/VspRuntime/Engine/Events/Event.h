#pragma once

#include <cstdint>
#include <functional>

#include "Engine/Core/Core.h"

namespace Vsp
{
    enum class EventType
    {
        None = 0,
        WindowClose,
        WindowResize,
        MouseMoved,
        MouseButtonPressed,
        MouseButtonReleased,
        KeyPressed,
        KeyReleased,
        KeyTyped,
        MouseScrolled
    };

    enum class EventCategory : uint16_t
    {
        None = 0,
        Application     = BIT(0),
        Input           = BIT(1),
        Keyboard        = BIT(2),
        Mouse           = BIT(3),
        MouseButton     = BIT(4),
        All             = 0xFFFF
    };

    constexpr EventCategory operator|(EventCategory a, EventCategory b)
    {
        return static_cast<EventCategory>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
    }
    constexpr EventCategory operator&(EventCategory a, EventCategory b)
    {
        return static_cast<EventCategory>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
    }
    constexpr EventCategory operator~(EventCategory a)
    {
        return static_cast<EventCategory>(~static_cast<uint16_t>(a));
    }
    constexpr bool operator!=(EventCategory a, EventCategory b) { return !(a == b); }

    class Event
    {
    public:
        
        virtual ~Event() = default;

        virtual EventType GetEventType() const = 0;
        virtual EventCategory GetCategory() const = 0;

        inline bool IsInCategory(EventCategory category)
        {
            // 事件相同时候 00000001 & 00000001 依旧是 00000001 这个事件
            // 事件不相同时 00000001 & 00000010 结果是 00000011 两个事件的总和（例如鼠标移动和鼠标按钮同时触发）
            return static_cast<int>(GetCategory() & category);
        }

        bool IsHandled() const { return m_Handled; }
        void SetHandled(bool handled = true) { m_Handled = handled; }

    private:
        bool m_Handled = false;
    };

    using EventCallback = std::function<void(Event&)>;

#define EVENT_CLASS_TYPE(type) \
    static EventType GetStaticType() { return EventType::type; } \
    EventType GetEventType() const override { return GetStaticType(); }
}

