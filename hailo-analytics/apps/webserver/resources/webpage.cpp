#include "webpage.hpp"

#define WEBPAGE_BUNDLE_PATH "/usr/share/hailo/webpage"

webserver::resources::WebpageResource::WebpageResource(std::shared_ptr<EventBus> event_bus) : Resource(event_bus)
{
}

void webserver::resources::WebpageResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->set_mount_point("/", WEBPAGE_BUNDLE_PATH); // webpage & assets
    srv->Redirect("/", "/index.html");
}
