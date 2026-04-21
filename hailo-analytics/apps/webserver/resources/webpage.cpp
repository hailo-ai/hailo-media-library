#include "webpage.hpp"

#define WEBPAGE_BUNDLE_PATH "/usr/share/hailo/webpage"

std::string webserver::resources::WebpageResource::name()
{
    return "webpage";
}

webserver::resources::ResourceType webserver::resources::WebpageResource::get_type()
{
    return ResourceType::RESOURCE_WEBPAGE;
}

webserver::resources::WebpageResource::WebpageResource(std::shared_ptr<EventBus> event_bus) : Resource(event_bus)
{
}

void webserver::resources::WebpageResource::http_register(HTTPServer &srv)
{
    srv.set_mount_point("/", WEBPAGE_BUNDLE_PATH); // webpage & assets
    srv.Redirect("/", "/index.html");
}
