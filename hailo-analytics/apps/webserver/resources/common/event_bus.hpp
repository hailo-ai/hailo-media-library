#pragma once

#include <nlohmann/json.hpp>
#include <unordered_map>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <string>
#include <type_traits>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <utility>

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
            // Create a copy of the callbacks to avoid iterator invalidation
            // if callbacks modify subscriptions during execution (e.g., unsubscribe_all + resubscribe)
            std::map<EventPriority, std::vector<resource_callback_t>> priority_callbacks_copy = it->second;

            // Track which registration IDs we've already executed to avoid duplicates
            std::set<uint64_t> executed_registration_ids;

            // Call the callbacks in order of priority
            for (auto &[priority, callbacks_snapshot] : priority_callbacks_copy)
            {
                WEBSERVER_LOG_DEBUG("Calling callbacks for event type {} with priority {}",
                                    static_cast<nlohmann::json>(event_type).dump(), priority);

                // First, execute callbacks from the snapshot that still exist
                for (auto &callback : callbacks_snapshot)
                {
                    // Verify callback still exists in the original map before executing
                    // This prevents the case where same event & priority is being unregistered
                    // (eg, from previous callback of higher priority) during notification
                    if (is_callback_still_registered(event_type, callback))
                    {
                        WEBSERVER_LOG_DEBUG("Calling callback event type {} with priority {} (from snapshot)",
                                            static_cast<nlohmann::json>(event_type).dump(), priority);
                        callback.callback({event_type, data});
                        executed_registration_ids.insert(callback.registration_id);
                    }
                    else
                    {
                        WEBSERVER_LOG_DEBUG("Skipping callback for event type {} - subscriber {} was unregistered",
                                            static_cast<nlohmann::json>(event_type).dump(), callback.subscriber_id);
                    }
                }

                // Second, check for newly registered callbacks at this priority level and execute them
                // This handles the case where a previous higher priority callback registered another
                // callback of the same event & priority
                auto current_it = m_callbacks.find(event_type);
                if (current_it != m_callbacks.end())
                {
                    auto current_priority_it = current_it->second.find(priority);
                    if (current_priority_it != current_it->second.end())
                    {
                        for (auto &callback : current_priority_it->second)
                        {
                            // Only execute if we haven't already executed this registration_id
                            if (executed_registration_ids.find(callback.registration_id) ==
                                executed_registration_ids.end())
                            {
                                WEBSERVER_LOG_DEBUG(
                                    "Calling callback event type {} with priority {} (newly registered)",
                                    static_cast<nlohmann::json>(event_type).dump(), priority);
                                callback.callback({event_type, data});
                                executed_registration_ids.insert(callback.registration_id);
                            }
                        }
                    }
                }

                WEBSERVER_LOG_DEBUG("finished calling callback with priority {}", priority);
            }
            WEBSERVER_LOG_DEBUG("finished call all the callbacks");
        }
    }

  private:
    bool is_callback_still_registered(EventType event_type, const resource_callback_t &callback) const;
    uint64_t generate_registration_id();

    std::unordered_map<EventType, std::map<EventPriority, std::vector<resource_callback_t>>> m_callbacks;
    std::atomic<uint64_t> m_next_registration_id{1};
};
