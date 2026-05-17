#include "resources.hpp"

namespace webserver
{
namespace resources
{

std::string Resource::name()
{
    return "ResourceBase";
}

std::string Resource::to_string()
{
    return m_config.dump();
}

nlohmann::json Resource::get()
{
    return m_config;
}

Resource::Resource(std::shared_ptr<EventBus> event_bus) : m_event_bus(event_bus)
{
    subscribe_callback(EventType::RESET_CONFIG, EventPriority::EVENT_PRIORITY_MEDIUM,
                       [this](ResourceStateChangeNotification /*notification*/) { this->reset_config(); });
}

void Resource::subscribe_callback(EventType resource_type, ResourceChangeCallback callback)
{
    m_event_bus->subscribe(name(), resource_type, EventPriority::EVENT_PRIORITY_HIGH, callback);
}

void Resource::subscribe_callback(EventType resource_type, EventPriority priority, ResourceChangeCallback callback)
{
    m_event_bus->subscribe(name(), resource_type, priority, callback);
}

void Resource::subscribe_callback(std::initializer_list<EventType> resource_types, ResourceChangeCallback callback)
{
    m_event_bus->subscribe(name(), resource_types, EventPriority::EVENT_PRIORITY_HIGH, callback);
}

void Resource::subscribe_callback(std::initializer_list<EventType> resource_types, EventPriority priority,
                                  ResourceChangeCallback callback)
{
    m_event_bus->subscribe(name(), resource_types, priority, callback);
}

} // namespace resources
} // namespace webserver
