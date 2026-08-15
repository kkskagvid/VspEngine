#pragma once

#include "Engine/Events/Event.h"

namespace Vsp
{
    class MouseMovedEvent : public Event
    {
    public:
        MouseMovedEvent(float x, float y) : X(x), Y(y) {}

        float X, Y;

        EVENT_CLASS_TYPE(MouseMoved)
        EventCategory GetCategory() const override
        {
            return EventCategory::Input | EventCategory::Mouse;
        }
    };

    class MouseButtonPressedEvent : public Event
    {
    public:
        MouseButtonPressedEvent(int button) : Button(button) {}

        int Button;

        EVENT_CLASS_TYPE(MouseButtonPressed)
        EventCategory GetCategory() const override
        {
            return EventCategory::Input | EventCategory::Mouse | EventCategory::MouseButton;
        }
    };

    class MouseButtonReleasedEvent : public Event
    {
    public:
        MouseButtonReleasedEvent(int button) : Button(button) {}

        int Button;

        EVENT_CLASS_TYPE(MouseButtonReleased)
        EventCategory GetCategory() const override
        {
            return EventCategory::Input | EventCategory::Mouse | EventCategory::MouseButton;
        }
    };

    class KeyPressedEvent : public Event
    {
    public:
        KeyPressedEvent(int keyCode, int repeatCount) : KeyCode(keyCode), RepeatCount(repeatCount) {}

        int KeyCode;
        int RepeatCount;

        EVENT_CLASS_TYPE(KeyPressed)
        EventCategory GetCategory() const override
        {
            return EventCategory::Input | EventCategory::Keyboard;
        }
    };

    class KeyReleasedEvent : public Event
    {
    public:
        KeyReleasedEvent(int keyCode) : KeyCode(keyCode) {}

        int KeyCode;

        EVENT_CLASS_TYPE(KeyReleased)
        EventCategory GetCategory() const override
        {
            return EventCategory::Input | EventCategory::Keyboard;
        }
    };

    class KeyTypedEvent : public Event
    {
    public:
        KeyTypedEvent(unsigned int codepoint) : Codepoint(codepoint) {}

        unsigned int Codepoint;

        EVENT_CLASS_TYPE(KeyTyped)
        EventCategory GetCategory() const override
        {
            return EventCategory::Input | EventCategory::Keyboard;
        }
    };

    class MouseScrolledEvent : public Event
    {
    public:
        MouseScrolledEvent(float xOffset, float yOffset) : XOffset(xOffset), YOffset(yOffset) {}

        float XOffset, YOffset;

        EVENT_CLASS_TYPE(MouseScrolled)
        EventCategory GetCategory() const override
        {
            return EventCategory::Input | EventCategory::Mouse;
        }
    };
}
