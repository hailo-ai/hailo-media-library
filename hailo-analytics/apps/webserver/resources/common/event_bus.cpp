#include "event_bus.hpp"

#include <algorithm>
#include <thread>

#include "resources/common/events_utils.hpp"

uint64_t EventBus::generate_registration_id()
{
    return m_next_registration_id.fetch_add(1, std::memory_order_relaxed);
}

void EventBus::subscribe(subscriber_id_t subscriber_id, EventType event_type, EventPriority priority,
                         const ResourceChangeCallback &callback)
{
    WEBSERVER_LOG_INFO("Subscribing to event type {} with priority {}", nlohmann::json(event_type).dump(),
                       nlohmann::json(priority).dump());
    resource_callback_t callback_data{callback, event_type, priority, subscriber_id, false, generate_registration_id()};
    m_callbacks[event_type][priority].emplace_back(callback_data);
}

void EventBus::subscribe(subscriber_id_t subscriber_id, std::initializer_list<EventType> event_types,
                         EventPriority priority, const ResourceChangeCallback &callback)
{
    for (auto event_type : event_types)
    {
        subscribe(subscriber_id, event_type, priority, callback);
    }
}
void EventBus::subscribe_async(subscriber_id_t subscriber_id, EventType event_type, EventPriority priority,
                               const ResourceChangeCallback &callback)
{
    auto async_callback = [callback](auto... args) {
        std::thread([callback, args...]() { callback(args...); }).detach();
    };
    WEBSERVER_LOG_INFO("Subscribing to event type {} with priority {}", nlohmann::json(event_type).dump(),
                       nlohmann::json(priority).dump());
    resource_callback_t callback_data{async_callback, event_type, priority,
                                      subscriber_id,  true,       generate_registration_id()};
    m_callbacks[event_type][priority].emplace_back(callback_data);
}

void EventBus::subscribe_async(subscriber_id_t subscriber_id, std::initializer_list<EventType> event_types,
                               EventPriority priority, const ResourceChangeCallback &callback)
{
    for (auto event_type : event_types)
    {
        subscribe_async(subscriber_id, event_type, priority, callback);
    }
}

void EventBus::unsubscribe_all(subscriber_id_t subscriber_id)
{
    WEBSERVER_LOG_INFO("Unsubscribing all events for subscriber {}", subscriber_id);
    for (auto &event_type : m_callbacks)
    {
        for (auto &priority_callbacks : event_type.second)
        {
            auto &callbacks = priority_callbacks.second;
            callbacks.erase(std::remove_if(callbacks.begin(), callbacks.end(),
                                           [&subscriber_id](const resource_callback_t &callback) {
                                               return callback.subscriber_id == subscriber_id;
                                           }),
                            callbacks.end());
        }
    }
}

bool EventBus::is_callback_still_registered(EventType event_type, const resource_callback_t &callback) const
{
    auto event_it = m_callbacks.find(event_type);
    if (event_it == m_callbacks.end())
    {
        return false;
    }

    auto priority_it = event_it->second.find(callback.priority);
    if (priority_it == event_it->second.end())
    {
        return false;
    }

    const auto &callbacks = priority_it->second;
    for (const auto &registered_callback : callbacks)
    {
        // Match by unique registration_id - this ensures we only execute if the exact
        // same callback registration still exists, not just a callback with the same subscriber_id
        if (registered_callback.registration_id == callback.registration_id)
        {
            return true;
        }
    }

    return false;
}
