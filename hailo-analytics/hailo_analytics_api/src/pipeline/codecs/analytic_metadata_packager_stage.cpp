
#include <stddef.h>
#include <stdint.h>
#include <hailo_postprocess_tools/objects/hailo_common.hpp>
#include <hailo_postprocess_tools/objects/hailo_objects.hpp>
#include <media_library/buffer_pool.hpp>
#include <media_library/media_library_buffer.hpp>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/codecs/analytic_metadata_packager_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "analytics_metadata.pb.h"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::codecs
{

void LandmarksCache::update(int tracking_id, HailoLandmarksPtr landmarks)
{
    auto &entry = m_entries[tracking_id];
    entry.landmarks = std::move(landmarks);
    entry.seen_this_frame = true;
}

HailoLandmarksPtr LandmarksCache::lookup(int tracking_id)
{
    auto it = m_entries.find(tracking_id);
    if (it == m_entries.end())
    {
        return nullptr;
    }
    // Marking on lookup keeps tracker-only frames from aging the entry out — the track is still
    // present, just hasn't had a fresh AI update on this frame.
    it->second.seen_this_frame = true;
    return it->second.landmarks;
}

void LandmarksCache::advance_frame()
{
    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        if (!it->second.seen_this_frame)
        {
            it = m_entries.erase(it);
        }
        else
        {
            it->second.seen_this_frame = false;
            ++it;
        }
    }
}

namespace
{

constexpr const char *LANDMARKS_POINTS_FORMAT = "x,y,conf";
constexpr uint32_t LANDMARKS_POINTS_STRIDE = 3;

// Forward declaration; populate_objects_recursive recurses with both Frame and Detection parents.
template <typename ParentMessage>
void populate_objects_recursive(HailoROIPtr roi, const HailoBBox &parent_bbox, uint32_t native_width,
                                uint32_t native_height, ParentMessage &parent, LandmarksCache *cache);

// Both hailo_analytics::Frame and hailo_analytics::Detection expose add_detections() returning
// Detection*, so a single template covers both parents without explicit dispatch.
template <typename ParentMessage>
hailo_analytics::Detection *populate_detection(HailoDetectionPtr detection, const HailoBBox &roi_bbox,
                                               uint32_t native_width, uint32_t native_height, ParentMessage &parent)
{
    auto *detection_msg = parent.add_detections();

    detection_msg->set_label(detection->get_label());
    detection_msg->set_confidence(detection->get_confidence());

    auto *bbox_msg = detection_msg->mutable_bbox();
    auto bbox = detection->get_bbox();
    bbox_msg->set_xmin(((bbox.xmin() * roi_bbox.width()) + roi_bbox.xmin()) * native_width);
    bbox_msg->set_ymin(((bbox.ymin() * roi_bbox.height()) + roi_bbox.ymin()) * native_height);
    bbox_msg->set_xmax(((bbox.xmax() * roi_bbox.width()) + roi_bbox.xmin()) * native_width);
    bbox_msg->set_ymax(((bbox.ymax() * roi_bbox.height()) + roi_bbox.ymin()) * native_height);

    return detection_msg;
}

template <typename ParentMessage>
hailo_analytics::Landmarks *populate_landmarks(HailoLandmarksPtr landmarks, const HailoBBox &roi_bbox,
                                               uint32_t native_width, uint32_t native_height, ParentMessage &parent)
{
    const auto &points_input = landmarks->get_points();
    if (points_input.empty())
        return nullptr;

    auto *landmarks_msg = parent.add_landmarks();
    landmarks_msg->set_points_format(LANDMARKS_POINTS_FORMAT);
    landmarks_msg->set_points_stride(LANDMARKS_POINTS_STRIDE);

    const float scale_x = roi_bbox.width() * static_cast<float>(native_width);
    const float offset_x = roi_bbox.xmin() * static_cast<float>(native_width);
    const float scale_y = roi_bbox.height() * static_cast<float>(native_height);
    const float offset_y = roi_bbox.ymin() * static_cast<float>(native_height);

    auto *points_field = landmarks_msg->mutable_points();
    points_field->Reserve(static_cast<int>(points_input.size() * LANDMARKS_POINTS_STRIDE));
    for (const auto &point : points_input)
    {
        points_field->Add(point.x() * scale_x + offset_x);
        points_field->Add(point.y() * scale_y + offset_y);
        points_field->Add(point.confidence());
    }

    const auto &pairs_input = landmarks->get_pairs();
    auto *pairs_field = landmarks_msg->mutable_pairs();
    pairs_field->Reserve(static_cast<int>(pairs_input.size() * 2));
    for (const auto &pair : pairs_input)
    {
        pairs_field->Add(static_cast<uint32_t>(pair.first));
        pairs_field->Add(static_cast<uint32_t>(pair.second));
    }

    return landmarks_msg;
}

template <typename ParentMessage>
void populate_classification(HailoClassificationPtr classification, ParentMessage &parent)
{
    auto *classification_msg = parent.add_classifications();
    classification_msg->set_type(classification->get_classification_type());
    classification_msg->set_label(classification->get_label());
    classification_msg->set_confidence(classification->get_confidence());
}

void set_tracking_id(uint32_t /*tracking_id*/, hailo_analytics::Frame & /*parent*/)
{
    // Top-level Frame has no tracking_id; HailoUniqueID at the root is silently dropped (matches legacy JSON
    // behaviour).
}

void set_tracking_id(uint32_t tracking_id, hailo_analytics::Detection &parent)
{
    parent.set_tracking_id(tracking_id);
}

// Locate this detection's own landmarks (if any) and tracking id (if any) without recursing — the
// outer walker needs both before deciding whether to refresh / fall back to the cache.
struct DetectionContext
{
    HailoLandmarksPtr own_landmarks;
    int tracking_id = -1; // -1 = no HailoUniqueID found
};

DetectionContext scan_detection_metadata(HailoROIPtr child_roi)
{
    DetectionContext ctx;
    for (auto obj : child_roi->get_objects())
    {
        switch (obj->get_type())
        {
        case HAILO_LANDMARKS:
            if (!ctx.own_landmarks)
            {
                ctx.own_landmarks = std::dynamic_pointer_cast<HailoLandmarks>(obj);
            }
            break;
        case HAILO_UNIQUE_ID:
            if (auto uid = std::dynamic_pointer_cast<HailoUniqueID>(obj))
            {
                ctx.tracking_id = uid->get_id();
            }
            break;
        default:
            break;
        }
    }
    return ctx;
}

template <typename ParentMessage>
void populate_objects_recursive(HailoROIPtr roi, const HailoBBox &parent_bbox, uint32_t native_width,
                                uint32_t native_height, ParentMessage &parent, LandmarksCache *cache)
{
    for (auto obj : roi->get_objects())
    {
        switch (obj->get_type())
        {
        case HAILO_DETECTION: {
            auto detection = std::dynamic_pointer_cast<HailoDetection>(obj);
            auto *detection_msg = populate_detection(detection, parent_bbox, native_width, native_height, parent);

            auto child_roi = std::dynamic_pointer_cast<HailoROI>(obj);
            if (child_roi)
            {
                auto child_bbox = hailo_common::create_flattened_bbox(parent_bbox, detection->get_bbox());

                // Cache update / fallback: refresh on fresh landmarks, fall back to the cached
                // entry when this frame has no landmarks of its own (tracker-only frame).
                HailoLandmarksPtr cached_fallback;
                if (cache)
                {
                    auto ctx = scan_detection_metadata(child_roi);
                    if (ctx.tracking_id >= 0)
                    {
                        if (ctx.own_landmarks)
                        {
                            cache->update(ctx.tracking_id, ctx.own_landmarks);
                        }
                        else
                        {
                            cached_fallback = cache->lookup(ctx.tracking_id);
                        }
                    }
                }

                populate_objects_recursive(child_roi, child_bbox, native_width, native_height, *detection_msg, cache);

                if (cached_fallback)
                {
                    populate_landmarks(cached_fallback, child_bbox, native_width, native_height, *detection_msg);
                }
            }
            break;
        }
        case HAILO_LANDMARKS: {
            auto landmarks = std::dynamic_pointer_cast<HailoLandmarks>(obj);
            auto *landmarks_msg = populate_landmarks(landmarks, parent_bbox, native_width, native_height, parent);

            auto child_roi = std::dynamic_pointer_cast<HailoROI>(obj);
            if (landmarks_msg != nullptr && child_roi)
            {
                // Child ROI inherits parent_bbox (matches legacy code's object_bbox for landmarks).
                // Landmarks message itself has no nested children fields, so we only recurse if we
                // somehow share scope with sibling detections. Skipping to mirror legacy semantics.
            }
            break;
        }
        case HAILO_UNIQUE_ID: {
            auto unique_id = std::dynamic_pointer_cast<HailoUniqueID>(obj);
            set_tracking_id(static_cast<uint32_t>(unique_id->get_id()), parent);
            break;
        }
        case HAILO_CLASSIFICATION: {
            auto classification = std::dynamic_pointer_cast<HailoClassification>(obj);
            populate_classification(classification, parent);
            break;
        }
        default:
            HAILO_ANALYTICS_LOG_INFO("analytic_metadata_proto: skipping unknown object type {}", obj->get_type());
            break;
        }
    }
}

} // namespace

bool build_metadata_proto(BufferPtr data, hailo_analytics::Frame &frame)
{
    auto roi = data->get_roi();
    if (!roi)
        return false;

    auto native_width = data->get_buffer()->buffer_data->width;
    auto native_height = data->get_buffer()->buffer_data->height;

    populate_objects_recursive(roi, roi->get_bbox(), native_width, native_height, frame, /*cache=*/nullptr);

    if (frame.detections_size() == 0 && frame.landmarks_size() == 0 && frame.classifications_size() == 0)
        return false;

    frame.set_isp_timestamp_ns(data->get_buffer()->isp_timestamp_ns);
    frame.set_frame_width(native_width);
    frame.set_frame_height(native_height);
    return true;
}

bool build_metadata_proto(BufferPtr data, hailo_analytics::Frame &frame, LandmarksCache &cache)
{
    auto roi = data->get_roi();
    if (!roi)
        return false;

    auto native_width = data->get_buffer()->buffer_data->width;
    auto native_height = data->get_buffer()->buffer_data->height;

    populate_objects_recursive(roi, roi->get_bbox(), native_width, native_height, frame, &cache);
    cache.advance_frame();

    if (frame.detections_size() == 0 && frame.landmarks_size() == 0 && frame.classifications_size() == 0)
        return false;

    frame.set_isp_timestamp_ns(data->get_buffer()->isp_timestamp_ns);
    frame.set_frame_width(native_width);
    frame.set_frame_height(native_height);
    return true;
}

AnalyticMetadataPackagerStage::AnalyticMetadataPackagerStage(std::string name, size_t queue_size, bool leaky,
                                                             bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations)
{
}

AppStatus AnalyticMetadataPackagerStage::process(BufferPtr data)
{
    hailo_analytics::Frame frame;
    if (!build_metadata_proto(data, frame, m_landmarks_cache))
        return AppStatus::SUCCESS;

    std::string serialized;
    if (!frame.SerializeToString(&serialized))
    {
        HAILO_ANALYTICS_LOG_WARN("analytic_metadata_proto: SerializeToString failed");
        return AppStatus::SUCCESS;
    }

    auto zmq_msg = std::make_shared<HailoZMQMessage>();
    zmq_msg->set_output_msg(std::move(serialized));
    data->get_roi()->add_object(zmq_msg);

    send_to_subscribers(data);

    return AppStatus::SUCCESS;
}

AnalyticMetadataPackagerStageBuild::Builder &AnalyticMetadataPackagerStageBuild::Builder::set_stage_name(
    std::string name)
{
    m_stage_name = name;
    return *this;
}

AnalyticMetadataPackagerStageBuild::Builder &AnalyticMetadataPackagerStageBuild::Builder::set_queue_size_opt(
    size_t size)
{
    m_queue_size = size;
    return *this;
}

AnalyticMetadataPackagerStageBuild::Builder &AnalyticMetadataPackagerStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

AnalyticMetadataPackagerStageBuild::Builder &AnalyticMetadataPackagerStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<AnalyticMetadataPackagerStage> AnalyticMetadataPackagerStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<AnalyticMetadataPackagerStage>(m_stage_name.value(), m_queue_size, m_leaky, m_trace);
}

AnalyticMetadataPackagerStageBuild::Builder AnalyticMetadataPackagerStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::codecs
