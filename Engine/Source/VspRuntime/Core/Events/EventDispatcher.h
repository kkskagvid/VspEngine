#pragma once

#include <functional>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <algorithm>

#include "Core/Events/Event.h"

namespace Vsp
{
    class EventDispatcher
    {
    public:
        template<typename E>
        void AddListener(std::function<void(E&)> callback)
        {
            static_assert(std::is_base_of_v<Event, E>, "E must derive from Event");

            auto wrapped = [cb = std::move(callback)](Event& e)
            {
                cb(static_cast<E&>(e));
            };

            EventType type = E::GetStaticType();
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Listeners[type].emplace_back(std::move(wrapped));
        }

        void Dispatch(Event& e)
        {
            EventType type = e.GetEventType();
            std::lock_guard<std::mutex> lock(m_Mutex);

            auto it = m_Listeners.find(type);
            if (it == m_Listeners.end())
                return;

            for (auto& callback : it->second)
            {
                callback(e);
                if (e.IsHandled())
                    break;
            }
        }

    private:
        std::mutex m_Mutex;
        std::unordered_map<EventType, std::vector<std::function<void(Event&)>>> m_Listeners;
    };
}
