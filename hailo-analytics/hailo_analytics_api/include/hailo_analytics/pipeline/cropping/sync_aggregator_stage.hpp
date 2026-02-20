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

/**
 * @brief Synchronized aggregator stage with timeout and adaptive synchronization
 *
 * Extends AggregatorStage to handle synchronized streams with timeout management
 * and adaptive timeout adjustment based on drop rate. This stage waits for subframes
 * with a configurable timeout and can automatically adjust the timeout based on
 * frame drop statistics to optimize performance.
 *
 * Features:
 * - Configurable timeout for subframe synchronization
 * - Adaptive timeout adjustment based on drop rate
 * - Min/max timeout bounds
 * - Drop rate monitoring and blocking
 */
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
    /**
     * @brief Constructor for SyncAggregatorStage
     * @param name Stage name for identification
     * @param main_inlet_name Name of the main frame inlet
     * @param main_queue_size Size of the main frame queue
     * @param main_queue_leaky If true, main queue drops old frames when full
     * @param sub_inlet_name Name of the subframe inlet
     * @param sub_queue_size Size of the subframe queue
     * @param sub_queue_leaky If true, subframe queue drops old frames when full
     * @param multi_scale Enable multi-scale aggregation (default: false)
     * @param iou_threshold IOU threshold for NMS (default: 0.3)
     * @param m_border_threshold Border threshold for removing edge detections (default: 0.1)
     * @param skip_migration Skip metadata migration (default: false)
     * @param trace_processing_operations Enable tracing (default: true)
     * @param static_sub_frames Fixed number of subframes, if known (default: nullopt)
     * @param timeout Timeout for waiting for subframes (default: nullopt)
     * @param min_timeout Minimum timeout value for adaptive adjustment (default: nullopt)
     * @param max_timeout Maximum timeout value for adaptive adjustment (default: nullopt)
     * @param timeout_adjustment_period Period for timeout adjustment (default: 1000ms)
     * @param drop_rate_threshold Threshold for drop rate monitoring (default: 0.2)
     * @param timeout_step_size Step size for timeout adjustments (default: 10ms)
     * @param drop_rate_block Block on high drop rate (default: false)
     */
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

    /**
     * @brief Adjust timeout based on drop rate statistics
     */
    void timeout_adjustment();

    /**
     * @brief Get subframes for a main buffer with timeout
     * @param main_buffer Main frame buffer
     * @param num_subframes Number of subframes to retrieve
     * @return Expected containing vector of subframes or status
     */
    virtual tl::expected<std::vector<BufferPtr>, SubframeStatus> get_subframes(BufferPtr main_buffer,
                                                                               int num_subframes) override;
};

/**
 * @brief Builder pattern implementation for SyncAggregatorStage
 *
 * Provides a fluent interface for constructing SyncAggregatorStage instances
 * with configurable parameters including timeout management.
 */
class SyncAggregatorStageBuild : public SyncAggregatorStage
{
  public:
    /**
     * @brief Builder class for constructing SyncAggregatorStage instances
     */
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

        /**
         * @brief Set the subframe wait timeout
         * @param timeout Timeout duration for waiting for subframes
         * @return Builder reference for chaining
         */
        Builder &set_timeout_opt(std::chrono::milliseconds timeout);

        /**
         * @brief Set the minimum timeout for adaptive adjustment
         * @param min_timeout Minimum timeout value
         * @return Builder reference for chaining
         */
        Builder &set_min_timeout_opt(std::chrono::milliseconds min_timeout);

        /**
         * @brief Set the maximum timeout for adaptive adjustment
         * @param max_timeout Maximum timeout value
         * @return Builder reference for chaining
         */
        Builder &set_max_timeout_opt(std::chrono::milliseconds max_timeout);

        /**
         * @brief Set the period for timeout adjustments
         * @param period Period duration between adjustments
         * @return Builder reference for chaining
         */
        Builder &set_timeout_adjustment_period_opt(std::chrono::milliseconds period);

        /**
         * @brief Set the drop rate threshold
         * @param threshold Drop rate threshold (0.0 to 1.0)
         * @return Builder reference for chaining
         */
        Builder &set_drop_rate_threshold_opt(float threshold);

        /**
         * @brief Set the timeout step size for adjustments
         * @param step_size Step size for timeout changes
         * @return Builder reference for chaining
         */
        Builder &set_timeout_step_size_opt(std::chrono::milliseconds step_size);

        /**
         * @brief Set whether to block on high drop rate
         * @param block If true, blocks when drop rate exceeds threshold
         * @return Builder reference for chaining
         */
        Builder &set_drop_rate_block_opt(bool block);

        /**
         * @brief Build and return shared pointer to SyncAggregatorStage
         * @return Shared pointer to constructed SyncAggregatorStage
         * @throws std::runtime_error if required parameters are missing
         */
        std::shared_ptr<SyncAggregatorStage> buildptr() const;
    };

    /**
     * @brief Create a new Builder instance
     * @return Builder instance for constructing SyncAggregatorStage
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::cropping
