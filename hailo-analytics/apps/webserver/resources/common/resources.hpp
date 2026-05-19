#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <initializer_list>
#include <memory>

#include "common/httplib/httplib_utils.hpp"
#include "event_bus.hpp"
#include "resources/common/events_utils.hpp"

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
    Resource(std::shared_ptr<EventBus> event_bus);
    virtual ~Resource() = default;
    virtual std::string name();
    virtual ResourceType get_type() = 0;
    virtual void http_register(HTTPServer &srv) = 0;

    virtual std::string to_string();

    virtual nlohmann::json get();

    template <typename T> void on_resource_change(EventType type, std::shared_ptr<T> state)
    {
        m_event_bus->notify(type, state);
    }

    void subscribe_callback(EventType resource_type, ResourceChangeCallback callback);

    void subscribe_callback(EventType resource_type, EventPriority priority, ResourceChangeCallback callback);

    void subscribe_callback(std::initializer_list<EventType> resource_types, ResourceChangeCallback callback);

    void subscribe_callback(std::initializer_list<EventType> resource_types, EventPriority priority,
                            ResourceChangeCallback callback);
};
} // namespace resources
} // namespace webserver

typedef std::shared_ptr<webserver::resources::Resource> WebserverResource;
