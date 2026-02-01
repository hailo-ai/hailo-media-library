
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/codecs/analytic_metadata_packager_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::codecs
{

AnalyticMetadataPackagerStage::AnalyticMetadataPackagerStage(std::string name, size_t queue_size, bool leaky,
                                                             bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations)
{
}

AppStatus AnalyticMetadataPackagerStage::init()
{
    return AppStatus::SUCCESS;
}

AppStatus AnalyticMetadataPackagerStage::deinit()
{
    return AppStatus::SUCCESS;
}

nlohmann::json &AnalyticMetadataPackagerStage::process_detection(HailoDetectionPtr detection, const HailoBBox &roi_bbox,
                                                                 uint32_t native_width, uint32_t native_height,
                                                                 nlohmann::json &metadata_json)
{
    auto detection_bbox = detection->get_bbox();

    if (!metadata_json.contains(analytic_metadata_fields::DETECTIONS))
        metadata_json[analytic_metadata_fields::DETECTIONS] = nlohmann::json::array();

    metadata_json[analytic_metadata_fields::DETECTIONS].push_back({
        {analytic_metadata_fields::detection::LABEL, detection->get_label()},
        {analytic_metadata_fields::detection::CONFIDENCE, detection->get_confidence()},
        {analytic_metadata_fields::detection::BBOX,
         {
             {analytic_metadata_fields::detection::bbox::XMIN,
              ((detection_bbox.xmin() * roi_bbox.width()) + roi_bbox.xmin()) * native_width},
             {analytic_metadata_fields::detection::bbox::YMIN,
              ((detection_bbox.ymin() * roi_bbox.height()) + roi_bbox.ymin()) * native_height},
             {analytic_metadata_fields::detection::bbox::XMAX,
              ((detection_bbox.xmax() * roi_bbox.width()) + roi_bbox.xmin()) * native_width},
             {analytic_metadata_fields::detection::bbox::YMAX,
              ((detection_bbox.ymax() * roi_bbox.height()) + roi_bbox.ymin()) * native_height},
         }},
    });

    return metadata_json[analytic_metadata_fields::DETECTIONS];
}

nlohmann::json &AnalyticMetadataPackagerStage::process_landmarks(HailoLandmarksPtr landmarks, const HailoBBox &roi_bbox,
                                                                 uint32_t native_width, uint32_t native_height,
                                                                 nlohmann::json &metadata_json)
{
    const auto &points_input = landmarks->get_points();
    const auto &pairs_input = landmarks->get_pairs();
    if (points_input.empty())
    {
        static nlohmann::json empty_json;
        return empty_json;
    }

    // Initialize landmarks array if not present
    if (!metadata_json.contains(analytic_metadata_fields::LANDMARKS))
        metadata_json[analytic_metadata_fields::LANDMARKS] = nlohmann::json::array();

    // Pre-calculate scalars (Loop Hoisting)
    // Formula: ((p * roi_w) + roi_xmin) * native_w
    // Simplified: p * (roi_w * native_w) + (roi_xmin * native_w)
    const float scale_x = roi_bbox.width() * (float)native_width;
    const float offset_x = roi_bbox.xmin() * (float)native_width;
    const float scale_y = roi_bbox.height() * (float)native_height;
    const float offset_y = roi_bbox.ymin() * (float)native_height;

    // Reuse class member buffers to avoid repeated allocations
    m_points_buffer.clear();
    m_points_buffer.reserve(points_input.size() * 3);

    m_pairs_buffer.clear();
    m_pairs_buffer.reserve(pairs_input.size() * 2);

    for (const auto &point : points_input)
    {
        m_points_buffer.push_back(point.x() * scale_x + offset_x);
        m_points_buffer.push_back(point.y() * scale_y + offset_y);
        m_points_buffer.push_back(point.confidence());
    }

    for (const auto &pair : pairs_input)
    {
        m_pairs_buffer.push_back(static_cast<int>(pair.first));
        m_pairs_buffer.push_back(static_cast<int>(pair.second));
    }

    // Create a new landmark object and add it to the array
    // Copy into JSON (nlohmann::json will efficiently store the vector data)
    nlohmann::json landmark_obj = {
        {analytic_metadata_fields::landmark::POINTS_FORMAT, analytic_metadata_fields::landmark::POINTS_FORMAT_VALUE},
        {analytic_metadata_fields::landmark::POINTS_STRIDE, analytic_metadata_fields::landmark::POINTS_STRIDE_VALUE},
        {analytic_metadata_fields::landmark::POINTS, m_points_buffer},
        {analytic_metadata_fields::landmark::PAIRS, m_pairs_buffer}};

    metadata_json[analytic_metadata_fields::LANDMARKS].push_back(std::move(landmark_obj));

    return metadata_json[analytic_metadata_fields::LANDMARKS];
}

void AnalyticMetadataPackagerStage::process_objects_recursive(HailoROIPtr roi, const HailoBBox &parent_bbox,
                                                              uint32_t native_width, uint32_t native_height,
                                                              nlohmann::json &metadata_json)
{
    for (auto obj : roi->get_objects())
    {
        nlohmann::json *current_node = nullptr;
        HailoBBox object_bbox = parent_bbox;

        switch (obj->get_type())
        {
        case HAILO_DETECTION: {
            HailoDetectionPtr detection = std::dynamic_pointer_cast<HailoDetection>(obj);

            auto &detections_array =
                process_detection(detection, parent_bbox, native_width, native_height, metadata_json);

            // Get reference to the just-added detection node (last element in the array)
            current_node = &detections_array.back();

            // Create a flattened bbox for potential sub-objects
            object_bbox = hailo_common::create_flattened_bbox(parent_bbox, detection->get_bbox());
            break;
        }
        case HAILO_LANDMARKS: {
            HailoLandmarksPtr landmarks = std::dynamic_pointer_cast<HailoLandmarks>(obj);

            auto &landmarks_array =
                process_landmarks(landmarks, parent_bbox, native_width, native_height, metadata_json);

            // Only set current_node if we actually created a landmarks object (not empty)
            if (!landmarks_array.empty())
            {
                // Get reference to the just-added landmark node (last element in the array)
                current_node = &landmarks_array.back();
                // Landmarks don't have a bbox, so keep parent_bbox for any sub-objects
                object_bbox = parent_bbox;
            }
            break;
        }
        default:
            HAILO_ANALYTICS_LOG_INFO("AnalyticMetadataPackagerStage: Warning - Skipping unknown object type {}",
                                     obj->get_type());
            // Skip unknown object types
            break;
        }

        // Recursively process sub-objects for any object type that has them
        HailoROIPtr obj_roi = std::dynamic_pointer_cast<HailoROI>(obj);
        if (current_node != nullptr && obj_roi != nullptr)
        {
            // Process sub-objects recursively, passing the current object's node
            process_objects_recursive(obj_roi, object_bbox, native_width, native_height, *current_node);
        }
    }
}

void AnalyticMetadataPackagerStage::loop()
{
    init();

    while (!m_end_of_stream)
    {
        // the first queue is the video frame with analytic metadata
        BufferPtr main_buffer = m_queues[0]->pop();
        if (main_buffer == nullptr && m_end_of_stream)
        {
            break;
        }

        auto roi = main_buffer->get_roi();
        if (!roi)
        {
            HAILO_ANALYTICS_LOG_INFO("No ROI found in main buffer at stage {}", m_stage_name);
            continue;
        }

        auto native_width = main_buffer->get_buffer()->buffer_data->width;
        auto native_height = main_buffer->get_buffer()->buffer_data->height;

        nlohmann::json metadata_json;

        // Process all objects recursively
        process_objects_recursive(roi, roi->get_bbox(), native_width, native_height, metadata_json);

        if (metadata_json.empty())
        {
            continue;
        }

        metadata_json[analytic_metadata_fields::ISP_TIMESTAMP] = main_buffer->get_buffer()->isp_timestamp_ns;
        metadata_json[analytic_metadata_fields::FRAME_WIDTH] = native_width;
        metadata_json[analytic_metadata_fields::FRAME_HEIGHT] = native_height;

        BufferPtr MetadataBufferPtr = std::make_shared<Buffer>(nullptr);
        auto zmq_msg = std::make_shared<HailoZMQMessage>();

        // For best performance send the message as MessagePack binary instead of string since
        // metadata_json can contain very large data (especially with face landmarks)
        std::vector<uint8_t> binary_msg = nlohmann::json::to_msgpack(metadata_json);
        zmq_msg->set_output_msg(std::string(binary_msg.begin(), binary_msg.end()));

        MetadataBufferPtr->get_roi()->add_object(zmq_msg);

        send_to_subscribers(MetadataBufferPtr);
    }

    deinit();
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
