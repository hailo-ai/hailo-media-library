#pragma once
#include "resources.hpp"
#include "resources/webrtc.hpp"
#include "resources/osd.hpp"
#include "resources/isp.hpp"
#include "resources/encoder.hpp"
#include "resources/privacy_mask.hpp"
#include "resources/webpage.hpp"
#include "resources/configs.hpp"
#include <map>

namespace webserver
{
namespace resources
{
class ResourceRepository
{
  private:
    std::vector<WebserverResource> m_resources;

  public:
    std::shared_ptr<EventBus> m_event_bus;
    std::shared_ptr<HTTPServer> m_srv;

    static std::shared_ptr<webserver::resources::ResourceRepository> create(std::shared_ptr<HTTPServer> srv,
                                                                            std::string config_path);
    static std::shared_ptr<webserver::resources::ResourceRepository> create(std::shared_ptr<HTTPServer> srv);
    ResourceRepository(std::vector<WebserverResource> resources, std::shared_ptr<EventBus> event_bus,
                       std::shared_ptr<HTTPServer> srv);
    void register_resources(std::shared_ptr<HTTPServer> srv);
    WebserverResource get(ResourceType t)
    {
        auto it = std::find_if(m_resources.begin(), m_resources.end(),
                               [t](const WebserverResource &resource) { return resource->get_type() == t; });
        if (it != m_resources.end())
        {
            return *it;
        }
        throw std::runtime_error("Resource not found");
    }
};
} // namespace resources
} // namespace webserver

typedef std::shared_ptr<webserver::resources::ResourceRepository> WebserverResourceRepository;
