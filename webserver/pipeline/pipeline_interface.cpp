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

IPipeline::IPipeline(WebserverResourceRepository resources)
{
    m_resources = resources;

    gst_init(nullptr, nullptr);
}

IPipeline::~IPipeline()
{
    gst_object_unref(m_pipeline);
}

void IPipeline::start()
{
    std::cout << "Starting pipeline" << std::endl;
    m_pipeline = gst_parse_launch(this->create_gst_pipeline_string().c_str(), NULL);
    if (!m_pipeline)
    {
        std::cout << "Failed to create pipeline" << std::endl;
        throw std::runtime_error("Failed to create pipeline");
    }

    std::cout << "Setting pipeline to PLAYING" << std::endl;
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        std::cout << "Failed to start pipeline" << std::endl;
        throw std::runtime_error("Failed to start pipeline");
    }
    std::cout << "Pipeline started" << std::endl;
}

void IPipeline::stop()
{
    std::cout << "Stopping pipeline" << std::endl;
    gboolean ret = gst_element_send_event(m_pipeline, gst_event_new_eos());
    if (!ret)
    {
        std::cout << "Failed to send EOS event" << std::endl;
        throw std::runtime_error("Failed to send EOS event");
    }

    std::cout << "Setting pipeline to NULL" << std::endl;
    gst_element_set_state(m_pipeline, GST_STATE_NULL);

    gst_object_unref(m_pipeline);
    std::cout << "Pipeline stopped" << std::endl;
}
