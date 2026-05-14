#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/perfetto/hailo_analytics_perfetto.hpp"
#include "hailo_analytics/pipeline/cropping/sync_aggregator_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::cropping
{

// Internal class for Perfetto tracing to maintain ABI compatibility
class AggTracing
{
  private:
#ifdef HAVE_PERFETTO
    std::string m_counter_name_drop_rate;
    std::string m_counter_name_timeout;
#endif

  public:
    AggTracing([[maybe_unused]] const std::string &name)
#ifdef HAVE_PERFETTO
        : m_counter_name_drop_rate("aggregator_" + name + "_drop_rate"),
          m_counter_name_timeout("aggregator_" + name + "_timeout")
#endif
    {
    }

    void track_drop_rate([[maybe_unused]] float drop_rate)
    {
        HAILO_ANALYTICS_TRACE_COUNTER(m_counter_name_drop_rate, drop_rate, HAILO_ANALYTICS_PROCESSING_TRACK,
                                      HAILO_ANALYTICS_DETAILED_CATEGORY);
    }

    void track_timeout([[maybe_unused]] std::chrono::milliseconds timeout)
    {
        HAILO_ANALYTICS_TRACE_COUNTER(m_counter_name_timeout, timeout.count(), HAILO_ANALYTICS_PROCESSING_TRACK,
                                      HAILO_ANALYTICS_DETAILED_CATEGORY);
    }
};

SyncAggregatorStage::SyncAggregatorStage(std::string name, std::string main_inlet_name, size_t main_queue_size,
                                         bool main_queue_leaky, std::string sub_inlet_name, size_t sub_queue_size,
                                         bool sub_queue_leaky, bool multi_scale, float iou_threshold,
                                         float m_border_threshold, bool skip_migration,
                                         bool trace_processing_operations, std::optional<int> static_sub_frames,
                                         std::optional<std::chrono::milliseconds> timeout,
                                         std::optional<std::chrono::milliseconds> min_timeout,
                                         std::optional<std::chrono::milliseconds> max_timeout,
                                         std::chrono::milliseconds timeout_adjustment_period, float drop_rate_threshold,
                                         std::chrono::milliseconds timeout_step_size, bool drop_rate_block)
    : AggregatorStage(name, main_inlet_name, main_queue_size, main_queue_leaky, sub_inlet_name, sub_queue_size,
                      sub_queue_leaky, multi_scale, iou_threshold, m_border_threshold, skip_migration,
                      trace_processing_operations, static_sub_frames),
      m_timeout(timeout), m_min_timeout(min_timeout), m_max_timeout(max_timeout),
      m_timeout_adjustment_period(timeout_adjustment_period), m_drop_rate_threshold(drop_rate_threshold),
      m_timeout_step_size(timeout_step_size), m_drop_rate_block(drop_rate_block),
      m_agg_tracing(std::make_unique<AggTracing>(name))
{
}

void SyncAggregatorStage::timeout_adjustment()
{
    const auto time_now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    if (time_now - m_last_timeout_adjustment < m_timeout_adjustment_period)
    {
        return;
    }

    const float drop_rate = static_cast<float>(m_dropped_frames) / static_cast<float>(m_processed_frames);
    m_agg_tracing->track_drop_rate(drop_rate);
    if (m_timeout)
    {
        if (m_min_timeout && m_max_timeout)
        {
            auto t = m_timeout.value();
            const auto &low = m_min_timeout.value();
            const auto &high = m_max_timeout.value();
            if (drop_rate > m_drop_rate_threshold)
            {
                t += m_timeout_step_size;
            }
            else if (drop_rate < m_drop_rate_threshold)
            {
                t -= m_timeout_step_size;
            }
            m_timeout = std::clamp(t, low, high);
        }
        m_agg_tracing->track_timeout(m_timeout.value());
    }
    m_drop_rate = drop_rate;
    m_dropped_frames = 0;
    m_processed_frames = 0;
    m_last_timeout_adjustment = time_now;
}

tl::expected<std::vector<BufferPtr>, SubframeStatus> SyncAggregatorStage::get_subframes(BufferPtr main_buffer,
                                                                                        int num_subframes)
{
    std::vector<BufferPtr> subframes;
    if (num_subframes == 1)
    {
        std::optional<std::chrono::milliseconds> timeout = m_timeout;
        if (m_first_sync)
        {
            m_first_sync = false;
            timeout = std::nullopt;
        }
        if (m_drop_rate_block && m_drop_rate >= 0.99f)
        {
            HAILO_ANALYTICS_LOG_WARN("[{}] drop rate 100%, allowing indefinite timeout until recovered.", m_stage_name);
            timeout = std::nullopt;
        }

        uint64_t mainframe_timestamp = main_buffer->get_buffer()->isp_timestamp_ns;
        auto samples_start =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
        uint64_t subframe_timestamp = m_queues[1]->check_timestamp(timeout);
        auto sampling_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()) -
            samples_start;
        if (subframe_timestamp == 0)
        {
            // if we reached here, then the queue is empty and we are flushing (or timed out)
            return tl::make_unexpected(SubframeStatus::TIMEOUT);
        }

        // main frame is newer than sub frame
        while ((mainframe_timestamp > subframe_timestamp) && (subframe_timestamp != 0))
        {
            // drop the oldest subframe
            m_queues[1]->pop();
            m_processed_frames++;
            m_dropped_frames++;
            // check the next subframe
            if (timeout.has_value())
            {
                timeout = m_timeout.value() - sampling_time;
            }
            if (timeout.has_value() && timeout <= std::chrono::milliseconds(0))
            {
                subframe_timestamp = 0; // skip if acumulated time is more than originally requested
            }
            else
            {
                subframe_timestamp = m_queues[1]->check_timestamp(timeout);
                sampling_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch()) -
                                samples_start;
            }
        }

        // timestamps match
        if (mainframe_timestamp == subframe_timestamp)
        {
            subframes.push_back(m_queues[1]->pop());
            m_processed_frames++;
            if (subframes[0] == nullptr && m_end_of_stream)
            {
                m_tracing->trace_processing_end(main_buffer);
                return tl::make_unexpected(SubframeStatus::END_OF_STREAM);
            }
        }
        else
        {
            // timestamps don't match, main frame is older
            num_subframes = 0; // pass the main frame as is
        }

        timeout_adjustment();
    }
    return subframes;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_static_subframes_opt(int num)
{
    m_static_sub_frames = num;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_main_inlet_name(std::string name)
{
    m_main_inlet_name = name;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_main_queue_size(size_t size)
{
    m_main_queue_size = size;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_main_leaky(bool leaky)
{
    m_main_queue_leaky = leaky;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_sub_inlet_name(std::string name)
{
    m_sub_inlet_name = name;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_sub_queue_size(size_t size)
{
    m_sub_queue_size = size;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_sub_leaky(bool leaky)
{
    m_sub_queue_leaky = leaky;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_multiscale_opt(bool multi_scale)
{
    m_multi_scale = multi_scale;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_skip_migration_opt(bool skip)
{
    m_skip_migration = skip;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_iou_threshold_opt(float threshold)
{
    m_iou_threshold = threshold;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_border_threshold_opt(float threshold)
{
    m_border_threshold = threshold;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_timeout_opt(std::chrono::milliseconds timeout)
{
    m_timeout = timeout;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_min_timeout_opt(
    std::chrono::milliseconds min_timeout)
{
    m_min_timeout = min_timeout;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_max_timeout_opt(
    std::chrono::milliseconds max_timeout)
{
    m_max_timeout = max_timeout;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_timeout_adjustment_period_opt(
    std::chrono::milliseconds period)
{
    m_timeout_adjustment_period = period;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_drop_rate_threshold_opt(float threshold)
{
    m_drop_rate_threshold = threshold;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_timeout_step_size_opt(
    std::chrono::milliseconds step_size)
{
    m_timeout_step_size = step_size;
    return *this;
}

SyncAggregatorStageBuild::Builder &SyncAggregatorStageBuild::Builder::set_drop_rate_block_opt(bool block)
{
    m_drop_rate_block = block;
    return *this;
}

std::shared_ptr<SyncAggregatorStage> SyncAggregatorStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING(m_main_inlet_name.has_value(), "set_main_inlet_name");
    THROW_IF_MISSING(m_sub_inlet_name.has_value(), "set_sub_inlet_name");

    return std::make_shared<SyncAggregatorStage>(
        m_stage_name.value(), m_main_inlet_name.value(), m_main_queue_size, m_main_queue_leaky,
        m_sub_inlet_name.value(), m_sub_queue_size, m_sub_queue_leaky, m_multi_scale, m_iou_threshold,
        m_border_threshold, m_skip_migration, m_trace, m_static_sub_frames, m_timeout, m_min_timeout, m_max_timeout,
        m_timeout_adjustment_period, m_drop_rate_threshold, m_timeout_step_size, m_drop_rate_block);
}

SyncAggregatorStageBuild::Builder SyncAggregatorStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::cropping
