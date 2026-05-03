#pragma once

/**
 * @file gst_sink_stage.hpp
 * @brief Analytics pipeline sink stage that pushes buffers to a GStreamer appsrc.
 *
 * Receives analytics pipeline buffers and converts them to GstBuffers for
 * a GStreamer appsrc element (e.g. feeding into gsthailoencoder).
 **/

#include <optional>
#include <string>

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

#include "media_library/media_library_types.hpp"

namespace hailo_analytics::pipeline::sinks
{

/**
 * @brief Sink stage that pushes analytics buffers to a GStreamer appsrc.
 *
 * Converts HailoMediaLibraryBufferPtr to GstBuffer and pushes via
 * gst_app_src_push_buffer(). Attaches GstHailoBufferMeta and optionally
 * GstHailoROIMeta for downstream GStreamer elements.
 *
 * Usage:
 * @code
 * auto sink = GstSinkStageBuild::create()
 *     .set_stage_name("gst_output").buildptr();
 * sink->configure(appsrc_element);
 *
 * PipelineBuilder()
 *     ...
 *     .add_stage(sink, StageType::SINK)
 *     .connect("upstream", "gst_output")
 *     ...
 * @endcode
 */
class GstSinkStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    GstAppSrc *m_appsrc = nullptr;
    bool m_caps_set = false;

  public:
    /**
     * @brief Construct a new GstSinkStage.
     * @param name Stage name (used in PipelineBuilder).
     * @param queue_size Size of the processing queue.
     * @param leaky If true, drops oldest buffer when queue is full.
     * @param trace_processing_operations If true, enables performance tracing.
     */
    GstSinkStage(std::string name, size_t queue_size = 1, bool leaky = false, bool trace_processing_operations = true);

    /**
     * @brief Set the GStreamer appsrc to push buffers to. Call before pipeline start.
     * @param appsrc The GStreamer appsrc element.
     * @return AppStatus::SUCCESS on success.
     */
    AppStatus configure(GstElement *appsrc);

    /**
     * @brief Process a buffer by pushing it to the appsrc.
     * @param data Buffer from the analytics pipeline.
     * @return AppStatus::SUCCESS on success.
     */
    AppStatus process(BufferPtr data) override;
};

/**
 * @brief Builder for GstSinkStage construction.
 */
class GstSinkStageBuild : public GstSinkStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = 1;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_trace_opt(bool activate);
        std::shared_ptr<GstSinkStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::sinks
