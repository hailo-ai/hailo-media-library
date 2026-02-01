#pragma once

// General includes
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <tl/expected.hpp>
#include <functional>
#include <iostream>
#include <queue>
#include <thread>
#include <vector>

// Media library includes
#include "gsthailobuffermeta.hpp"
#include "media_library/buffer_pool.hpp"
#include "media_library/media_library_types.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

// Defines
#define SRC_QUEUE_NAME "appsrc_q"

namespace hailo_analytics::pipeline::sinks
{

enum class EncodingType
{
    H264 = 0,
    H265,
};

class OutputModule;
using OutputModulePtr = std::shared_ptr<OutputModule>;

class OutputModule
{
  private:
    GstAppSrc *m_appsrc;
    GMainLoop *m_main_loop;
    std::shared_ptr<std::thread> m_main_loop_thread;
    std::string m_name;
    bool m_print_fps;
    GstBus *m_bus;
    guint m_bus_watch_id;
    std::mutex m_eos_mutex;
    std::condition_variable m_eos_cv;
    bool m_stop_event_received;

  protected:
    EncodingType m_type;
    GstElement *m_pipeline;

  public:
    virtual ~OutputModule();
    OutputModule(std::string name, EncodingType type, bool print_fps);
    AppStatus start();
    AppStatus stop();
    AppStatus add_buffer(HailoMediaLibraryBufferPtr ptr, size_t size);
    void on_fps_measurement(GstElement *fpssink, gdouble fps, gdouble droprate, gdouble avgfps);
    gboolean on_bus_call(GstBus *bus, GstMessage *msg);
    static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer user_data)
    {
        OutputModule *output_module = static_cast<OutputModule *>(user_data);
        return output_module->on_bus_call(bus, msg);
    }
    void set_gst_callbacks(std::string source);

  private:
    static void fps_measurement(GstElement *fpssink, gdouble fps, gdouble droprate, gdouble avgfps, gpointer user_data)
    {
        OutputModule *output_module = static_cast<OutputModule *>(user_data);
        output_module->on_fps_measurement(fpssink, fps, droprate, avgfps);
    }
    GstFlowReturn add_buffer_internal(GstBuffer *buffer);
};

struct OutputPtrWrapper
{
    HailoMediaLibraryBufferPtr ptr;
};

static inline void hailo_media_library_output_release(OutputPtrWrapper *wrapper)
{
    delete wrapper;
}

} // namespace hailo_analytics::pipeline::sinks
