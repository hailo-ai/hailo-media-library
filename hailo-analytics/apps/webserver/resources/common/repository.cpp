#include "repository.hpp"

#include <utility>
#include "common/httplib/httplib_utils.hpp"

using namespace webserver::resources;

WebserverResourceRepository ResourceRepository::create(HTTPServer &svr, std::string config_path)
{
    auto event_bus = std::make_shared<EventBus>();

    auto config_resource = std::make_shared<ConfigResourceMedialib>(event_bus, config_path);
    auto osd_resource = std::make_shared<OsdResource>(event_bus, config_resource);
    auto isp_resource = std::make_shared<IspResource>(event_bus, config_resource);
    auto encoder_resource = std::make_shared<EncoderResource>(event_bus, config_resource);
    auto privacy_mask_resource = std::make_shared<PrivacyMaskResource>(event_bus, config_resource);
    auto webpage_resource = std::make_shared<WebpageResource>(event_bus);
    auto webrtc_resource = std::make_shared<WebRtcResource>(event_bus, config_resource);
    std::vector<WebserverResource> resources_vec{};
    resources_vec.push_back(config_resource);
    resources_vec.push_back(isp_resource);
    resources_vec.push_back(osd_resource);
    resources_vec.push_back(encoder_resource);
    resources_vec.push_back(privacy_mask_resource);
    resources_vec.push_back(webpage_resource);
    resources_vec.push_back(webrtc_resource);

    return std::make_unique<ResourceRepository>(resources_vec, event_bus, svr);
}

WebserverResourceRepository ResourceRepository::create(HTTPServer &svr)
{
    return create(svr, "");
}

ResourceRepository::ResourceRepository(std::vector<WebserverResource> resources, std::shared_ptr<EventBus> event_bus,
                                       HTTPServer &srv)
    : m_resources(std::move(resources)), m_event_bus(std::move(event_bus)), m_srv(srv)
{
    register_resources(srv);
}

ResourceRepository::~ResourceRepository()
{
    WEBSERVER_LOG_DEBUG("ResourceRepository destroyed");
}

WebserverResource ResourceRepository::get(ResourceType t)
{
    auto it = std::find_if(m_resources.begin(), m_resources.end(),
                           [t](const WebserverResource &resource) { return resource->get_type() == t; });
    if (it != m_resources.end())
    {
        return *it;
    }
    throw std::runtime_error("Resource not found");
}

void ResourceRepository::register_resources(HTTPServer &svr)
{
    for (auto resource : m_resources)
    {
        resource->http_register(svr);
    }
}
