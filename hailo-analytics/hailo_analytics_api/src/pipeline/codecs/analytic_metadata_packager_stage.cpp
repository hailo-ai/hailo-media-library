
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/codecs/analytic_metadata_packager_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::codecs
{

static nlohmann::json &append_detection(HailoDetectionPtr detection, const HailoBBox &roi_bbox, uint32_t native_width,
                                        uint32_t native_height, nlohmann::json &metadata_json)
{
    auto detection_bbox = detection->get_bbox();

    if (!metadata_json.contains(analytic_metadata_fields::DETECTIONS))
        metadata_json[analytic_metadata_fields::DETECTIONS] = nlohmann::json::array();

    metadata_json[analytic_metadata_fields::DETECTIONS].push_back({
        {analytic_metadata_fields::detection::LABEL, detection->get_label()},
        {analytic_metadata_fields::detection::DETECTION_CONFIDENCE, detection->get_confidence()},
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

static bool append_landmarks(HailoLandmarksPtr landmarks, const HailoBBox &roi_bbox, uint32_t native_width,
                             uint32_t native_height, nlohmann::json &metadata_json)
{
    const auto &points_input = landmarks->get_points();
    const auto &pairs_input = landmarks->get_pairs();
    if (points_input.empty())
        return false;

    if (!metadata_json.contains(analytic_metadata_fields::LANDMARKS))
        metadata_json[analytic_metadata_fields::LANDMARKS] = nlohmann::json::array();

    const float scale_x = roi_bbox.width() * (float)native_width;
    const float offset_x = roi_bbox.xmin() * (float)native_width;
    const float scale_y = roi_bbox.height() * (float)native_height;
    const float offset_y = roi_bbox.ymin() * (float)native_height;

    std::vector<float> points;
    points.reserve(points_input.size() * 3);
    for (const auto &point : points_input)
    {
        points.push_back(point.x() * scale_x + offset_x);
        points.push_back(point.y() * scale_y + offset_y);
        points.push_back(point.confidence());
    }

    std::vector<int> pairs;
    pairs.reserve(pairs_input.size() * 2);
    for (const auto &pair : pairs_input)
    {
        pairs.push_back(static_cast<int>(pair.first));
        pairs.push_back(static_cast<int>(pair.second));
    }

    nlohmann::json landmark_obj = {
        {analytic_metadata_fields::landmark::POINTS_FORMAT, analytic_metadata_fields::landmark::POINTS_FORMAT_VALUE},
        {analytic_metadata_fields::landmark::POINTS_STRIDE, analytic_metadata_fields::landmark::POINTS_STRIDE_VALUE},
        {analytic_metadata_fields::landmark::POINTS, std::move(points)},
        {analytic_metadata_fields::landmark::PAIRS, std::move(pairs)}};

    metadata_json[analytic_metadata_fields::LANDMARKS].push_back(std::move(landmark_obj));

    return true;
}

static void append_classification(HailoClassificationPtr classification, nlohmann::json &metadata_json)
{
    if (!metadata_json.contains(analytic_metadata_fields::CLASSIFICATIONS))
        metadata_json[analytic_metadata_fields::CLASSIFICATIONS] = nlohmann::json::array();

    metadata_json[analytic_metadata_fields::CLASSIFICATIONS].push_back({
        {analytic_metadata_fields::classification::TYPE, classification->get_classification_type()},
        {analytic_metadata_fields::classification::LABEL, classification->get_label()},
        {analytic_metadata_fields::classification::CLASSIFICATION_CONFIDENCE, classification->get_confidence()},
    });
}

static void append_objects_recursive(HailoROIPtr roi, const HailoBBox &parent_bbox, uint32_t native_width,
                                     uint32_t native_height, nlohmann::json &metadata_json)
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
                append_detection(detection, parent_bbox, native_width, native_height, metadata_json);

            current_node = &detections_array.back();
            object_bbox = hailo_common::create_flattened_bbox(parent_bbox, detection->get_bbox());
            break;
        }
        case HAILO_LANDMARKS: {
            HailoLandmarksPtr landmarks = std::dynamic_pointer_cast<HailoLandmarks>(obj);

            if (append_landmarks(landmarks, parent_bbox, native_width, native_height, metadata_json))
            {
                current_node = &metadata_json[analytic_metadata_fields::LANDMARKS].back();
                object_bbox = parent_bbox;
            }
            break;
        }
        case HAILO_UNIQUE_ID: {
            HailoUniqueIDPtr unique_id = std::dynamic_pointer_cast<HailoUniqueID>(obj);
            metadata_json[analytic_metadata_fields::detection::TRACKING_ID] = unique_id->get_id();
            break;
        }
        case HAILO_CLASSIFICATION: {
            HailoClassificationPtr classification = std::dynamic_pointer_cast<HailoClassification>(obj);
            append_classification(classification, metadata_json);
            break;
        }
        default:
            HAILO_ANALYTICS_LOG_INFO("analytic_metadata_json: skipping unknown object type {}", obj->get_type());
            break;
        }

        HailoROIPtr obj_roi = std::dynamic_pointer_cast<HailoROI>(obj);
        if (current_node != nullptr && obj_roi != nullptr)
        {
            append_objects_recursive(obj_roi, object_bbox, native_width, native_height, *current_node);
        }
    }
}

nlohmann::json build_metadata_json(BufferPtr data)
{
    auto roi = data->get_roi();
    if (!roi)
        return {};

    auto native_width = data->get_buffer()->buffer_data->width;
    auto native_height = data->get_buffer()->buffer_data->height;

    nlohmann::json metadata_json;
    append_objects_recursive(roi, roi->get_bbox(), native_width, native_height, metadata_json);

    if (metadata_json.empty())
        return {};

    metadata_json[analytic_metadata_fields::ISP_TIMESTAMP] = data->get_buffer()->isp_timestamp_ns;
    metadata_json[analytic_metadata_fields::FRAME_WIDTH] = native_width;
    metadata_json[analytic_metadata_fields::FRAME_HEIGHT] = native_height;

    return metadata_json;
}

AnalyticMetadataPackagerStage::AnalyticMetadataPackagerStage(std::string name, Format format, size_t queue_size,
                                                             bool leaky, bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations), m_format(format)
{
}

AppStatus AnalyticMetadataPackagerStage::process(BufferPtr data)
{
    nlohmann::json metadata_json = build_metadata_json(data);
    if (metadata_json.empty())
        return AppStatus::SUCCESS;

    auto zmq_msg = std::make_shared<HailoZMQMessage>();

    if (m_format == Format::JSON)
    {
        zmq_msg->set_output_msg(metadata_json.dump());
    }
    else
    {
        auto binary_msg = nlohmann::json::to_msgpack(metadata_json);
        zmq_msg->set_output_msg(std::string(reinterpret_cast<const char *>(binary_msg.data()), binary_msg.size()));
    }

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

AnalyticMetadataPackagerStageBuild::Builder &AnalyticMetadataPackagerStageBuild::Builder::set_format_opt(Format format)
{
    m_format = format;
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

    return std::make_shared<AnalyticMetadataPackagerStage>(m_stage_name.value(), m_format, m_queue_size, m_leaky,
                                                           m_trace);
}

AnalyticMetadataPackagerStageBuild::Builder AnalyticMetadataPackagerStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::codecs
