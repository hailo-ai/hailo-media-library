#include "pipeline.hpp"
#include <gst/gst.h>

#ifndef MEDIALIB_LOCAL_SERVER
#define UDP_HOST "10.0.0.2"
#else
#define UDP_HOST "127.0.0.1"
#endif

std::shared_ptr<webserver::pipeline::IPipeline> webserver::pipeline::IPipeline::create()
{
#ifndef MEDIALIB_LOCAL_SERVER
    std::cout << "Creating pipeline" << std::endl;
    return webserver::pipeline::Pipeline::create();
#else
    std::cout << "Creating dummy pipeline" << std::endl;
    return webserver::pipeline::DummyPipeline::create();
#endif
}

std::shared_ptr<webserver::pipeline::Pipeline> webserver::pipeline::Pipeline::create()
{
    auto resources = webserver::resources::ResourceRepository::create();

    std::ostringstream pipeline;
    pipeline << "v4l2src device=/dev/video0 io-mode=mmap ! ";
    pipeline << "video/x-raw,format=NV12,width=3840,height=2160,framerate=30/1 ! ";
    pipeline << "queue leaky=downstream max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "hailofrontend name=frontend config-string='" << resources->get(webserver::resources::RESOURCE_FRONTEND)->to_string() << "' ";
    pipeline << "frontend. ! ";
    pipeline << "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! ";
    // pipeline << "hailoosd name=osd config-str=" << resources->get(webserver::resources::RESOURCE_OSD)->to_string() << " ! ";
    // pipeline << "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! ";
    // pipeline << "hailonet name=detection hef-path=" << ai_config["detection_hef_path"] << " is-active=" << ai_config["detection_active"] << " ! ";
    // pipeline << "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "hailoencoder name=enc config-str=" << resources->get(webserver::resources::RESOURCE_ENCODER)->to_string() << " ! ";
    pipeline << "h264parse config-interval=-1 ! ";
    pipeline << "video/x-h264,framerate=30 ! ";
    pipeline << "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "rtph264pay ! ";
    pipeline << "udpsink host=" << UDP_HOST << " port=5000";
    std::string pipeline_str = pipeline.str();
    std::cout << "Pipeline: \n"
              << pipeline_str << std::endl;
    return std::make_shared<webserver::pipeline::Pipeline>(resources, pipeline_str);
}

std::shared_ptr<webserver::pipeline::DummyPipeline> webserver::pipeline::DummyPipeline::create()
{
    std::ostringstream pipeline;
    pipeline << "videotestsrc pattern=ball ! ";
    pipeline << "video/x-raw,width=320,height=240,framerate=30/1 ! ";
    pipeline << "x264enc ! ";
    pipeline << "matroskamux streamable=true ! ";
    pipeline << "udpsink host=" << UDP_HOST << " port=5000";

    std::string pipeline_str = pipeline.str();
    std::cout << "Pipeline: \n"
              << pipeline_str << std::endl;
    return std::make_shared<webserver::pipeline::DummyPipeline>(webserver::resources::ResourceRepository::create(), pipeline_str);
}

webserver::pipeline::IPipeline::IPipeline(WebserverResourceRepository resources, std::string gst_pipeline_str)
{
    m_resources = resources;
    m_gst_pipeline_str = gst_pipeline_str;

    gst_init(nullptr, nullptr);

    m_pipeline = gst_parse_launch(gst_pipeline_str.c_str(), NULL);
    if (!m_pipeline)
    {
        std::cout << "Failed to create pipeline" << std::endl;
        throw std::runtime_error("Failed to create pipeline");
    }
    m_main_loop = g_main_loop_new(NULL, FALSE);
}

webserver::pipeline::IPipeline::~IPipeline()
{
    gst_object_unref(m_pipeline);
}

void webserver::pipeline::IPipeline::start()
{
    std::cout << "Starting pipeline" << std::endl;
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        std::cout << "Failed to start pipeline" << std::endl;
        throw std::runtime_error("Failed to start pipeline");
    }
    m_main_loop_thread = std::make_shared<std::thread>(
        [this]()
        { g_main_loop_run(m_main_loop); });
}

void webserver::pipeline::IPipeline::stop()
{
    gboolean ret = gst_element_send_event(m_pipeline, gst_event_new_eos());
    if (!ret)
    {
        std::cout << "Failed to send EOS event" << std::endl;
        throw std::runtime_error("Failed to send EOS event");
    }

    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    g_main_loop_quit(m_main_loop);
    m_main_loop_thread->join();
}

webserver::pipeline::Pipeline::Pipeline(WebserverResourceRepository resources, std::string gst_pipeline_str)
    : IPipeline(resources, gst_pipeline_str)
{
    for (const auto &[key, val] : resources->get_all_types())
    {
        for (const auto &resource_type : val)
        {
            auto resource = resources->get(resource_type);
            if (resource == nullptr)
                continue;
            resource->subscribe_callback([this](webserver::resources::ResourceStateChangeNotification notif)
                                         { this->callback_handle_strategy(notif); });
        }
    }
}

webserver::pipeline::DummyPipeline::DummyPipeline(WebserverResourceRepository resources, std::string gst_pipeline_str)
    : IPipeline(resources, gst_pipeline_str)
{
    for (const auto &[key, val] : resources->get_all_types())
    {
        for (const auto &resource_type : val)
        {
            auto resource = resources->get(resource_type);
            if (resource == nullptr)
                continue;
            resource->subscribe_callback([this](webserver::resources::ResourceStateChangeNotification notif)
                                         { this->callback_handle_strategy(notif); });
        }
    }
}

void webserver::pipeline::DummyPipeline::callback_handle_strategy(webserver::resources::ResourceStateChangeNotification notif)
{
    std::string data_string;
    switch (notif.resource_type)
    {
    case webserver::resources::RESOURCE_AI:
    {
        auto state = std::static_pointer_cast<webserver::resources::AiResource::AiResourceState>(notif.resource_state);
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
    case webserver::resources::RESOURCE_FRONTEND:
    case webserver::resources::RESOURCE_ENCODER:
    case webserver::resources::RESOURCE_OSD:
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

void webserver::pipeline::Pipeline::callback_handle_strategy(webserver::resources::ResourceStateChangeNotification notif)
{
    switch (notif.resource_type)
    {
    case webserver::resources::RESOURCE_FRONTEND:
    {
        auto state = std::static_pointer_cast<webserver::resources::ConfigResourceState>(notif.resource_state);
        GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
        g_object_set(frontend, "config-string", state->config.c_str(), NULL);
        gst_object_unref(frontend);
        break;
    }
    case webserver::resources::RESOURCE_ENCODER:
    {
        auto state = std::static_pointer_cast<webserver::resources::ConfigResourceState>(notif.resource_state);
        GstElement *encoder = gst_bin_get_by_name(GST_BIN(m_pipeline), "enc");
        g_object_set(encoder, "config-str", state->config.c_str(), NULL);
        gst_object_unref(encoder);
        break;
    }
    case webserver::resources::RESOURCE_OSD:
    {
        auto state = std::static_pointer_cast<webserver::resources::ConfigResourceState>(notif.resource_state);
        GstElement *osd = gst_bin_get_by_name(GST_BIN(m_pipeline), "osd");
        g_object_set(osd, "config-str", state->config.c_str(), NULL);
        gst_object_unref(osd);
        break;
    }
    case webserver::resources::RESOURCE_AI:
    {
        auto state = std::static_pointer_cast<webserver::resources::AiResource::AiResourceState>(notif.resource_state);

        GstElement *detection = gst_bin_get_by_name(GST_BIN(m_pipeline), "detection");
        gboolean detection_enabled;
        g_object_get(detection, "is-active", &detection_enabled, NULL);
        if (detection_enabled)
        {
            if (std::find(state->disabled.begin(), state->disabled.end(), webserver::resources::AiResource::AI_APPLICATION_DETECTION) == state->disabled.end())
            {
                g_object_set(detection, "is-active", FALSE, NULL);
            }
        }
        else
        {
            if (std::find(state->enabled.begin(), state->enabled.end(), webserver::resources::AiResource::AI_APPLICATION_DETECTION) == state->enabled.end())
            {
                g_object_set(detection, "is-active", TRUE, NULL);
            }
        }
        gst_object_unref(detection);

        break;
    }
    default:
        break;
    }
}