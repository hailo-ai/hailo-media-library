#include "pipeline/pipeline.hpp"
#include "common/common.hpp"
#include "resources/resources.hpp"
#include <gst/gst.h>

using namespace webserver::pipeline;
using namespace webserver::resources;

std::shared_ptr<IPipeline> IPipeline::create()
{
#ifndef MEDIALIB_LOCAL_SERVER
    std::cout << "Creating pipeline" << std::endl;
    return Pipeline::create();
#else
    std::cout << "Creating dummy pipeline" << std::endl;
    return DummyPipeline::create();
#endif
}

IPipeline::IPipeline(WebserverResourceRepository resources, std::string gst_pipeline_str)
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
}

IPipeline::~IPipeline()
{
    gst_object_unref(m_pipeline);
}

GstFlowReturn IPipeline::wait_for_end_of_pipeline()
{
    GstBus *bus;
    GstMessage *msg;
    GstFlowReturn ret = GST_FLOW_ERROR;
    bus = gst_element_get_bus(m_pipeline);
    gboolean done = FALSE;

    // This function blocks until an error or EOS message is received.
    while (!done)
    {
        msg = gst_bus_timed_pop_filtered(bus, GST_MSECOND * 250, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

        if (msg != NULL)
        {
            std::cout << "Message type: " << GST_MESSAGE_TYPE(msg) << std::endl;

            GError *err;
            gchar *debug_info;
            done = TRUE;
            switch (GST_MESSAGE_TYPE(msg))
            {
            case GST_MESSAGE_ERROR:
            {
                gst_message_parse_error(msg, &err, &debug_info);
                GST_ERROR("Error received from element %s: %s", GST_OBJECT_NAME(msg->src), err->message);

                std::string dinfo = debug_info ? std::string(debug_info) : "none";
                GST_ERROR("Debugging information : %s", dinfo.c_str());

                g_clear_error(&err);
                g_free(debug_info);
                ret = GST_FLOW_ERROR;
                break;
            }
            case GST_MESSAGE_EOS:
            {
                GST_ERROR("End-Of-Stream reached");
                ret = GST_FLOW_OK;
                break;
            }
            default:
            {
                // We should not reach here because we only asked for ERRORs and EOS
                GST_WARNING("Unexpected message received %d", GST_MESSAGE_TYPE(msg));
                ret = GST_FLOW_ERROR;
                break;
            }
            }
            gst_message_unref(msg);
        }
    }

    std::cout << "Pipeling ended!" << std::endl;

    gst_object_unref(bus);
    return ret;
}

void IPipeline::start()
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
        {
            wait_for_end_of_pipeline();
        });
}

void IPipeline::stop()
{
    gboolean ret = gst_element_send_event(m_pipeline, gst_event_new_eos());
    if (!ret)
    {
        std::cout << "Failed to send EOS event" << std::endl;
        throw std::runtime_error("Failed to send EOS event");
    }

    m_main_loop_thread->join();

    gst_element_set_state(m_pipeline, GST_STATE_NULL);
}
