#include "event_bus.hpp"

void EventBus::subscribe(subscriber_id_t subscriber_id, EventType event_type, EventPriority priority,
                         const ResourceChangeCallback &callback)
{
    WEBSERVER_LOG_INFO("Subscribing to event type {} with priority {}", nlohmann::json(event_type).dump(),
                       nlohmann::json(priority).dump());
    resource_callback_t callback_data{callback, event_type, priority, subscriber_id, false};
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
    resource_callback_t callback_data{async_callback, event_type, priority, subscriber_id, true};
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
