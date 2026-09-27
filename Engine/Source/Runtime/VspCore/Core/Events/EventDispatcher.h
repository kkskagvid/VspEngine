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
            // The listeners of the event's type are COPIED under the lock and
            // invoked OUTSIDE it. A callback is arbitrary code - it may dispatch
            // another event or add a listener - and calling it while the mutex is
            // held would deadlock on the engine's non-recursive mutex (and let a
            // callback mutate the list it is being iterated).
            std::vector<std::function<void(Event&)>> listenersOfType;
            {
                EventType type = e.GetEventType();
                std::lock_guard<std::mutex> lock(m_Mutex);

                auto it = m_Listeners.find(type);
                if (it == m_Listeners.end())
                    return;

                listenersOfType = it->second;
            }

            for (auto& callback : listenersOfType)
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
