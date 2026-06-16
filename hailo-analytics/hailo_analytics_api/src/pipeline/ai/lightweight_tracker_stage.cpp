#include <stddef.h>
#include <hailo_postprocess_tools/objects/hailo_common.hpp>
#include <hailo_postprocess_tools/objects/hailo_objects.hpp>
#include <hailo_postprocess_tools/tracking/hailo_lightweight_tracker.hpp>
#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/ai/lightweight_tracker_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::ai
{

LightweightTrackerStage::LightweightTrackerStage(std::string name, std::map<int, TrackerParams> tracker_params,
                                                 size_t queue_size, bool leaky, std::vector<int> classification_ids,
                                                 bool block_non_tracked_class, bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
      m_classification_ids(classification_ids), m_block_non_tracked_class_id(block_non_tracked_class)
{
    for (int class_id : m_classification_ids)
    {
        m_trackers[class_id] = std::make_unique<HailoLightweightTracker>(tracker_params[class_id]);
    }
    HAILO_ANALYTICS_LOG_INFO(
        "LightweightTrackerStage created with name: {}, queue size: {}, leaky: {}, classification_ids count: {}", name,
        queue_size, leaky, classification_ids.size());
}

AppStatus LightweightTrackerStage::process(BufferPtr data)
{
    HailoROIPtr hailo_roi = data->get_roi();

    std::map<int, std::vector<HailoDetectionPtr>> detections;
    for (auto obj : hailo_roi->get_objects_typed(HAILO_DETECTION))
    {
        HailoDetectionPtr detection = std::dynamic_pointer_cast<HailoDetection>(obj);
        if (m_classification_ids.empty() || std::find(m_classification_ids.begin(), m_classification_ids.end(),
                                                      detection->get_class_id()) == m_classification_ids.end())
        {
            if (m_block_non_tracked_class_id)
                hailo_roi->remove_object(detection);

            continue;
        }

        detections[detection->get_class_id()].push_back(detection);
        hailo_roi->remove_object(detection);
    }

    // Swap the detections in the roi with just the online tracked detections
    for (int class_id : m_classification_ids)
    {
        std::vector<HailoDetectionPtr> online_detection_ptrs = m_trackers[class_id]->update(detections[class_id]);
        hailo_common::add_detection_pointers(hailo_roi, online_detection_ptrs);
    }
    send_to_subscribers(data);

    return AppStatus::SUCCESS;
}

LightweightTrackerStageBuild::Builder &LightweightTrackerStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}
LightweightTrackerStageBuild::Builder &LightweightTrackerStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}
LightweightTrackerStageBuild::Builder &LightweightTrackerStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}
LightweightTrackerStageBuild::Builder &LightweightTrackerStageBuild::Builder::set_classification_ids(
    const std::vector<int> &ids)
{
    m_classification_ids = ids;
    for (int class_id : m_classification_ids)
    {
        if (m_tracker_params.find(class_id) == m_tracker_params.end())
        {
            m_tracker_params[class_id] = TrackerParams();
        }
    }
    return *this;
}
LightweightTrackerStageBuild::Builder &LightweightTrackerStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

LightweightTrackerStageBuild::Builder &LightweightTrackerStageBuild::Builder::set_history_size(size_t history_size,
                                                                                               int class_id)
{
    if (class_id == -1)
    {
        for (auto &params : m_tracker_params)
        {
            params.second.history_size = history_size;
        }
    }
    else
    {
        m_tracker_params[class_id].history_size = history_size;
    }
    return *this;
}
LightweightTrackerStageBuild::Builder &LightweightTrackerStageBuild::Builder::set_smooth_alpha(float smooth_alpha,
                                                                                               int class_id)
{
    if (class_id == -1)
    {
        for (auto &params : m_tracker_params)
        {
            params.second.smooth_alpha = smooth_alpha;
        }
    }
    else if (m_tracker_params.find(class_id) == m_tracker_params.end())
    {
        m_tracker_params[class_id] = TrackerParams();
    }
    return *this;
}
LightweightTrackerStageBuild::Builder &LightweightTrackerStageBuild::Builder::set_iou_threshold(float iou_threshold,
                                                                                                int class_id)
{
    if (class_id == -1)
    {
        for (auto &params : m_tracker_params)
        {
            params.second.iou_threshold = iou_threshold;
        }
    }
    else
    {
        m_tracker_params[class_id].iou_threshold = iou_threshold;
    }
    return *this;
}
LightweightTrackerStageBuild::Builder &LightweightTrackerStageBuild::Builder::set_weighted_average_decay(
    float weighted_average_decay, int class_id)
{
    if (class_id == -1)
    {
        for (auto &params : m_tracker_params)
        {
            params.second.weighted_average_decay = weighted_average_decay;
        }
    }
    else
    {
        m_tracker_params[class_id].weighted_average_decay = weighted_average_decay;
    }
    return *this;
}
LightweightTrackerStageBuild::Builder &LightweightTrackerStageBuild::Builder::set_grid_size(size_t grid_size,
                                                                                            int class_id)
{
    if (class_id == -1)
    {
        for (auto &params : m_tracker_params)
        {
            params.second.grid_size = grid_size;
        }
    }
    else
    {
        m_tracker_params[class_id].grid_size = grid_size;
    }
    return *this;
}
LightweightTrackerStageBuild::Builder &LightweightTrackerStageBuild::Builder::set_grace_period(int grace_period,
                                                                                               int class_id)
{
    if (class_id == -1)
    {
        for (auto &params : m_tracker_params)
        {
            params.second.grace_period = grace_period;
        }
    }
    else
    {
        m_tracker_params[class_id].grace_period = grace_period;
    }
    return *this;
}
LightweightTrackerStageBuild::Builder &LightweightTrackerStageBuild::Builder::set_add_tracking_id(bool add_tracking_id,
                                                                                                  int class_id)
{
    if (class_id == -1)
    {
        for (auto &params : m_tracker_params)
        {
            params.second.add_tracking_id = add_tracking_id;
        }
    }
    else
    {
        m_tracker_params[class_id].add_tracking_id = add_tracking_id;
    }
    return *this;
}
LightweightTrackerStageBuild::Builder &LightweightTrackerStageBuild::Builder::set_copy_nested_objects(
    bool copy_nested_objects, int class_id)
{
    if (class_id == -1)
    {
        for (auto &params : m_tracker_params)
        {
            params.second.copy_nested_objects = copy_nested_objects;
        }
    }
    else
    {
        m_tracker_params[class_id].copy_nested_objects = copy_nested_objects;
    }
    return *this;
}
LightweightTrackerStageBuild::Builder &LightweightTrackerStageBuild::Builder::set_block_non_tracked_classification_id(
    bool block)
{
    m_block_non_tracked_class_id = block;
    return *this;
}

std::shared_ptr<LightweightTrackerStage> LightweightTrackerStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING(!m_classification_ids.empty(), "set_classification_ids");

    return std::make_shared<LightweightTrackerStage>(m_stage_name.value(), m_tracker_params, m_queue_size, m_leaky,
                                                     m_classification_ids, m_block_non_tracked_class_id, m_trace);
}

LightweightTrackerStageBuild::Builder LightweightTrackerStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::ai
