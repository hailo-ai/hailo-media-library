// General includes
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <glib-object.h>
#include <glib.h>
#include <stddef.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

// Media library includes
#include "gsthailobuffermeta.hpp"
#include "media_library/buffer_pool.hpp"
// Infra includes
#include "hailo_analytics/pipeline/sinks/output_module.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::sinks
{

OutputModule::OutputModule(std::string name, EncodingType type, bool print_fps)
    : m_appsrc(nullptr), m_main_loop(nullptr), m_name(name), m_print_fps(print_fps), m_bus(nullptr), m_bus_watch_id(0),
      m_stop_event_received(false), m_type(type), m_pipeline(nullptr)
{
    m_main_loop = g_main_loop_new(NULL, FALSE);
}

OutputModule::~OutputModule()
{
    // Remove bus watch first
    if (m_bus_watch_id)
    {
        g_source_remove(m_bus_watch_id);
        m_bus_watch_id = 0;
    }

    // Stop and cleanup pipeline before unreferencing
    if (m_pipeline != nullptr && GST_MINI_OBJECT_REFCOUNT_VALUE(m_pipeline) > 0)
    {
        // Stop the main loop if running
        if (m_main_loop != nullptr && g_main_loop_is_running(m_main_loop))
        {
            g_main_loop_quit(m_main_loop);
        }

        // Wait for main loop thread to finish
        if (m_main_loop_thread && m_main_loop_thread->joinable())
        {
            m_main_loop_thread->join();
        }

        // Set pipeline to NULL state before cleanup
        gst_element_set_state(m_pipeline, GST_STATE_NULL);

        // Unref pipeline (this will also release its bus reference)
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }

    // Now it's safe to unref our bus reference
    if (m_bus)
    {
        gst_object_unref(m_bus);
        m_bus = nullptr;
    }

    // Finally cleanup the main loop
    if (m_main_loop != nullptr)
    {
        g_main_loop_unref(m_main_loop);
        m_main_loop = nullptr;
    }
}

AppStatus OutputModule::start()
{
    m_stop_event_received = false;

    if (!m_bus)
    {
        m_bus = gst_element_get_bus(m_pipeline);
        if (m_bus)
        {
            m_bus_watch_id = gst_bus_add_watch(m_bus, (GstBusFunc)bus_call, this);
        }
        else
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to get bus from pipeline");
            return AppStatus::PIPELINE_ERROR;
        }
    }

    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to start output pipeline");
        return AppStatus::PIPELINE_ERROR;
    }
    m_main_loop_thread = std::make_shared<std::thread>([this]() { g_main_loop_run(m_main_loop); });

    return AppStatus::SUCCESS;
}

AppStatus OutputModule::stop()
{
    // Check if the pipeline is in a running state before sending EOS
    GstState state, pending;
    GstStateChangeReturn state_ret = gst_element_get_state(m_pipeline, &state, &pending, 0);
    if (state_ret == GST_STATE_CHANGE_FAILURE || state < GST_STATE_PAUSED)
    {
        // Pipeline is not running, just set to NULL and return
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        if (m_main_loop && g_main_loop_is_running(m_main_loop))
        {
            g_main_loop_quit(m_main_loop);
        }
        if (m_main_loop_thread && m_main_loop_thread->joinable())
            m_main_loop_thread->join();
        return AppStatus::SUCCESS;
    }

    gboolean ret = gst_element_send_event(m_pipeline, gst_event_new_eos());
    if (!ret)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to send EOS to output pipeline");
        return AppStatus::PIPELINE_ERROR;
    }

    // Wait for EOS or error to be received via bus watch callback for graceful exit
    {
        std::unique_lock<std::mutex> lock(m_eos_mutex);
        auto timeout = std::chrono::seconds(5);
        if (!m_eos_cv.wait_for(lock, timeout, [this] { return m_stop_event_received; }))
        {
            HAILO_ANALYTICS_LOG_WARN("Timeout waiting for EOS");
        }
    }

    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    g_main_loop_quit(m_main_loop);
    if (m_main_loop_thread && m_main_loop_thread->joinable())
        m_main_loop_thread->join();

    return AppStatus::SUCCESS;
}

/**
 * Print the FPS of the pipeline
 *
 * @note Prints the FPS to the stdout.
 */
void OutputModule::on_fps_measurement(GstElement *fpsdisplaysink, gdouble fps, gdouble droprate, gdouble avgfps)
{
    gchar *name;
    g_object_get(G_OBJECT(fpsdisplaysink), "name", &name, NULL);
    if (m_print_fps)
        std::cout << m_name << ", DROP RATE: " << droprate << " FPS: " << fps << " AVG_FPS: " << avgfps << std::endl;
    HAILO_ANALYTICS_LOG_DEBUG("FPS: {} DROP RATE: {} AVG_FPS: {}", fps, droprate, avgfps);
    g_free(name);
}

/**
 * Set the callbacks
 *
 * @param[in] pipeline        The pipeline as a GstElement.
 */
void OutputModule::set_gst_callbacks(std::string appsrc_name)
{
    // set fps callbacks
    GstElement *fpssink = gst_bin_get_by_name(GST_BIN(m_pipeline), "fpsdisplaysink");
    if (fpssink)
    {
        g_signal_connect(fpssink, "fps-measurements", G_CALLBACK(fps_measurement), this);
        gst_object_unref(fpssink);
    }
    else
    {
        HAILO_ANALYTICS_LOG_WARN("Could not find fpsdisplaysink element");
    }

    GstElement *appsrc = gst_bin_get_by_name(GST_BIN(m_pipeline), appsrc_name.c_str());
    if (!appsrc)
    {
        HAILO_ANALYTICS_LOG_ERROR("Could not find appsrc element with name: {}", appsrc_name);
        m_appsrc = nullptr;
        return;
    }

    m_appsrc = GST_APP_SRC(appsrc);
    gst_object_unref(appsrc);
}

gboolean OutputModule::on_bus_call(GstBus * /*bus*/, GstMessage *msg)
{
    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_EOS: {
        HAILO_ANALYTICS_LOG_DEBUG("Received EOS from pipeline");
        {
            std::lock_guard<std::mutex> lock(m_eos_mutex);
            m_stop_event_received = true;
        }
        m_eos_cv.notify_one();
        break;
    }
    case GST_MESSAGE_ERROR: {
        gchar *debug;
        GError *err;

        gst_message_parse_error(msg, &err, &debug);
        HAILO_ANALYTICS_LOG_ERROR("Received an error message from the output pipeline: {}", err->message);
        g_error_free(err);
        g_free(debug);
        {
            std::lock_guard<std::mutex> lock(m_eos_mutex);
            m_stop_event_received = true;
        }
        m_eos_cv.notify_one();
        break;
    }
    default:
        break;
    }
    return TRUE;
}

AppStatus OutputModule::add_buffer(HailoMediaLibraryBufferPtr ptr, size_t size)
{
    // convert to GstBuffer
    OutputPtrWrapper *wrapper = new OutputPtrWrapper();
    wrapper->ptr = ptr;
    GstBuffer *gst_buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, ptr->get_plane_ptr(0),
                                                        ptr->get_plane_size(0), 0, size, wrapper,
                                                        GDestroyNotify(hailo_media_library_output_release));
    gst_buffer_add_hailo_buffer_meta(gst_buffer, ptr, size);

    GstFlowReturn ret = this->add_buffer_internal(gst_buffer);
    if (ret != GST_FLOW_OK)
    {
        return AppStatus::PIPELINE_ERROR;
    }
    return AppStatus::SUCCESS;
}

GstFlowReturn OutputModule::add_buffer_internal(GstBuffer *buffer)
{
    if (!m_appsrc)
    {
        HAILO_ANALYTICS_LOG_ERROR("m_appsrc is NULL in add_buffer_internal!");
        return GST_FLOW_ERROR;
    }

    if (!buffer)
    {
        HAILO_ANALYTICS_LOG_ERROR("buffer is NULL in add_buffer_internal!");
        return GST_FLOW_ERROR;
    }

    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buffer);
    if (ret != GST_FLOW_OK)
    {
        GST_ERROR_OBJECT(m_appsrc, "Failed to push buffer to appsrc");
        return ret;
    }
    return ret;
}

gboolean OutputModule::bus_call(GstBus *bus, GstMessage *msg, gpointer user_data)
{
    OutputModule *output_module = static_cast<OutputModule *>(user_data);
    return output_module->on_bus_call(bus, msg);
}

void OutputModule::fps_measurement(GstElement *fpssink, gdouble fps, gdouble droprate, gdouble avgfps,
                                   gpointer user_data)
{
    OutputModule *output_module = static_cast<OutputModule *>(user_data);
    output_module->on_fps_measurement(fpssink, fps, droprate, avgfps);
}

void hailo_media_library_output_release(OutputPtrWrapper *wrapper)
{
    delete wrapper;
}

} // namespace hailo_analytics::pipeline::sinks
