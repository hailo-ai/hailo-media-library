#include <hailopp/hailotracker.h>
#include <hailopp/status.h>
#include <hailort.h>
#include <stddef.h>
#include <stdint.h>
#include <hailo_postprocess_tools/objects/hailo_common.hpp>
#include <hailo_postprocess_tools/objects/hailo_objects.hpp>
#include <media_library/cloexec_fstream.hpp>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/ai/detection_tracker_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::ai
{

DetectionTrackerStage::DetectionTrackerStage(std::string name, hailo_tracker_config_t config,
                                             std::map<uint8_t, std::string> labels_map, size_t queue_size, bool leaky,
                                             bool trace_processing_operations, std::string mot_output_path)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
      m_tracker_handle(nullptr), m_tracker_config(config), m_labels_map(std::move(labels_map)),
      m_save_mot_output(!mot_output_path.empty()), m_mot_output_path(std::move(mot_output_path)), m_frame_counter(0)
{
    HAILO_ANALYTICS_LOG_INFO("DetectionTrackerStage created with name: {}, queue size: {}, leaky: {}", name, queue_size,
                             leaky);
}

DetectionTrackerStage::~DetectionTrackerStage()
{
    deinit();
}

AppStatus DetectionTrackerStage::init()
{
    hailopp_status status = hailo_tracker_create(&m_tracker_config, &m_tracker_handle);
    if (HAILOPP_SUCCESS != status)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create tracker, status: {}", static_cast<int>(status));
        return AppStatus::HAILORT_ERROR;
    }

    if (m_save_mot_output)
    {
        m_mot_file.open(m_mot_output_path, std::ios::out | std::ios::trunc);
        if (!m_mot_file.is_open())
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to open MOT output file: {}", m_mot_output_path);
            return AppStatus::CONFIGURATION_ERROR;
        }
        m_frame_counter = 0;
        HAILO_ANALYTICS_LOG_WARN("MOT Challenge debug output enabled (writes every frame to {})", m_mot_output_path);
    }

    HAILO_ANALYTICS_LOG_INFO("DetectionTrackerStage initialized tracker handle successfully");
    return AppStatus::SUCCESS;
}

AppStatus DetectionTrackerStage::deinit()
{
    if (m_mot_file.is_open())
    {
        m_mot_file.close();
    }

    if (m_tracker_handle != nullptr)
    {
        hailopp_status status = hailo_tracker_release(m_tracker_handle);
        m_tracker_handle = nullptr;
        if (HAILOPP_SUCCESS != status)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to release HailoRT tracker, status: {}", static_cast<int>(status));
            return AppStatus::HAILORT_ERROR;
        }
    }
    return AppStatus::SUCCESS;
}

std::vector<uint8_t> DetectionTrackerStage::hailo_detections_to_hailort_detections(
    const std::vector<HailoDetectionPtr> &detections)
{
    size_t count = detections.size();
    size_t alloc_size = sizeof(hailo_detections_t) + count * sizeof(hailo_detection_t);
    std::vector<uint8_t> buffer(alloc_size, 0);
    auto *tracker_detections = reinterpret_cast<hailo_detections_t *>(buffer.data());
    tracker_detections->count = static_cast<uint16_t>(count);

    for (size_t i = 0; i < count; i++)
    {
        HailoBBox bbox = detections[i]->get_bbox();
        tracker_detections->detections[i].x_min = bbox.xmin();
        tracker_detections->detections[i].y_min = bbox.ymin();
        tracker_detections->detections[i].x_max = bbox.xmax();
        tracker_detections->detections[i].y_max = bbox.ymax();
        tracker_detections->detections[i].score = detections[i]->get_confidence();
        tracker_detections->detections[i].class_id = static_cast<uint16_t>(detections[i]->get_class_id());
    }

    return buffer;
}

std::vector<HailoDetectionPtr> DetectionTrackerStage::tracklets_to_hailo_detections(
    const hailo_tracklets_t &tracklets) const
{
    std::vector<HailoDetectionPtr> result;
    for (size_t i = 0; i < tracklets.count; i++)
    {
        const hailo_tracklet_t &tracklet = tracklets.tracklets[i];
        if (tracklet.state != HAILO_TRACKLET_STATE_TRACKED)
        {
            continue;
        }

        // Kalman predictions can drift outside [0,1] for exiting tracks; clamp so downstream
        // consumers don't wrap negatives through size_t casts.
        const float x_min = std::clamp(tracklet.detection.x_min, 0.0f, 1.0f);
        const float y_min = std::clamp(tracklet.detection.y_min, 0.0f, 1.0f);
        const float x_max = std::clamp(tracklet.detection.x_max, 0.0f, 1.0f);
        const float y_max = std::clamp(tracklet.detection.y_max, 0.0f, 1.0f);
        const float width = x_max - x_min;
        const float height = y_max - y_min;
        if (width <= 0.0f || height <= 0.0f)
        {
            continue;
        }
        HailoBBox bbox(x_min, y_min, width, height);

        std::string label;
        auto it = m_labels_map.find(static_cast<uint8_t>(tracklet.detection.class_id));
        if (it != m_labels_map.end())
        {
            label = it->second;
        }

        auto detection = std::make_shared<HailoDetection>(bbox, static_cast<int>(tracklet.detection.class_id), label,
                                                          tracklet.detection.score);
        detection->add_object(std::make_shared<HailoUniqueID>(static_cast<int>(tracklet.id), TRACKING_ID));
        result.push_back(detection);
    }
    return result;
}

void DetectionTrackerStage::write_mot_line(const std::vector<HailoDetectionPtr> &detections)
{
    for (const auto &det : detections)
    {
        HailoBBox bbox = det->get_bbox();
        int tracking_id = -1;
        auto unique_ids = hailo_common::get_hailo_track_id(det);
        if (!unique_ids.empty())
        {
            tracking_id = unique_ids[0]->get_id();
        }

        // MOT format: <frame>,<id>,<bb_left>,<bb_top>,<bb_width>,<bb_height>,<conf>,<class>,<visibility>
        m_mot_file << m_frame_counter << "," << tracking_id << "," << bbox.xmin() << "," << bbox.ymin() << ","
                   << bbox.width() << "," << bbox.height() << "," << det->get_confidence() << "," << det->get_class_id()
                   << ",-1\n";
    }
}

AppStatus DetectionTrackerStage::predict_tracklets(hailo_tracklets_t &tracklets)
{
    hailopp_status status = hailo_tracker_predict(m_tracker_handle, &tracklets);
    if (HAILOPP_SUCCESS != status)
    {
        HAILO_ANALYTICS_LOG_ERROR("hailo_tracker_predict failed, status: {}", static_cast<int>(status));
        return AppStatus::HAILORT_ERROR;
    }
    return AppStatus::SUCCESS;
}

AppStatus DetectionTrackerStage::update_tracker_from_roi_detections(HailoROIPtr hailo_roi)
{
    auto hailo_detections = hailo_common::get_hailo_detections(hailo_roi);
    for (const auto &det : hailo_detections)
    {
        hailo_roi->remove_object(det);
    }

    auto detection_buffer = hailo_detections_to_hailort_detections(hailo_detections);
    auto *tracker_detections = reinterpret_cast<hailo_detections_t *>(detection_buffer.data());

    hailopp_status status = hailo_tracker_update(m_tracker_handle, tracker_detections);
    if (HAILOPP_SUCCESS != status)
    {
        HAILO_ANALYTICS_LOG_ERROR("hailo_tracker_update failed, status: {}", static_cast<int>(status));
        return AppStatus::HAILORT_ERROR;
    }
    return AppStatus::SUCCESS;
}

AppStatus DetectionTrackerStage::process(BufferPtr data)
{
    HailoROIPtr hailo_roi = data->get_roi();

    hailo_tracklets_t tracklets = {};
    AppStatus predict_status = predict_tracklets(tracklets);
    if (AppStatus::SUCCESS != predict_status)
    {
        return predict_status;
    }

    bool ai_processed = !data->get_metadata_of_type(MetadataType::TENSOR).empty();
    if (ai_processed)
    {
        AppStatus update_status = update_tracker_from_roi_detections(hailo_roi);
        if (AppStatus::SUCCESS != update_status)
        {
            return update_status;
        }
    }

    auto online_detection_ptrs = tracklets_to_hailo_detections(tracklets);
    hailo_common::add_detection_pointers(hailo_roi, online_detection_ptrs);

    m_frame_counter++;
    if (m_save_mot_output && m_mot_file.is_open())
    {
        write_mot_line(online_detection_ptrs);
    }

    send_to_subscribers(data);
    return AppStatus::SUCCESS;
}

// --- Builder implementation ---

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = std::move(name);
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_max_tracklets(uint16_t max_tracklets)
{
    m_tracker_config.max_tracklets = max_tracklets;
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_max_missed_frames(
    uint8_t max_missed_frames)
{
    m_tracker_config.max_missed_frames = max_missed_frames;
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_min_confirmed_frames(
    uint8_t min_confirmed_frames)
{
    m_tracker_config.min_confirmed_frames = min_confirmed_frames;
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_aging_threshold(uint8_t aging_threshold)
{
    m_tracker_config.aging_threshold = aging_threshold;
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_add_threshold(float add_threshold)
{
    m_tracker_config.add_threshold = add_threshold;
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_association_threshold(
    float association_threshold)
{
    m_tracker_config.association_threshold = association_threshold;
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_iou_weight(float iou_weight)
{
    m_tracker_config.iou_weight = iou_weight;
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_class_aware_tracking(
    bool class_aware_tracking)
{
    m_tracker_config.class_aware_tracking = class_aware_tracking;
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_enable_kalman_filter(
    bool enable_kalman_filter)
{
    m_tracker_config.enable_kalman_filter = enable_kalman_filter;
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_position_std_weight(
    float position_std_weight)
{
    m_tracker_config.position_std_weight = position_std_weight;
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_velocity_std_weight(
    float velocity_std_weight)
{
    m_tracker_config.velocity_std_weight = velocity_std_weight;
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_smoothing_alpha(float smoothing_alpha)
{
    m_tracker_config.smoothing_alpha = smoothing_alpha;
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_labels_map(
    std::map<uint8_t, std::string> labels_map)
{
    m_labels_map = std::move(labels_map);
    return *this;
}

DetectionTrackerStageBuild::Builder &DetectionTrackerStageBuild::Builder::set_mot_output_path(std::string path)
{
    m_mot_output_path = std::move(path);
    return *this;
}

std::shared_ptr<DetectionTrackerStage> DetectionTrackerStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<DetectionTrackerStage>(m_stage_name.value(), m_tracker_config, m_labels_map, m_queue_size,
                                                   m_leaky, m_trace, m_mot_output_path);
}

DetectionTrackerStageBuild::Builder DetectionTrackerStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::ai
