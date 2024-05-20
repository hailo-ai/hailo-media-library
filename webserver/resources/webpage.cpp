#include "resources.hpp"

#define WEBPAGE_BUNDLE_PATH "/usr/share/hailo/webpage"

void webserver::resources::WebpageResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->set_mount_point("/", WEBPAGE_BUNDLE_PATH); // webpage & assets
    srv->Redirect("/", "/index.html");
}