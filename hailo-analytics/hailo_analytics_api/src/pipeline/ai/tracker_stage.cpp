#include "hailo_analytics/pipeline/ai/tracker_stage.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

namespace hailo_analytics::pipeline::ai
{

TrackerStage::TrackerStage(std::string name, size_t queue_size, bool leaky, int classification_id,
                           bool block_non_tracked_class_id, bool trace_processing_operations, bool print_fps)
    : ThreadedStage(name, queue_size, leaky, trace_processing_operations), m_class_id(classification_id),
      m_block_non_tracked_class_id(block_non_tracked_class_id), m_print_fps(print_fps)
{
}

AppStatus TrackerStage::init()
{
    m_tracker_params.kalman_distance = DEFAULT_KALMAN_DISTANCE;
    m_tracker_params.iou_threshold = DEFAULT_IOU_THRESHOLD;
    m_tracker_params.init_iou_threshold = DEFAULT_INIT_IOU_THRESHOLD;
    m_tracker_params.keep_tracked_frames = DEFAULT_KEEP_FRAMES;
    m_tracker_params.keep_new_frames = DEFAULT_KEEP_FRAMES;
    m_tracker_params.keep_lost_frames = DEFAULT_KEEP_FRAMES;
    m_tracker_params.keep_past_metadata = DEFAULT_KEEP_PAST_METADATA;
    m_tracker_params.std_weight_position = DEFAULT_STD_WEIGHT_POSITION;
    m_tracker_params.std_weight_position_box = DEFAULT_STD_WEIGHT_POSITION_BOX;
    m_tracker_params.std_weight_velocity = DEFAULT_STD_WEIGHT_VELOCITY;
    m_tracker_params.std_weight_velocity_box = DEFAULT_STD_WEIGHT_VELOCITY_BOX;
    m_tracker_params.debug = DEFAULT_DEBUG;
    m_tracker_params.hailo_objects_blacklist = DEFAULT_HAILO_OBJECTS_BLACKLIST;
    return AppStatus::SUCCESS;
}

AppStatus TrackerStage::deinit()
{
    return AppStatus::SUCCESS;
}

AppStatus TrackerStage::process(BufferPtr data)
{
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    HailoROIPtr hailo_roi = data->get_roi();

    std::vector<HailoDetectionPtr> detections;
    for (auto obj : hailo_roi->get_objects_typed(HAILO_DETECTION))
    {
        HailoDetectionPtr detection = std::dynamic_pointer_cast<HailoDetection>(obj);

        if ((m_class_id == -1) || (detection->get_class_id() == m_class_id))
        {
            detections.push_back(detection);
            hailo_roi->remove_object(detection);
        }
        else if (m_block_non_tracked_class_id)
        {
            hailo_roi->remove_object(detection);
        }
    }

    // Swap the detections in the roi with just the online tracked detections
    std::vector<HailoDetectionPtr> online_detection_ptrs =
        HailoTracker::GetInstance().update(m_tracker_name, detections);

    hailo_common::add_detection_pointers(hailo_roi, online_detection_ptrs);

    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    if (m_print_fps)
    {
        std::cout << "Tracker time = " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count()
                  << "[microseconds]" << std::endl;
    }
    HAILO_ANALYTICS_LOG_TRACE("Tracker time = {}[microseconds]",
                              std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
    send_to_subscribers(data);

    return AppStatus::SUCCESS;
}

TrackerStageBuild::Builder &TrackerStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

TrackerStageBuild::Builder &TrackerStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}

TrackerStageBuild::Builder &TrackerStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

TrackerStageBuild::Builder &TrackerStageBuild::Builder::set_classification_id(int id)
{
    m_classification_id = id;
    return *this;
}

TrackerStageBuild::Builder &TrackerStageBuild::Builder::set_block_non_tracked_classification_id(bool block)
{
    m_block_non_tracked_class_id = block;
    return *this;
}

TrackerStageBuild::Builder &TrackerStageBuild::Builder::set_printfps_opt(bool activate)
{
    m_print_fps = activate;
    return *this;
}

TrackerStageBuild::Builder &TrackerStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<TrackerStage> TrackerStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<TrackerStage>(m_stage_name.value(), m_queue_size, m_leaky, m_classification_id,
                                          m_block_non_tracked_class_id, m_trace, m_print_fps);
}

TrackerStageBuild::Builder TrackerStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::ai
