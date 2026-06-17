#pragma once

/**
 * @file gst_source_stage.hpp
 * @brief Analytics pipeline source stage that pulls buffers from GStreamer appsink(s).
 *
 * Replaces FrontendStage for GStreamer-based input. Supports multiple streams
 * via per-stream_id appsinks, and is compatible with PipelineBuilder::connect_frontend().
 **/

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <stddef.h>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <memory>

#include "hailo_analytics/pipeline/core/stage.hpp"
#include "media_library/media_library_types.hpp"

namespace hailo_analytics::pipeline::sources
{

/**
 * @brief Source stage that pulls video frames from GStreamer appsink elements.
 *
 * Each appsink is associated with a stream_id. Downstream stages subscribe to
 * specific streams via add_subscriber(subscriber, stream_id), which is called
 * by PipelineBuilder::connect_frontend().
 *
 * Usage:
 * @code
 * auto source = GstSourceStageBuild::create()
 *     .set_stage_name("gst_source").buildptr();
 * source->add_appsink("sink0", appsink_element);
 * source->add_appsink("sink1", appsink_element2);
 *
 * PipelineBuilder()
 *     .add_stage(source, StageType::SOURCE)
 *     .connect_frontend("gst_source", "sink0", "downstream_stage")
 *     ...
 * @endcode
 */
class GstSourceStage : public hailo_analytics::pipeline::ThreadedStage
{
  protected:
    std::map<output_stream_id_t, GstAppSink *> m_appsinks;
    std::map<output_stream_id_t, std::vector<hailo_analytics::pipeline::StagePtr>> m_stream_subscribers;
    std::vector<std::thread> m_pull_threads;
    std::atomic<bool> m_started;
    std::mutex m_running_mutex;
    std::condition_variable m_running_cv;

  public:
    /**
     * @brief Construct a new GstSourceStage.
     * @param name Stage name (used in PipelineBuilder).
     * @param queue_size Size of the processing queue.
     * @param leaky If true, drops oldest buffer when queue is full.
     * @param trace_processing_operations If true, enables performance tracing.
     */
    GstSourceStage(std::string name, size_t queue_size = 1, bool leaky = false,
                   bool trace_processing_operations = true);

    ~GstSourceStage() override;

    /**
     * @brief Register a GStreamer appsink for a stream. Call before pipeline build.
     * @param stream_id The stream identifier (e.g. "sink0").
     * @param appsink The GStreamer appsink element.
     * @return AppStatus::SUCCESS on success.
     */
    AppStatus add_appsink(output_stream_id_t stream_id, GstElement *appsink);

    /**
     * @brief Subscribe a stage to a specific stream (called by PipelineBuilder::connect_frontend).
     * @param subscriber Downstream stage.
     * @param stream_id Stream identifier (must match an add_appsink call).
     */
    void add_subscriber(StagePtr subscriber, std::optional<std::string> stream_id = std::nullopt) override;

    /**
     * @brief Subscribe a stage to a specific output stream.
     * @param stream_id The stream identifier.
     * @param subscriber Stage to receive buffers from this stream.
     * @return AppStatus::SUCCESS on success.
     */
    AppStatus subscribe_to_stream(output_stream_id_t stream_id, StagePtr subscriber);

    AppStatus init() override;
    AppStatus deinit() override;
    AppStatus stop() override;
    void loop() override;

  private:
    void pull_loop(const std::string &stream_id, GstAppSink *appsink);
};

/**
 * @brief Builder for GstSourceStage construction.
 */
class GstSourceStageBuild : public GstSourceStage
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
        std::shared_ptr<GstSourceStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::sources
