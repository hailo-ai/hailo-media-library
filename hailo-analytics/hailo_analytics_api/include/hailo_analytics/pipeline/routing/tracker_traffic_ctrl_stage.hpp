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

class ObjectRateLimiter
{
  private:
    std::deque<std::chrono::steady_clock::time_point> timestamps;
    uint64_t max_objects_per_second;
    std::mutex mtx;
    bool started;

    // Remove timestamps older than 1 second
    void clean_old_timestamps(const std::chrono::steady_clock::time_point &now);

  public:
    ObjectRateLimiter(uint64_t max_objects_per_second);

    // Returns 0 if object added successfully or no limit set
    // Returns milliseconds to wait if limit exceeded
    uint64_t add_object();

    // Get current count of objects in the sliding window
    uint64_t get_current_count();

    // Reset the limiter
    void reset();
};

class TrackerTrafficCtrlStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    bool m_initialized = false;
    bool m_block_all_untracked;
    size_t m_tracked_unclassified_frame_block_cnt;
    size_t m_tracked_classified_frame_block_cnt;
    std::unordered_map<int, size_t> m_trackingcounts;
    ObjectRateLimiter m_rate_limiter;

    int get_tracking_id(HailoDetectionPtr detection);

    bool is_classified(HailoDetectionPtr detection);

  public:
    TrackerTrafficCtrlStage(
        std::string name, bool block_all_untracked = false,
        size_t tracked_unclassified_frame_block_cnt = TRACKER_UNCLASSIFIED_FPS_BLOCK_COUNT_BEFORE_PASS,
        size_t tracked_classified_frame_block_cnt = TRACKER_CLASSIFIED_FPS_BLOCK_COUNT_BEFORE_PASS,
        size_t tracked_max_objects_per_second = 0, size_t queue_size = TRACKER_TRAFFIC_QUEUE_SIZE_DEFAULT,
        bool leaky = false, bool trace_processing_operations = true);

    AppStatus init() override;

    AppStatus deinit() override;

    AppStatus process(BufferPtr data);

    AppStatus set_unclassified_fps_to_block(size_t count);

    size_t get_unclassified_fps_to_block();

  private:
    std::unordered_map<int, size_t> m_trackingcounts_monitor;

    void clean_up_tracking_counts();
};

class TrackerTrafficCtrlStageBuild : public TrackerTrafficCtrlStage
{
  public:
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
        Builder &set_stage_name(std::string name);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_block_untracked_obj(bool activate);
        Builder &set_classified_fps_to_block(size_t count);
        Builder &set_tracked_max_objects_per_second(size_t max_objects);
        Builder &set_unclassified_fps_to_block(size_t count);
        Builder &set_trace_processing_operations(bool activate);

        std::shared_ptr<TrackerTrafficCtrlStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::routing
