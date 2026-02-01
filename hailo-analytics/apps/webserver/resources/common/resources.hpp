#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <mutex>
#include <shared_mutex>
#include <gst/gst.h>
#include <initializer_list>
#include "common/httplib/httplib_utils.hpp"
#include "common/logger_macros.hpp"
#include "event_bus.hpp"
#include "media_library/gyro_device.hpp"
#include "common/common.hpp"

namespace webserver
{
namespace resources
{
enum ResourceType
{
    RESOURCE_WEBPAGE,
    RESOURCE_CONFIG_MANAGER,
    RESOURCE_FRONTEND,
    RESOURCE_ENCODER,
    RESOURCE_OSD,
    RESOURCE_AI,
    RESOURCE_ISP,
    RESOURCE_PRIVACY_MASK,
    RESOURCE_WEBRTC,
};

class Resource
{
  private:
    std::shared_ptr<EventBus> m_event_bus;

  protected:
    std::string m_default_config;
    nlohmann::json m_config;
    virtual void reset_config() {};

  public:
    Resource(std::shared_ptr<EventBus> event_bus) : m_event_bus(event_bus)
    {
        subscribe_callback(EventType::RESET_CONFIG, EventPriority::EVENT_PRIORITY_MEDIUM,
                           [this](ResourceStateChangeNotification notification) { this->reset_config(); });
    }
    virtual ~Resource() = default;
    virtual std::string name()
    {
        return "ResourceBase";
    };
    virtual ResourceType get_type() = 0;
    virtual void http_register(std::shared_ptr<HTTPServer> srv) = 0;

    virtual std::string to_string()
    {
        return m_config.dump();
    }

    virtual nlohmann::json get()
    {
        return m_config;
    }

    template <typename T> void on_resource_change(EventType type, std::shared_ptr<T> state)
    {
        m_event_bus->notify(type, state);
    }

    void subscribe_callback(EventType resource_type, ResourceChangeCallback callback)
    {
        m_event_bus->subscribe(name(), resource_type, EventPriority::EVENT_PRIORITY_HIGH, callback);
    }

    void subscribe_callback(EventType resource_type, EventPriority priority, ResourceChangeCallback callback)
    {
        m_event_bus->subscribe(name(), resource_type, priority, callback);
    }

    void subscribe_callback(std::initializer_list<EventType> resource_types, ResourceChangeCallback callback)
    {
        m_event_bus->subscribe(name(), resource_types, EventPriority::EVENT_PRIORITY_HIGH, callback);
    }

    void subscribe_callback(std::initializer_list<EventType> resource_types, EventPriority priority,
                            ResourceChangeCallback callback)
    {
        m_event_bus->subscribe(name(), resource_types, priority, callback);
    }
};
} // namespace resources
} // namespace webserver

typedef std::shared_ptr<webserver::resources::Resource> WebserverResource;
