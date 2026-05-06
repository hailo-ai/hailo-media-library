#include "full_frame_bbox_injector_stage.hpp"

#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

FullFrameBBoxInjectorStage::FullFrameBBoxInjectorStage(std::string name, std::string class_name, float interval_seconds,
                                                       size_t queue_size, bool leaky, bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
      m_class_name(std::move(class_name)), m_interval_seconds(interval_seconds), m_first_frame(true),
      m_last_inject_time()
{
}

hailo_analytics::pipeline::AppStatus FullFrameBBoxInjectorStage::init()
{
    return hailo_analytics::pipeline::AppStatus::SUCCESS;
}

hailo_analytics::pipeline::AppStatus FullFrameBBoxInjectorStage::deinit()
{
    return hailo_analytics::pipeline::AppStatus::SUCCESS;
}

hailo_analytics::pipeline::AppStatus FullFrameBBoxInjectorStage::process(BufferPtr data)
{
    auto now = std::chrono::steady_clock::now();
    bool should_inject = m_first_frame;
    if (!m_first_frame)
    {
        auto elapsed = std::chrono::duration<float>(now - m_last_inject_time).count();
        should_inject = (elapsed >= m_interval_seconds);
    }

    if (should_inject)
    {
        m_first_frame = false;
        m_last_inject_time = now;

        auto roi = data->get_roi();
        if (roi)
        {
            HailoDetectionPtr detection =
                std::make_shared<HailoDetection>(HailoBBox(0.0f, 0.0f, 1.0f, 1.0f), 0, m_class_name, 1.0f);

            // FaissStorageStage requires a tracking ID on every detection — without it, the
            // embedding is silently skipped. INT32_MAX is a fixed ID shared by all full-frame
            // detections, which also enables deduplication in query results.
            int32_t track_id = INT32_MAX;
            detection->add_object(std::make_shared<HailoUniqueID>(track_id, TRACKING_ID));

            roi->add_object(detection);

            HAILO_ANALYTICS_LOG_DEBUG("FullFrameBBoxInjectorStage {} injected full-frame bbox with track_id {}",
                                      m_stage_name, track_id);
        }
    }

    send_to_subscribers(data);
    return hailo_analytics::pipeline::AppStatus::SUCCESS;
}

// Builder implementation

FullFrameBBoxInjectorStageBuild::Builder &FullFrameBBoxInjectorStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

FullFrameBBoxInjectorStageBuild::Builder &FullFrameBBoxInjectorStageBuild::Builder::set_full_frame_class_name(
    std::string class_name)
{
    m_class_name = class_name;
    return *this;
}

FullFrameBBoxInjectorStageBuild::Builder &FullFrameBBoxInjectorStageBuild::Builder::set_interval_seconds(float seconds)
{
    m_interval_seconds = seconds;
    return *this;
}

FullFrameBBoxInjectorStageBuild::Builder &FullFrameBBoxInjectorStageBuild::Builder::set_queue_size(size_t size)
{
    m_queue_size = size;
    return *this;
}

FullFrameBBoxInjectorStageBuild::Builder &FullFrameBBoxInjectorStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

FullFrameBBoxInjectorStageBuild::Builder &FullFrameBBoxInjectorStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<FullFrameBBoxInjectorStage> FullFrameBBoxInjectorStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING(m_class_name.has_value(), "set_full_frame_class_name");

    return std::make_shared<FullFrameBBoxInjectorStage>(m_stage_name.value(), m_class_name.value(), m_interval_seconds,
                                                        m_queue_size, m_leaky, m_trace);
}

FullFrameBBoxInjectorStageBuild::Builder FullFrameBBoxInjectorStageBuild::create()
{
    return Builder();
}
