#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/ai/analytics_db_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::ai
{

AnalyticsDBStage::AnalyticsDBStage(const std::string &name, size_t queue_size, bool leaky,
                                   const std::string &analytics_data_id, AnalyticsType type,
                                   bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
      m_analytics_data_id(analytics_data_id), m_type(type)
{
    switch (m_type)
    {
    case AnalyticsType::INSTANCE_SEGMENTATION:
        break;
    case AnalyticsType::SEMANTIC_SEGMENTATION:
        break;
    case AnalyticsType::DETECTION:
        break;
    default:
        HAILO_ANALYTICS_LOG_ERROR("Unsupported AnalyticsType: {}", static_cast<int>(m_type));
        throw std::runtime_error("Unsupported AnalyticsType in AnalyticsDBStage");
    }
}

std::chrono::time_point<std::chrono::steady_clock> AnalyticsDBStage::get_timestamp_from_buffer(BufferPtr data) const
{
    auto isp_timestamp = data->get_buffer()->isp_timestamp_ns;
    return std::chrono::time_point<std::chrono::steady_clock>(std::chrono::nanoseconds(isp_timestamp));
}

std::vector<HailoMediaLibraryBufferPtr> AnalyticsDBStage::collect_tensor_buffers(BufferPtr data) const
{
    std::vector<HailoMediaLibraryBufferPtr> tensor_buffers;
    std::vector<MetadataPtr> tensor_metadata = data->get_metadata_of_type(MetadataType::TENSOR);
    HAILO_ANALYTICS_LOG_TRACE("Found {} tensor metadata entries", tensor_metadata.size());

    for (const auto &metadata : tensor_metadata)
    {
        TensorMetadataPtr tensor_meta = std::dynamic_pointer_cast<TensorMetadata>(metadata);
        if (tensor_meta)
        {
            HailoMediaLibraryBufferPtr buffer = tensor_meta->get_buffer()->get_buffer();
            tensor_buffers.push_back(buffer);
        }
    }
    HAILO_ANALYTICS_LOG_TRACE("Collected {} tensor buffers to keep alive in analytics DB", tensor_buffers.size());
    return tensor_buffers;
}

AppStatus AnalyticsDBStage::add_cached_or_empty_semantic_segmentation_entry(BufferPtr data, const std::string &reason)
{
    auto &analytics_db = AnalyticsDB::instance();
    auto timestamp = get_timestamp_from_buffer(data);
    auto isp_timestamp_ns = data->get_buffer()->isp_timestamp_ns;

    SemanticSegmentationAnalyticsData db_data;
    if (m_last_semantic_segmentation_data.has_value())
    {
        // Repeat the previous result with the current frame's timestamp
        db_data = m_last_semantic_segmentation_data.value();
        db_data.ts = timestamp;
        HAILO_ANALYTICS_LOG_TRACE("Repeating cached semantic segmentation result ({} masks) for timestamp {} ns",
                                  db_data.analytics_buffer.size(), isp_timestamp_ns);
        m_last_semantic_segmentation_data.reset();
    }
    else
    {
        db_data = {.ts = timestamp, .analytics_buffer = {}, .medialib_buffer_ptrs = {}};
    }
    auto ret = analytics_db.add_semantic_segmentation_entry(m_analytics_data_id, db_data);

    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to add entry, analytics_id='{}', isp_timestamp={} ns", m_analytics_data_id,
                                  isp_timestamp_ns);
        return AppStatus::MEDIA_LIBRARY_ERROR;
    }

    return AppStatus::SUCCESS;
}

void AnalyticsDBStage::convert_bbox_to_pixel_coords(const HailoBBox &bbox, uint32_t width, uint32_t height,
                                                    float32_t &x_min, float32_t &y_min, float32_t &x_max,
                                                    float32_t &y_max)
{
    // Use float32_t to preserve sub-pixel accuracy which may be needed for downstream processing
    x_min = bbox.xmin() * static_cast<float32_t>(width);
    y_min = bbox.ymin() * static_cast<float32_t>(height);
    x_max = bbox.xmax() * static_cast<float32_t>(width);
    y_max = bbox.ymax() * static_cast<float32_t>(height);
}

void AnalyticsDBStage::convert_bbox_to_clamped_pixel_coords(float norm_x_min, float norm_y_min, float norm_x_max,
                                                            float norm_y_max, uint32_t width, uint32_t height,
                                                            uint32_t &x_min, uint32_t &y_min, uint32_t &x_max,
                                                            uint32_t &y_max)
{
    // Use floor for min coords to ensure we don't start outside the bounding box
    // Use ceil for max coords to ensure we fully cover the bounding box
    // Clamp to valid range [0, dimension] to prevent overflow and out-of-bounds values

    float pixel_x_min = std::floor(norm_x_min * static_cast<float>(width));
    float pixel_y_min = std::floor(norm_y_min * static_cast<float>(height));
    float pixel_x_max = std::ceil(norm_x_max * static_cast<float>(width));
    float pixel_y_max = std::ceil(norm_y_max * static_cast<float>(height));

    x_min = static_cast<uint32_t>(std::clamp(pixel_x_min, 0.0f, static_cast<float>(width)));
    y_min = static_cast<uint32_t>(std::clamp(pixel_y_min, 0.0f, static_cast<float>(height)));
    x_max = static_cast<uint32_t>(std::clamp(pixel_x_max, 0.0f, static_cast<float>(width)));
    y_max = static_cast<uint32_t>(std::clamp(pixel_y_max, 0.0f, static_cast<float>(height)));
}

tl::expected<detection_analytics_config_t, AppStatus> AnalyticsDBStage::get_detection_config() const
{
    auto &analytics_db = AnalyticsDB::instance();
    auto application_analytics_config = analytics_db.get_application_analytics_config();

    auto it = application_analytics_config.detection_analytics_config.find(m_analytics_data_id);
    if (it == application_analytics_config.detection_analytics_config.end())
    {
        HAILO_ANALYTICS_LOG_ERROR("Detection analytics config not found for ID: {}", m_analytics_data_id);
        return tl::unexpected(AppStatus::MEDIA_LIBRARY_ERROR);
    }

    return it->second;
}

tl::expected<instance_segmentation_analytics_config_t, AppStatus> AnalyticsDBStage::get_instance_segmentation_config()
    const
{
    auto &analytics_db = AnalyticsDB::instance();
    auto application_analytics_config = analytics_db.get_application_analytics_config();

    auto it = application_analytics_config.instance_segmentation_analytics_config.find(m_analytics_data_id);
    if (it == application_analytics_config.instance_segmentation_analytics_config.end())
    {
        HAILO_ANALYTICS_LOG_ERROR("Instance segmentation analytics config not found for ID: {}", m_analytics_data_id);
        return tl::unexpected(AppStatus::MEDIA_LIBRARY_ERROR);
    }

    return it->second;
}

tl::expected<semantic_segmentation_analytics_config_t, AppStatus> AnalyticsDBStage::get_semantic_segmentation_config()
    const
{
    auto &analytics_db = AnalyticsDB::instance();
    auto application_analytics_config = analytics_db.get_application_analytics_config();

    auto it = application_analytics_config.semantic_segmentation_analytics_config.find(m_analytics_data_id);
    if (it == application_analytics_config.semantic_segmentation_analytics_config.end())
    {
        HAILO_ANALYTICS_LOG_ERROR("Semantic segmentation analytics config not found for ID: {}", m_analytics_data_id);
        return tl::unexpected(AppStatus::MEDIA_LIBRARY_ERROR);
    }

    return it->second;
}

std::optional<hailo_semantic_segmentation_mask_t> AnalyticsDBStage::extract_mask_from_detection(
    HailoDetectionPtr detection, int target_class_id, uint32_t image_width, uint32_t image_height)
{
    auto detection_bbox = detection->get_bbox();
    auto nested_objects = detection->get_objects();

    // Find the mask at the target_class_id index among HAILO_CLASS_MASK objects
    int mask_index = 0;
    for (const auto &nested : nested_objects)
    {
        if (nested->get_type() == HAILO_CLASS_MASK)
        {
            if (mask_index == target_class_id)
            {
                HailoClassMaskPtr hailo_mask = std::dynamic_pointer_cast<HailoClassMask>(nested);
                if (hailo_mask)
                {
                    hailo_semantic_segmentation_mask_t mask;
                    mask.class_id = static_cast<uint16_t>(target_class_id);
                    mask.width = static_cast<uint32_t>(hailo_mask->get_width());
                    mask.height = static_cast<uint32_t>(hailo_mask->get_height());
                    mask.transparency = hailo_mask->get_transparency();
                    const auto &mask_data = hailo_mask->get_data();
                    mask.mask_size = hailo_mask->get_width() * hailo_mask->get_height();

                    // SAFETY: const_cast required due to hailo_semantic_segmentation_mask_t API
                    // The mask data pointer is stored in analytics DB but NOT modified.
                    // Lifetime safety: The tensor_buffers collected via collect_tensor_buffers()
                    // are stored alongside masks in SemanticSegmentationAnalyticsData, ensuring
                    // the underlying mask data remains valid for the lifetime of the analytics entry.
                    // TODO: Ideally hailo_semantic_segmentation_mask_t.mask should be const uint8_t*
                    mask.mask = const_cast<uint8_t *>(mask_data);

                    mask.detection_x = detection_bbox.xmin() * image_width;
                    mask.detection_y = detection_bbox.ymin() * image_height;
                    mask.detection_width = detection_bbox.width() * image_width;
                    mask.detection_height = detection_bbox.height() * image_height;

                    return mask;
                }
            }
            mask_index++;
        }
    }

    return std::nullopt; // Mask not found at target index
}

AppStatus AnalyticsDBStage::process_detection(BufferPtr data, HailoMediaLibraryBufferPtr /*media_lib_buffer*/)
{
    auto config = get_detection_config();
    if (!config)
    {
        return config.error();
    }

    uint32_t ai_width = config->width;
    uint32_t ai_height = config->height;

    // Extract detection objects from ROI
    auto hailo_detections = hailo_common::get_hailo_detections(data->get_roi());
    std::vector<hailo_detection_t> detections = {};
    detections.reserve(hailo_detections.size());

    for (const auto &hailo_detection : hailo_detections)
    {
        HailoBBox bbox = hailo_detection->get_bbox();

        // Create detection struct with pixel coordinates
        hailo_detection_t detection;
        detection.score = hailo_detection->get_confidence();
        detection.class_id = static_cast<uint16_t>(hailo_detection->get_class_id());

        convert_bbox_to_pixel_coords(bbox, ai_width, ai_height, detection.x_min, detection.y_min, detection.x_max,
                                     detection.y_max);

        detections.push_back(detection);
    }

    auto timestamp = get_timestamp_from_buffer(data);
    auto isp_timestamp = data->get_buffer()->isp_timestamp_ns;

    HAILO_ANALYTICS_LOG_TRACE("Adding {} detections to analytics DB at id {}, timestamp {}", detections.size(),
                              m_analytics_data_id, isp_timestamp);

    auto &analytics_db = AnalyticsDB::instance();
    DetectionAnalyticsData db_data = {.ts = timestamp, .analytics_buffer = detections};
    auto ret = analytics_db.add_detection_entry(m_analytics_data_id, db_data);

    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to add detection entry to analytics DB");
        return AppStatus::MEDIA_LIBRARY_ERROR;
    }

    return AppStatus::SUCCESS;
}

AppStatus AnalyticsDBStage::process_instance_segmentation(BufferPtr data, HailoMediaLibraryBufferPtr media_lib_buffer)
{
    auto config = get_instance_segmentation_config();
    if (!config)
    {
        return config.error();
    }

    uint32_t ai_width = config->width;
    uint32_t ai_height = config->height;

    auto hailo_segmetations = hailo_common::get_hailo_segmentations(data->get_roi());
    std::vector<hailo_detection_with_byte_mask_t> segmentations = {};
    for (const auto &hailo_segmentation : hailo_segmetations)
    {
        hailo_detection_with_byte_mask_t segmentation = hailo_segmentation->get_segmentation();

        float norm_x_min = segmentation.box.x_min;
        float norm_y_min = segmentation.box.y_min;
        float norm_x_max = segmentation.box.x_max;
        float norm_y_max = segmentation.box.y_max;

        uint32_t x_min, y_min, x_max, y_max;
        convert_bbox_to_clamped_pixel_coords(norm_x_min, norm_y_min, norm_x_max, norm_y_max, ai_width, ai_height, x_min,
                                             y_min, x_max, y_max);

        segmentation.box.x_min = static_cast<float32_t>(x_min);
        segmentation.box.y_min = static_cast<float32_t>(y_min);
        segmentation.box.x_max = static_cast<float32_t>(x_max);
        segmentation.box.y_max = static_cast<float32_t>(y_max);

        auto roi_width = x_max - x_min;
        auto roi_height = y_max - y_min;
        HAILO_ANALYTICS_LOG_TRACE("Segmentation found: label {}, score {}, bbox: [{}, {}, {}, {}], mask size: {}, "
                                  "ROI width: {}, ROI height: {}",
                                  segmentation.class_id, segmentation.score, segmentation.box.x_min,
                                  segmentation.box.y_min, segmentation.box.x_max, segmentation.box.y_max,
                                  segmentation.mask_size, roi_width, roi_height);
        segmentations.push_back(segmentation);
    }

    auto timestamp = get_timestamp_from_buffer(data);
    auto isp_timestamp = data->get_buffer()->isp_timestamp_ns;

    HAILO_ANALYTICS_LOG_DEBUG("Adding {} segmentations to analytics DB at id {}, timestamp {}", segmentations.size(),
                              m_analytics_data_id, isp_timestamp);

    auto &analytics_db = AnalyticsDB::instance();
    InstanceSegmentationAnalyticsData db_data = {
        .ts = timestamp, .analytics_buffer = segmentations, .medialib_buffer_ptr = media_lib_buffer};
    auto ret = analytics_db.add_instance_segmentation_entry(m_analytics_data_id, db_data);

    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to add entry to analytics DB");
        return AppStatus::MEDIA_LIBRARY_ERROR;
    }

    return AppStatus::SUCCESS;
}

AppStatus AnalyticsDBStage::process_semantic_segmentation(BufferPtr data, HailoMediaLibraryBufferPtr media_lib_buffer)
{
    auto config = get_semantic_segmentation_config();
    if (!config)
    {
        return config.error();
    }

    uint32_t image_width = config->width;
    uint32_t image_height = config->height;

    HAILO_ANALYTICS_LOG_TRACE("Image dimensions: {}x{}", image_width, image_height);

    auto roi = data->get_roi();

    // Get the main ROI bbox - this contains the detection coordinates from BBox crop stage
    auto main_roi_bbox = roi->get_bbox();
    HAILO_ANALYTICS_LOG_TRACE("Main ROI (detection) bbox: ({:.3f}, {:.3f}) {:.3f}x{:.3f}", main_roi_bbox.xmin(),
                              main_roi_bbox.ymin(), main_roi_bbox.width(), main_roi_bbox.height());

    // Get sub-objects which should be detection objects containing nested masks
    auto sub_objects = roi->get_objects();
    HAILO_ANALYTICS_LOG_TRACE("Total sub-objects in ROI: {}", sub_objects.size());

    // Build masks directly with their detection coordinates
    std::vector<hailo_semantic_segmentation_mask_t> masks = {};
    masks.reserve(sub_objects.size());

    for (const auto &obj : sub_objects)
    {
        if (obj->get_type() != HAILO_DETECTION)
        {
            continue;
        }

        auto detection = std::dynamic_pointer_cast<HailoDetection>(obj);
        if (!detection)
        {
            continue;
        }

        auto detection_label = detection->get_label();
        auto target_class_id = find_class_id_for_label(detection_label, config->labels);

        if (!target_class_id)
        {
            HAILO_ANALYTICS_LOG_TRACE("No matching class_id found for detection label '{}', skipping", detection_label);
            continue;
        }

        HAILO_ANALYTICS_LOG_TRACE("Processing detection with label '{}', class_id={}", detection_label,
                                  *target_class_id);

        auto mask = extract_mask_from_detection(detection, *target_class_id, image_width, image_height);
        if (mask)
        {
            masks.push_back(*mask);
        }
    }

    HAILO_ANALYTICS_LOG_TRACE("Total semantic masks found: {}", masks.size());

    if (masks.empty())
    {
        HAILO_ANALYTICS_LOG_TRACE("No semantic masks found in sub-objects - adding cached or empty entry");
        return add_cached_or_empty_semantic_segmentation_entry(data, "no masks found after processing detections");
    }

    auto tensor_buffers = collect_tensor_buffers(data);
    auto timestamp = get_timestamp_from_buffer(data);

    HAILO_ANALYTICS_LOG_TRACE("===== Sending {} masks to Analytics DB =====", masks.size());
    HAILO_ANALYTICS_LOG_TRACE("Analytics ID: {}, Timestamp: {}", m_analytics_data_id,
                              data->get_buffer()->isp_timestamp_ns);
    for (size_t i = 0; i < masks.size(); i++)
    {
        HAILO_ANALYTICS_LOG_TRACE("Mask {}: class_id={}, size={}x{}, bbox=({:.3f},{:.3f}) {:.3f}x{:.3f}", i,
                                  masks[i].class_id, masks[i].width, masks[i].height, masks[i].detection_x,
                                  masks[i].detection_y, masks[i].detection_width, masks[i].detection_height);
    }

    auto &analytics_db = AnalyticsDB::instance();
    SemanticSegmentationAnalyticsData db_data = {
        .ts = timestamp, .analytics_buffer = masks, .medialib_buffer_ptrs = tensor_buffers};
    auto ret = analytics_db.add_semantic_segmentation_entry(m_analytics_data_id, db_data);

    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("*** FAILED to add semantic segmentation entry to analytics DB ***");
        return AppStatus::MEDIA_LIBRARY_ERROR;
    }

    // Cache the result so skipped frames can repeat it
    m_last_semantic_segmentation_data = db_data;

    HAILO_ANALYTICS_LOG_TRACE("✓ Successfully added {} masks to analytics DB", masks.size());

    return AppStatus::SUCCESS;
}

AppStatus AnalyticsDBStage::process(BufferPtr data)
{
    HAILO_ANALYTICS_LOG_TRACE("[{}] process() called", m_stage_name);

    AppStatus status = AppStatus::SUCCESS;

    if (m_type == AnalyticsType::SEMANTIC_SEGMENTATION)
    {
        // For semantic segmentation, first check if we have any HailoClassMask objects
        auto roi = data->get_roi();
        if (!roi)
        {
            HAILO_ANALYTICS_LOG_TRACE("[{}] No ROI found in buffer - adding empty entry", m_stage_name);
            auto status = add_cached_or_empty_semantic_segmentation_entry(data, "no ROI found");
            send_to_subscribers(data);
            return status;
        }

        // Check for mask objects in the ROI
        auto mask_objects = roi->get_objects_typed(HAILO_CLASS_MASK);
        HAILO_ANALYTICS_LOG_TRACE("[{}] Found {} HAILO_CLASS_MASK objects via get_objects_typed", m_stage_name,
                                  mask_objects.size());

        // Also check all objects to see what we have
        auto all_objects = roi->get_objects();
        HAILO_ANALYTICS_LOG_TRACE("[{}] Total objects in ROI: {}", m_stage_name, all_objects.size());

        // If no masks found directly, check inside detection objects (nested)
        if (mask_objects.empty())
        {
            HAILO_ANALYTICS_LOG_TRACE("[{}] No direct HAILO_CLASS_MASK objects, checking nested objects in detections",
                                      m_stage_name);

            // Look for masks inside detection objects
            for (const auto &obj : all_objects)
            {
                if (obj->get_type() == HAILO_DETECTION)
                {
                    auto detection = std::dynamic_pointer_cast<HailoDetection>(obj);
                    if (detection)
                    {
                        auto nested_objects = detection->get_objects();
                        HAILO_ANALYTICS_LOG_TRACE("[{}]   Detection has {} nested objects", m_stage_name,
                                                  nested_objects.size());
                        for (const auto &nested : nested_objects)
                        {
                            HAILO_ANALYTICS_LOG_TRACE("[{}]     Nested object type: {}", m_stage_name,
                                                      nested->get_type());
                            if (nested->get_type() == HAILO_CLASS_MASK)
                            {
                                mask_objects.push_back(nested);
                            }
                        }
                    }
                }
            }
            HAILO_ANALYTICS_LOG_TRACE("[{}] Found {} HAILO_CLASS_MASK objects in nested detections", m_stage_name,
                                      mask_objects.size());
        }

        if (mask_objects.empty())
        {
            auto status = add_cached_or_empty_semantic_segmentation_entry(data, "no HAILO_CLASS_MASK objects found");
            send_to_subscribers(data);
            return status;
        }

        // Now check for tensor metadata (should be available from aggregator)
        std::vector<MetadataPtr> metadata = data->get_metadata_of_type(MetadataType::TENSOR);
        if (metadata.empty())
        {
            HAILO_ANALYTICS_LOG_TRACE("[{}] TENSOR metadata missing for buffer with {} HailoClassMask objects "
                                      "- adding empty entry",
                                      m_stage_name, mask_objects.size());
            auto status = add_cached_or_empty_semantic_segmentation_entry(data, "tensor metadata missing");
            send_to_subscribers(data);
            return status;
        }

        HAILO_ANALYTICS_LOG_TRACE("[{}] Found TENSOR metadata, proceeding to process_semantic_segmentation",
                                  m_stage_name);

        TensorMetadataPtr buffer_metadata_ptr = std::dynamic_pointer_cast<TensorMetadata>(metadata[0]);
        HailoMediaLibraryBufferPtr media_lib_buffer = buffer_metadata_ptr->get_buffer()->get_buffer();

        status = process_semantic_segmentation(data, media_lib_buffer);
    }
    else
    {
        // For other analytics types, require tensor metadata
        std::vector<MetadataPtr> metadata = data->get_metadata_of_type(MetadataType::TENSOR);
        if (metadata.empty())
        {
            HAILO_ANALYTICS_LOG_ERROR("[{}] No TENSOR metadata found in buffer", m_stage_name);
            return AppStatus::PIPELINE_ERROR;
        }

        TensorMetadataPtr buffer_metadata_ptr = std::dynamic_pointer_cast<TensorMetadata>(metadata[0]);
        HailoMediaLibraryBufferPtr media_lib_buffer = buffer_metadata_ptr->get_buffer()->get_buffer();
        media_lib_buffer->sync_end();

        switch (m_type)
        {
        case AnalyticsType::INSTANCE_SEGMENTATION:
            status = process_instance_segmentation(data, media_lib_buffer);
            break;
        case AnalyticsType::DETECTION:
            status = process_detection(data, media_lib_buffer);
            break;
        default:
            HAILO_ANALYTICS_LOG_ERROR("Unsupported AnalyticsType: {}", static_cast<int>(m_type));
            return AppStatus::MEDIA_LIBRARY_ERROR;
        }
    }

    if (status != AppStatus::SUCCESS)
    {
        return status;
    }

    send_to_subscribers(data);
    return AppStatus::SUCCESS;
}

AnalyticsDBStageBuild::Builder &AnalyticsDBStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

AnalyticsDBStageBuild::Builder &AnalyticsDBStageBuild::Builder::set_queue_size(size_t size)
{
    m_queue_size = size;
    return *this;
}

AnalyticsDBStageBuild::Builder &AnalyticsDBStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

AnalyticsDBStageBuild::Builder &AnalyticsDBStageBuild::Builder::set_analytics_data_id(
    const std::string &analytics_data_id)
{
    m_analytics_data_id = analytics_data_id;
    return *this;
}

AnalyticsDBStageBuild::Builder &AnalyticsDBStageBuild::Builder::set_type(AnalyticsType type)
{
    m_type = type;
    return *this;
}

AnalyticsDBStageBuild::Builder &AnalyticsDBStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<AnalyticsDBStage> AnalyticsDBStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING(m_analytics_data_id.has_value(), "set_analytics_data_id");

    return std::make_shared<AnalyticsDBStage>(m_stage_name.value(), m_queue_size, m_leaky, m_analytics_data_id.value(),
                                              m_type, m_trace);
}

AnalyticsDBStageBuild::Builder AnalyticsDBStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::ai
