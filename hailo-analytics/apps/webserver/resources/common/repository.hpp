#pragma once
#include "common/httplib/httplib_utils.hpp"
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
    HTTPServer &m_srv;

    static std::unique_ptr<webserver::resources::ResourceRepository> create(HTTPServer &srv, std::string config_path);
    static std::unique_ptr<webserver::resources::ResourceRepository> create(HTTPServer &srv);
    ResourceRepository(std::vector<WebserverResource> resources, std::shared_ptr<EventBus> event_bus, HTTPServer &srv);
    ~ResourceRepository();
    void register_resources(HTTPServer &srv);
    WebserverResource get(ResourceType t);
};
} // namespace resources
} // namespace webserver

typedef std::unique_ptr<webserver::resources::ResourceRepository> WebserverResourceRepository;
