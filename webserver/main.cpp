#include <nlohmann/json.hpp>
#include "pipeline/pipeline.hpp"
#include "resources/resources.hpp"
#include "resources/repository.hpp"
#include "common/common.hpp"
#include "common/httplib/httplib_utils.hpp"
#include <chrono>
#include <thread>
#include <signal.h>
#include "media_library/signal_utils.hpp"

int main(int argc, char *argv[])
{
    WebServerPipeline pipeline = webserver::pipeline::IPipeline::create();

    std::shared_ptr<HTTPServer> svr = HTTPServer::create();
    WebserverResourceRepository resources_repo = pipeline->get_resources();
    auto supported_resources = resources_repo->get_all_types();

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

    signal_utils::register_signal_handler([pipeline](int signal)
                                          {
                                            std::cout << "Received signal " << signal << " exiting" << std::endl;
                                            pipeline->stop();
                                            exit(0); });

    pipeline->start();

    // Part of the logic involels sleeping for a second and then inspecting ISP parameters
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto isp_resource = std::static_pointer_cast<webserver::resources::IspResource>(pipeline->get_resources()->get(webserver::resources::RESOURCE_ISP));
    isp_resource->init();

    svr->listen("0.0.0.0", 80);
}
