#pragma once

/**
 * @file synced_aggregator_stage.hpp
 * @brief Stage that extends the AggregatorStage to support synchronized streams of cropped video frames.
 **/

// Infra includes
#include "hailo_analytics/pipeline/cropping/aggregator_stage.hpp"

namespace hailo_analytics::pipeline::cropping
{

// Forward declaration
class AggTracing;

class SyncAggregatorStage : public AggregatorStage
{
  protected:
    std::optional<std::chrono::milliseconds> m_timeout;
    std::optional<std::chrono::milliseconds> m_min_timeout;
    std::optional<std::chrono::milliseconds> m_max_timeout;
    bool m_first_sync = true;
    int m_dropped_frames = 0;
    int m_processed_frames = 0;
    float m_drop_rate = 0.0f;
    std::chrono::milliseconds m_last_timeout_adjustment = std::chrono::milliseconds(0);
    std::chrono::milliseconds m_timeout_adjustment_period;
    float m_drop_rate_threshold;
    std::chrono::milliseconds m_timeout_step_size;
    bool m_drop_rate_block;
    std::unique_ptr<AggTracing> m_agg_tracing;

  public:
    SyncAggregatorStage(std::string name, std::string main_inlet_name, size_t main_queue_size, bool main_queue_leaky,
                        std::string sub_inlet_name, size_t sub_queue_size, bool sub_queue_leaky,
                        bool multi_scale = false, float iou_threshold = 0.3, float m_border_threshold = 0.1,
                        bool skip_migration = false, bool trace_processing_operations = true,
                        std::optional<int> static_sub_frames = std::nullopt,
                        std::optional<std::chrono::milliseconds> timeout = std::nullopt,
                        std::optional<std::chrono::milliseconds> min_timeout = std::nullopt,
                        std::optional<std::chrono::milliseconds> max_timeout = std::nullopt,
                        std::chrono::milliseconds timeout_adjustment_period = std::chrono::milliseconds(1000),
                        float drop_rate_threshold = 0.2,
                        std::chrono::milliseconds timeout_step_size = std::chrono::milliseconds(10),
                        bool drop_rate_block = false);

    void timeout_adjustment();
    virtual tl::expected<std::vector<BufferPtr>, SubframeStatus> get_subframes(BufferPtr main_buffer,
                                                                               int num_subframes) override;
};

class SyncAggregatorStageBuild : public SyncAggregatorStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::optional<int> m_static_sub_frames = std::nullopt;
        std::optional<std::string> m_main_inlet_name;
        size_t m_main_queue_size = 10;
        bool m_main_queue_leaky = false;

        std::optional<std::string> m_sub_inlet_name;
        size_t m_sub_queue_size = 10;
        bool m_sub_queue_leaky = false;
        bool m_multi_scale = false;
        bool m_skip_migration = false;
        bool m_trace = true;
        float m_iou_threshold = 0.3;
        float m_border_threshold = 0.1;

        std::optional<std::chrono::milliseconds> m_timeout = std::nullopt;
        std::optional<std::chrono::milliseconds> m_min_timeout = std::nullopt;
        std::optional<std::chrono::milliseconds> m_max_timeout = std::nullopt;
        std::chrono::milliseconds m_timeout_adjustment_period = std::chrono::milliseconds(1000);
        float m_drop_rate_threshold = 0.2;
        std::chrono::milliseconds m_timeout_step_size = std::chrono::milliseconds(10);
        bool m_drop_rate_block = false;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_static_subframes_opt(int num);
        Builder &set_main_inlet_name(std::string name);
        Builder &set_main_queue_size(size_t size);
        Builder &set_main_leaky(bool leaky);
        Builder &set_sub_inlet_name(std::string name);
        Builder &set_sub_queue_size(size_t size);
        Builder &set_sub_leaky(bool leaky);
        Builder &set_multiscale_opt(bool multi_scale);
        Builder &set_skip_migration_opt(bool skip);
        Builder &set_trace_opt(bool activate);
        Builder &set_iou_threshold_opt(float threshold);
        Builder &set_border_threshold_opt(float threshold);
        Builder &set_timeout_opt(std::chrono::milliseconds timeout);
        Builder &set_min_timeout_opt(std::chrono::milliseconds min_timeout);
        Builder &set_max_timeout_opt(std::chrono::milliseconds max_timeout);
        Builder &set_timeout_adjustment_period_opt(std::chrono::milliseconds period);
        Builder &set_drop_rate_threshold_opt(float threshold);
        Builder &set_timeout_step_size_opt(std::chrono::milliseconds step_size);
        Builder &set_drop_rate_block_opt(bool block);

        std::shared_ptr<SyncAggregatorStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::cropping
