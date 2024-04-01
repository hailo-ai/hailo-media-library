#include "pipeline/pipeline.hpp"
#include "common/common.hpp"
#include "resources/resources.hpp"
#include <gst/gst.h>
// #include "osd_utils.hpp"

using namespace webserver::pipeline;
using namespace webserver::resources;

std::shared_ptr<DummyPipeline> DummyPipeline::create()
{
    return std::make_shared<DummyPipeline>(ResourceRepository::create());
}

std::string webserver::pipeline::DummyPipeline::create_gst_pipeline_string()
{
    std::ostringstream pipeline;
    pipeline << "gst-launch-1.0 videotestsrc pattern=ball ! ";
    pipeline << "video/x-raw,width=320,height=240,framerate=10/1 ! ";
    pipeline << "queue ! ";
    pipeline << "x264enc ! ";
    pipeline << "h264parse config-interval=-1 ! ";
    pipeline << "queue ! ";
    pipeline << "rtph264pay ! ";
    pipeline << "queue ! ";
    pipeline << "udpsink host=" << UDP_HOST << " port=5000";

    std::string pipeline_str = pipeline.str();
    std::cout << "Pipeline: \n"
              << pipeline_str << std::endl;
    return pipeline_str;
}

DummyPipeline::DummyPipeline(WebserverResourceRepository resources)
    : IPipeline(resources)
{
    for (const auto &[key, val] : resources->get_all_types())
    {
        for (const auto &resource_type : val)
        {
            auto resource = resources->get(resource_type);
            if (resource == nullptr)
                continue;
            resource->subscribe_callback([this](ResourceStateChangeNotification notif)
                                         { this->callback_handle_strategy(notif); });
        }
    }
}

void DummyPipeline::callback_handle_strategy(ResourceStateChangeNotification notif)
{
    std::string data_string;
    switch (notif.resource_type)
    {
    case RESOURCE_AI:
    {
        auto state = std::static_pointer_cast<AiResource::AiResourceState>(notif.resource_state);
        data_string = "\n\tenabled: ";
        for (const auto &app : state->enabled)
        {
            data_string += std::to_string(app) + ", ";
        }
        data_string += "\n\tdisabled: ";
        for (const auto &app : state->disabled)
        {
            data_string += std::to_string(app) + ", ";
        }
        break;
    }
    case RESOURCE_PRIVACY_MASK:
    {
        auto state = std::static_pointer_cast<PrivacyMaskResource::PrivacyMaskResourceState>(notif.resource_state);
        data_string = "\n\tenabled: ";
        for (const auto &mask : state->enabled)
        {
            data_string += mask + ", ";
        }
        data_string += "\n\tdisabled: ";
        for (const auto &mask : state->disabled)
        {
            data_string += mask + ", ";
        }
        break;
    }
    case RESOURCE_FRONTEND:
    case RESOURCE_ENCODER:
    case RESOURCE_OSD:
    case RESOURCE_ISP:
    {
        data_string = "";
        break;
    }
    default:
        data_string = "???";
        break;
    }

    std::cout << "Dummy pipeline Resource callback, type: " << notif.resource_type << " data: " << data_string << std::endl;
}