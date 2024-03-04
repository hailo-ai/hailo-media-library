#include <httplib.h>
#include <nlohmann/json.hpp>
#include "pipeline/pipeline.hpp"
#include "resources/resources.hpp"
#include "resources/repository.hpp"

#include <chrono>
#include <thread>

// main method
int main(int argc, char *argv[])
{
    // HTTP
    std::shared_ptr<httplib::Server> svr = std::make_shared<httplib::Server>();

    WebServerPipeline pipeline = webserver::pipeline::IPipeline::create();
    WebserverResourceRepository resources_repo = pipeline->get_resources();
    auto supported_resources = resources_repo->get_all_types();

    svr->Get("/", [](const httplib::Request &, httplib::Response &res)
             { res.set_content("Media Library WebAPI server!", "text/plain"); });

    svr->Get("/resources", [supported_resources](const httplib::Request &, httplib::Response &res)
             { 
                nlohmann::json resources_json = supported_resources;
                res.set_content(resources_json.dump(), "application/json"); });

    // register resources
    for (const auto &[key, val] : supported_resources)
    {
        for (const auto &resource_type : val)
        {
            auto resource = resources_repo->get(resource_type);
            if (resource == nullptr)
                continue;
            resource->http_register(svr);
        }
    }

    auto isp_resource = std::static_pointer_cast<webserver::resources::IspResource>(pipeline->get_resources()->get(webserver::resources::RESOURCE_ISP));
    isp_resource->override_configurations();

    pipeline->start();

    // Part of the logic involels sleeping for a second and then inspecting ISP parameters
    std::this_thread::sleep_for(std::chrono::seconds(1));
    isp_resource->init();

    svr->listen("0.0.0.0", 8080);
    pipeline->stop();
}
