#include <stddef.h>
#include <stdint.h>
#include <hailo_postprocess_tools/objects/hailo_objects.hpp>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hailo_analytics/pipeline/routing/tracker_traffic_ctrl_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::routing
{

// ObjectRateLimiter Implementation

/**
 * @brief Removes timestamps older than 1 second from the sliding window.
 */
void ObjectRateLimiter::clean_old_timestamps(const std::chrono::steady_clock::time_point &now)
{
    while (!timestamps.empty())
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - timestamps.front()).count();
        if (elapsed >= 1000)
        {
            timestamps.pop_front();
        }
        else
        {
            break;
        }
    }
}

/**
 * @brief Constructs an ObjectRateLimiter with the specified maximum objects per second.
 */
ObjectRateLimiter::ObjectRateLimiter(uint64_t max_objects_per_second)
    : max_objects_per_second(max_objects_per_second), started(false)
{
}

/**
 * @brief Attempts to add an object, returning wait time if rate limit exceeded.
 */
uint64_t ObjectRateLimiter::add_object()
{
    std::lock_guard<std::mutex> lock(mtx);

    if (max_objects_per_second == 0)
    {
        // No limit
        return 0;
    }

    auto now = std::chrono::steady_clock::now();

    // Clean up timestamps older than 1 second
    clean_old_timestamps(now);

    // Check if we've reached the limit
    if (timestamps.size() >= max_objects_per_second)
    {
        // Calculate how long until the oldest timestamp expires
        auto oldestTime = timestamps.front();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - oldestTime).count();
        uint64_t timeToWait = 1000 - elapsed;
        return timeToWait;
    }

    // Add this object's timestamp
    timestamps.push_back(now);
    started = true;
    return 0;
}

/**
 * @brief Gets the current count of objects in the sliding window.
 */
uint64_t ObjectRateLimiter::get_current_count()
{
    std::lock_guard<std::mutex> lock(mtx);
    if (!started)
        return 0;

    auto now = std::chrono::steady_clock::now();
    clean_old_timestamps(now);
    return timestamps.size();
}

/**
 * @brief Resets the limiter by clearing all timestamps.
 */
void ObjectRateLimiter::reset()
{
    std::lock_guard<std::mutex> lock(mtx);
    timestamps.clear();
    started = false;
}

// TrackerTrafficCtrlStage Implementation
int TrackerTrafficCtrlStage::get_tracking_id(HailoDetectionPtr detection)
{
    for (auto obj : detection->get_objects_typed(HAILO_UNIQUE_ID))
    {
        HailoUniqueIDPtr id = std::dynamic_pointer_cast<HailoUniqueID>(obj);
        if (id->get_mode() == TRACKING_ID)
        {
            return id->get_id();
        }
    }
    return 0;
}

bool TrackerTrafficCtrlStage::is_classified(HailoDetectionPtr detection)
{
    bool classified = false;
    for (auto classification_obj : detection->get_objects_typed(HAILO_CLASSIFICATION))
    {
        HailoClassificationPtr classification = std::dynamic_pointer_cast<HailoClassification>(classification_obj);
        if (classification->get_type() == HAILO_CLASSIFICATION)
        {
            classified = true;
            // std::cout << "Classification Label: " << classification->get_label() << std::endl;
        }
    }

    return classified;
}

TrackerTrafficCtrlStage::TrackerTrafficCtrlStage(std::string name, bool block_all_untracked,
                                                 size_t tracked_unclassified_frame_block_cnt,
                                                 size_t tracked_classified_frame_block_cnt,
                                                 size_t tracked_max_objects_per_second, size_t queue_size, bool leaky,
                                                 bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
      m_block_all_untracked(block_all_untracked),
      m_tracked_unclassified_frame_block_cnt(tracked_unclassified_frame_block_cnt),
      m_tracked_classified_frame_block_cnt(tracked_classified_frame_block_cnt),
      m_rate_limiter(tracked_max_objects_per_second)
{
}

AppStatus TrackerTrafficCtrlStage::init()
{
    m_initialized = true;
    m_trackingcounts.clear();
    return AppStatus::SUCCESS;
}

AppStatus TrackerTrafficCtrlStage::deinit()
{
    m_initialized = false;
    return AppStatus::SUCCESS;
}

AppStatus TrackerTrafficCtrlStage::process(BufferPtr data)
{
    // Clean up m_trackingcounts once a while
    clean_up_tracking_counts();

    HailoROIPtr hailo_roi = data->get_roi();

    std::vector<HailoObjectPtr> detections_to_remove;
    uint64_t ms_wait = 0;
    for (auto obj : hailo_roi->get_objects_typed(HAILO_DETECTION))
    {
        bool remove_detection = false;
        HailoDetectionPtr detection = std::dynamic_pointer_cast<HailoDetection>(obj);
        int track_id = get_tracking_id(detection);
        if (track_id)
        {
            size_t tracked_count_fps_block_before_pass = 1;
            if (is_classified(detection))
                tracked_count_fps_block_before_pass = m_tracked_classified_frame_block_cnt;
            else
                tracked_count_fps_block_before_pass = m_tracked_unclassified_frame_block_cnt;

            // When classified/unclassified tracked object still does not exceed more than
            // tracked_count_fps_block_before_pass times, we will NOT let it pass to next subscribed.
            if (m_trackingcounts[track_id]++ < tracked_count_fps_block_before_pass)
            {
                remove_detection = true;
            }
            else if ((ms_wait = m_rate_limiter.add_object()) > 0)
            {
                // If we have reached the max objects per second limit, we remove it and keep the tracking
                // count as is so that when the object limiter is allowed again it has better chance to pass through
                remove_detection = true;
            }
            else
            {
                // The classified tracked object appears more than tracked_count_fps_block_before_pass we let it
                // pass to subscribed and we reset the counter
                m_trackingcounts.erase(track_id);
            }
        }
        else if (m_block_all_untracked)
        {
            // If we block all untracked objects, we will remove it
            remove_detection = true;
        }

        // We record for the detection object that we don't want to let it pass to next subscriber
        if (remove_detection)
        {
            detections_to_remove.push_back(obj);
        }
    }

    for (auto obj_to_remove : detections_to_remove)
    {
        hailo_roi->remove_object(obj_to_remove);
    }

    send_to_subscribers(data);

    return AppStatus::SUCCESS;
}

AppStatus TrackerTrafficCtrlStage::set_unclassified_fps_to_block(size_t count)
{
    if (!m_initialized)
    {
        std::cerr << "TrackerTrafficCtrlStage not initialized" << std::endl;
        return AppStatus::UNINITIALIZED;
    }

    m_tracked_unclassified_frame_block_cnt = count;
    return AppStatus::SUCCESS;
}

size_t TrackerTrafficCtrlStage::get_unclassified_fps_to_block()
{
    return m_tracked_unclassified_frame_block_cnt;
}

void TrackerTrafficCtrlStage::clean_up_tracking_counts()
{
    static auto executed_time = std::chrono::steady_clock::now();
    auto current_time = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(current_time - executed_time).count() >= CLEAN_UP_INTERVAL_SEC)
    {
        // After clean up time (default to each 60s), we remove the tracking count that
        // has not been updated (meaning the tracking id is not used anymore)
        for (const auto &[key, monitorValue] : m_trackingcounts_monitor)
        {
            auto it = m_trackingcounts.find(key);
            if (it != m_trackingcounts.end() && it->second == monitorValue)
            {
                m_trackingcounts.erase(it); // remove matching key
            }
        }

        m_trackingcounts_monitor = m_trackingcounts; // Update the monitor map
        executed_time = current_time;
    }
}

// TrackerTrafficCtrlStageBuild::Builder Implementation
TrackerTrafficCtrlStageBuild::Builder &TrackerTrafficCtrlStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

TrackerTrafficCtrlStageBuild::Builder &TrackerTrafficCtrlStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}

TrackerTrafficCtrlStageBuild::Builder &TrackerTrafficCtrlStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

TrackerTrafficCtrlStageBuild::Builder &TrackerTrafficCtrlStageBuild::Builder::set_block_untracked_obj(bool activate)
{
    m_block_all_untracked = activate;
    return *this;
}

TrackerTrafficCtrlStageBuild::Builder &TrackerTrafficCtrlStageBuild::Builder::set_classified_fps_to_block(size_t count)
{
    m_tracked_classified_frame_block_cnt = count;
    return *this;
}

TrackerTrafficCtrlStageBuild::Builder &TrackerTrafficCtrlStageBuild::Builder::set_tracked_max_objects_per_second(
    size_t max_objects)
{
    m_tracked_max_objects_per_second = max_objects;
    return *this;
}

TrackerTrafficCtrlStageBuild::Builder &TrackerTrafficCtrlStageBuild::Builder::set_unclassified_fps_to_block(
    size_t count)
{
    m_tracked_unclassified_frame_block_cnt = count;
    return *this;
}

TrackerTrafficCtrlStageBuild::Builder &TrackerTrafficCtrlStageBuild::Builder::set_trace_processing_operations(
    bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<TrackerTrafficCtrlStage> TrackerTrafficCtrlStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<TrackerTrafficCtrlStage>(
        m_stage_name.value(), m_block_all_untracked, m_tracked_unclassified_frame_block_cnt,
        m_tracked_classified_frame_block_cnt, m_tracked_max_objects_per_second, m_queue_size, m_leaky, m_trace);
}

TrackerTrafficCtrlStageBuild::Builder TrackerTrafficCtrlStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::routing
