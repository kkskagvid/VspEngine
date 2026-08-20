#pragma once

#include "Core/Events/Event.h"

namespace Vsp
{
    class WindowCloseEvent : public Event
    {
    public:
        WindowCloseEvent() {};

        EVENT_CLASS_TYPE(WindowClose)
        EventCategory GetCategory() const override
        {
            return EventCategory::Application;
        }
    };

    class WindowResizeEvent : public Event
    {
    public:
        WindowResizeEvent(int w, int h) : width(w), height(h) {}

        int width;
        int height;

        EVENT_CLASS_TYPE(WindowResize)
        EventCategory GetCategory() const override
        {
            return EventCategory::Application;
        }
    };

    // TODO: Add WindowMoveEvent, WindowFocusEvent, etc.
}
