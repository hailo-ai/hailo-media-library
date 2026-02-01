#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <type_traits>
#include <thread>
#include "common/logger_macros.hpp"
#include "events_utils.hpp"

using namespace webserver::resources;

typedef std::string subscriber_id_t;
class EventBus
{
  public:
    void subscribe(subscriber_id_t subscriber_id, EventType event_type, EventPriority prority,
                   const ResourceChangeCallback &callback);

    void subscribe(subscriber_id_t subscriber_id, std::initializer_list<EventType> event_types, EventPriority priority,
                   const ResourceChangeCallback &callback);

    void subscribe_async(subscriber_id_t subscriber_id, EventType event_type, EventPriority priority,
                         const ResourceChangeCallback &callback);

    void subscribe_async(subscriber_id_t subscriber_id, std::initializer_list<EventType> event_types,
                         EventPriority priority, const ResourceChangeCallback &callback);

    void unsubscribe_all(subscriber_id_t subscriber_id);

    template <typename T, typename = std::enable_if_t<std::is_base_of<ResourceState, T>::value>>
    void notify(EventType event_type, std::shared_ptr<T> data)
    {
        // Find the event type in the map
        auto it = m_callbacks.find(event_type);
        if (it != m_callbacks.end())
        {
            // Call the callbacks in order of priority
            std::map<EventPriority, std::vector<resource_callback_t>> &priority_callbacks = it->second;
            for (auto &[priority, callbacks] : priority_callbacks)
            {
                WEBSERVER_LOG_DEBUG("Calling callbacks for event type {} with priority {}",
                                    static_cast<nlohmann::json>(event_type).dump(), priority);
                for (auto &callback : callbacks)
                {
                    WEBSERVER_LOG_DEBUG("Calling callback event type {} with priority {}",
                                        static_cast<nlohmann::json>(event_type).dump(), priority);
                    callback.callback({event_type, data});
                }
                WEBSERVER_LOG_DEBUG("finished calling callback with priority {}", priority);
            }
            WEBSERVER_LOG_DEBUG("finished call all the callbacks");
        }
    }

  private:
    std::unordered_map<EventType, std::map<EventPriority, std::vector<resource_callback_t>>> m_callbacks;
};
