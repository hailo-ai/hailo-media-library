#pragma once

// General includes
#include <cstddef>

// Postprocess Tools includes
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/tracking/hailo_tracker.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

#define TRACKER_UNCLASSIFIED_FPS_BLOCK_COUNT_BEFORE_PASS (1)
#define TRACKER_CLASSIFIED_FPS_BLOCK_COUNT_BEFORE_PASS (1)
#define TRACKER_TRAFFIC_QUEUE_SIZE_DEFAULT (5)
#define CLEAN_UP_INTERVAL_SEC (10) // Clean up every X seconds

namespace hailo_analytics::pipeline::routing
{

/**
 * @brief Rate limiter for controlling the frequency of object processing.
 *
 * The ObjectRateLimiter maintains a sliding window of timestamps to track
 * the number of objects processed within the last second. It can be used
 * to throttle object processing to prevent overwhelming downstream systems.
 */
class ObjectRateLimiter
{
  private:
    std::deque<std::chrono::steady_clock::time_point> timestamps; ///< Queue of object timestamps
    uint64_t max_objects_per_second;                              ///< Maximum allowed objects per second (0 = no limit)
    std::mutex mtx;                                               ///< Mutex for thread-safe access
    bool started;                                                 ///< Whether the limiter has been started

    /**
     * @brief Removes timestamps older than 1 second from the sliding window.
     * @param now The current time point
     */
    void clean_old_timestamps(const std::chrono::steady_clock::time_point &now);

  public:
    /**
     * @brief Constructs an ObjectRateLimiter.
     * @param max_objects_per_second Maximum objects allowed per second (0 = no limit)
     */
    ObjectRateLimiter(uint64_t max_objects_per_second);

    /**
     * @brief Attempts to add an object to the rate limiter.
     * @return 0 if object added successfully or no limit set, otherwise milliseconds to wait if limit exceeded
     */
    uint64_t add_object();

    /**
     * @brief Gets the current count of objects in the sliding window.
     * @return Current number of objects processed in the last second
     */
    uint64_t get_current_count();

    /**
     * @brief Resets the limiter by clearing all timestamps.
     */
    void reset();
};

/**
 * @brief Stage that controls traffic of tracked objects based on classification and rate limiting.
 *
 * The TrackerTrafficCtrlStage manages the flow of detected and tracked objects through the pipeline
 * by applying various filtering and rate-limiting strategies. It can block untracked objects,
 * throttle the frequency of tracked objects (both classified and unclassified), and enforce
 * a maximum objects-per-second limit.
 *
 * Key features:
 * - Block all untracked objects (optional)
 * - Control frame pass-through rate for unclassified tracked objects
 * - Control frame pass-through rate for classified tracked objects
 * - Enforce maximum objects per second rate limit
 * - Automatic cleanup of stale tracking data
 */
class TrackerTrafficCtrlStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    bool m_initialized = false;                       ///< Initialization state
    bool m_block_all_untracked;                       ///< If true, blocks all untracked objects
    size_t m_tracked_unclassified_frame_block_cnt;    ///< Number of frames to block for unclassified tracked objects
    size_t m_tracked_classified_frame_block_cnt;      ///< Number of frames to block for classified tracked objects
    std::unordered_map<int, size_t> m_trackingcounts; ///< Tracking ID to frame count map
    ObjectRateLimiter m_rate_limiter;                 ///< Rate limiter for objects per second

    /**
     * @brief Extracts the tracking ID from a detection object.
     * @param detection The detection object
     * @return Tracking ID or -1 if not tracked
     */
    int get_tracking_id(HailoDetectionPtr detection);

    /**
     * @brief Checks if a detection is classified.
     * @param detection The detection object
     * @return True if the detection has a valid classification
     */
    bool is_classified(HailoDetectionPtr detection);

  public:
    /**
     * @brief Constructs a TrackerTrafficCtrlStage.
     *
     * @param name The name of the stage
     * @param block_all_untracked If true, blocks all untracked objects (default: false)
     * @param tracked_unclassified_frame_block_cnt Number of frames to block before passing unclassified tracked objects
     * (default: 1)
     * @param tracked_classified_frame_block_cnt Number of frames to block before passing classified tracked objects
     * (default: 1)
     * @param tracked_max_objects_per_second Maximum objects per second allowed (0 = no limit, default: 0)
     * @param queue_size The size of the internal processing queue (default: 5)
     * @param leaky If true, drops oldest buffers when queue is full (default: false)
     * @param trace_processing_operations Enable tracing for performance analysis (default: true)
     */
    TrackerTrafficCtrlStage(
        std::string name, bool block_all_untracked = false,
        size_t tracked_unclassified_frame_block_cnt = TRACKER_UNCLASSIFIED_FPS_BLOCK_COUNT_BEFORE_PASS,
        size_t tracked_classified_frame_block_cnt = TRACKER_CLASSIFIED_FPS_BLOCK_COUNT_BEFORE_PASS,
        size_t tracked_max_objects_per_second = 0, size_t queue_size = TRACKER_TRAFFIC_QUEUE_SIZE_DEFAULT,
        bool leaky = false, bool trace_processing_operations = true);

    /**
     * @brief Initializes the stage.
     * @return AppStatus indicating success or failure
     */
    AppStatus init() override;

    /**
     * @brief Deinitializes the stage and cleans up resources.
     * @return AppStatus indicating success or failure
     */
    AppStatus deinit() override;

    /**
     * @brief Processes a buffer by filtering tracked objects based on configured rules.
     * @param data The buffer to process
     * @return AppStatus indicating success or failure
     */
    AppStatus process(BufferPtr data);

    /**
     * @brief Sets the number of frames to block for unclassified tracked objects.
     * @param count Number of frames to block
     * @return AppStatus indicating success or failure
     */
    AppStatus set_unclassified_fps_to_block(size_t count);

    /**
     * @brief Gets the current number of frames being blocked for unclassified objects.
     * @return Number of frames being blocked
     */
    size_t get_unclassified_fps_to_block();

  private:
    std::unordered_map<int, size_t> m_trackingcounts_monitor; ///< Monitoring copy of tracking counts

    /**
     * @brief Cleans up stale tracking count entries.
     */
    void clean_up_tracking_counts();
};

/**
 * @brief Builder class for constructing TrackerTrafficCtrlStage instances.
 *
 * Provides a fluent interface for configuring and building TrackerTrafficCtrlStage objects
 * with various traffic control options.
 */
class TrackerTrafficCtrlStageBuild : public TrackerTrafficCtrlStage
{
  public:
    /**
     * @brief Builder for TrackerTrafficCtrlStage configuration.
     *
     * Allows step-by-step configuration of a TrackerTrafficCtrlStage before building.
     */
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = TRACKER_TRAFFIC_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_block_all_untracked = false;
        size_t m_tracked_unclassified_frame_block_cnt = TRACKER_UNCLASSIFIED_FPS_BLOCK_COUNT_BEFORE_PASS;
        size_t m_tracked_classified_frame_block_cnt = TRACKER_CLASSIFIED_FPS_BLOCK_COUNT_BEFORE_PASS;
        size_t m_tracked_max_objects_per_second = 0;
        bool m_trace = true;

      public:
        /**
         * @brief Sets the stage name (required).
         * @param name The name for the tracker traffic control stage
         * @return Reference to this builder for method chaining
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Sets the internal queue size.
         * @param size The size of the processing queue (default: 5)
         * @return Reference to this builder for method chaining
         */
        Builder &set_queue_size_opt(size_t size);

        /**
         * @brief Configures the leaky queue behavior.
         * @param activate If true, drops oldest buffers when queue is full (default: false)
         * @return Reference to this builder for method chaining
         */
        Builder &set_leaky_opt(bool activate);

        /**
         * @brief Configures whether to block all untracked objects.
         * @param activate If true, blocks all untracked objects (default: false)
         * @return Reference to this builder for method chaining
         */
        Builder &set_block_untracked_obj(bool activate);

        /**
         * @brief Sets the number of frames to block for classified tracked objects.
         * @param count Number of frames to block (default: 1)
         * @return Reference to this builder for method chaining
         */
        Builder &set_classified_fps_to_block(size_t count);

        /**
         * @brief Sets the maximum objects per second rate limit.
         * @param max_objects Maximum objects per second (0 = no limit, default: 0)
         * @return Reference to this builder for method chaining
         */
        Builder &set_tracked_max_objects_per_second(size_t max_objects);

        /**
         * @brief Sets the number of frames to block for unclassified tracked objects.
         * @param count Number of frames to block (default: 1)
         * @return Reference to this builder for method chaining
         */
        Builder &set_unclassified_fps_to_block(size_t count);

        /**
         * @brief Enables or disables tracing for performance analysis.
         * @param activate If true, enables tracing (default: true)
         * @return Reference to this builder for method chaining
         */
        Builder &set_trace_processing_operations(bool activate);

        /**
         * @brief Builds and returns a shared pointer to the configured TrackerTrafficCtrlStage.
         * @return Shared pointer to the newly created TrackerTrafficCtrlStage
         * @throws std::invalid_argument if required parameters are not set
         */
        std::shared_ptr<TrackerTrafficCtrlStage> buildptr() const;
    };

    /**
     * @brief Creates a new Builder instance for constructing a TrackerTrafficCtrlStage.
     * @return A new Builder instance
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::routing
