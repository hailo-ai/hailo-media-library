#include "hailo_analytics/pipeline/sinks/gst_sink_stage.hpp"

#include <glib-object.h>
#include <glib.h>
#include <hailo_postprocess_tools/objects/hailo_objects.hpp>
#include <media_library/buffer_pool.hpp>
#include <utility>

#include "gsthailobuffermeta.hpp"
#include "hailo_analytics/pipeline/sinks/gsthailoroimeta.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

namespace hailo_analytics::pipeline::sinks
{

struct BufferPtrWrapper
{
    HailoMediaLibraryBufferPtr ptr;
};

static void buffer_ptr_wrapper_release(BufferPtrWrapper *wrapper)
{
    delete wrapper;
}

GstSinkStage::GstSinkStage(std::string name, size_t queue_size, bool leaky, bool trace_processing_operations)
    : ThreadedStage(std::move(name), queue_size, leaky, trace_processing_operations)
{
}

AppStatus GstSinkStage::configure(GstElement *appsrc)
{
    if (!appsrc || !GST_IS_APP_SRC(appsrc))
    {
        HAILO_ANALYTICS_LOG_ERROR("GstSinkStage '{}': invalid appsrc element", m_stage_name);
        return AppStatus::INVALID_ARGUMENT;
    }

    m_appsrc = GST_APP_SRC(appsrc);
    HAILO_ANALYTICS_LOG_INFO("GstSinkStage '{}': configured with appsrc", m_stage_name);
    return AppStatus::SUCCESS;
}

AppStatus GstSinkStage::process(BufferPtr data)
{
    HAILO_ANALYTICS_LOG_DEBUG("GstSinkStage '{}': process() called", m_stage_name);

    if (!m_appsrc)
    {
        HAILO_ANALYTICS_LOG_ERROR("GstSinkStage '{}': appsrc not configured. Call configure()", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }

    HailoMediaLibraryBufferPtr hailo_buf = data->get_buffer();
    if (!hailo_buf)
    {
        HAILO_ANALYTICS_LOG_ERROR("GstSinkStage '{}': analytics buffer has null HailoMediaLibraryBuffer", m_stage_name);
        return AppStatus::PIPELINE_ERROR;
    }

    // Set caps on first buffer so downstream elements (e.g. gsthailoencoder) can negotiate format
    if (!m_caps_set)
    {
        auto width = hailo_buf->owner->get_width();
        auto height = hailo_buf->owner->get_height();
        GstCaps *caps =
            gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12", "width", G_TYPE_INT, (gint)width,
                                "height", G_TYPE_INT, (gint)height, "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
        gst_app_src_set_caps(m_appsrc, caps);
        gst_caps_unref(caps);
        m_caps_set = true;
        HAILO_ANALYTICS_LOG_INFO("GstSinkStage '{}': set caps {}x{} NV12 on appsrc", m_stage_name, width, height);
    }

    // Create GstBuffer wrapping the HailoMediaLibraryBuffer's memory.
    // Uses the same pattern as OutputModule::add_buffer() in reference_camera_api.
    auto *wrapper = new BufferPtrWrapper{hailo_buf};
    size_t plane_size = hailo_buf->get_plane_size(0);

    GstBuffer *gst_buf =
        gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, hailo_buf->get_plane_ptr(0), plane_size, 0,
                                    plane_size, wrapper, GDestroyNotify(buffer_ptr_wrapper_release));
    if (!gst_buf)
    {
        HAILO_ANALYTICS_LOG_ERROR("GstSinkStage '{}': failed to create GstBuffer", m_stage_name);
        delete wrapper;
        return AppStatus::PIPELINE_ERROR;
    }

    // Attach HailoBufferMeta so downstream GStreamer elements can access the original buffer
    gst_buffer_add_hailo_buffer_meta(gst_buf, hailo_buf, plane_size);

    // Attach ROI metadata with detection results (if available)
    HailoROIPtr roi = data->get_roi();
    if (roi)
    {
        gst_buffer_add_hailo_roi_meta(gst_buf, roi);
    }

    // Push to appsrc — transfers ownership of gst_buf
    HAILO_ANALYTICS_LOG_DEBUG("GstSinkStage '{}': pushing buffer to appsrc (plane_size={})", m_stage_name, plane_size);
    GstFlowReturn ret = gst_app_src_push_buffer(m_appsrc, gst_buf);
    if (ret != GST_FLOW_OK)
    {
        HAILO_ANALYTICS_LOG_ERROR("GstSinkStage '{}': failed to push buffer to appsrc (ret={})", m_stage_name,
                                  (int)ret);
        return AppStatus::PIPELINE_ERROR;
    }

    HAILO_ANALYTICS_LOG_DEBUG("GstSinkStage '{}': buffer pushed successfully", m_stage_name);
    return AppStatus::SUCCESS;
}

GstSinkStageBuild::Builder &GstSinkStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = std::move(name);
    return *this;
}

GstSinkStageBuild::Builder &GstSinkStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}

GstSinkStageBuild::Builder &GstSinkStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

GstSinkStageBuild::Builder &GstSinkStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<GstSinkStage> GstSinkStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    return std::make_shared<GstSinkStage>(m_stage_name.value(), m_queue_size, m_leaky, m_trace);
}

GstSinkStageBuild::Builder GstSinkStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::sinks
