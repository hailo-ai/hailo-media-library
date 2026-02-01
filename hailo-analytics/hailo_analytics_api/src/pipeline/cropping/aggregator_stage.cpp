#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/cropping/aggregator_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::cropping
{

AggregatorStage::AggregatorStage(std::string name, std::string main_inlet_name, size_t main_queue_size,
                                 bool main_queue_leaky, std::string sub_inlet_name, size_t sub_queue_size,
                                 bool sub_queue_leaky, bool multi_scale, float iou_threshold, float m_border_threshold,
                                 bool skip_migration, bool trace_processing_operations,
                                 std::optional<int> static_sub_frames, bool copy_sub_frame_tensor_to_metadata)
    : hailo_analytics::pipeline::ThreadedStage(name, main_queue_size, main_queue_leaky, trace_processing_operations),
      m_main_inlet_name(main_inlet_name), m_main_queue_size(main_queue_size), m_sub_inlet_name(sub_inlet_name),
      m_sub_queue_size(sub_queue_size), m_static_sub_frames(static_sub_frames), m_multi_scale(multi_scale),
      m_iou_threshold(iou_threshold), m_border_threshold(m_border_threshold), m_skip_migration(skip_migration),
      m_copy_sub_frame_tensor_to_metadata(copy_sub_frame_tensor_to_metadata)
{
    m_queues.push_back(std::make_shared<Queue>(name, m_main_inlet_name, m_main_queue_size, main_queue_leaky));
    m_queues.push_back(std::make_shared<Queue>(name, m_sub_inlet_name, m_sub_queue_size, sub_queue_leaky));
}

AggregatorStage::~AggregatorStage() = default;

void AggregatorStage::add_queue(std::string name)
{
    (void)name;
    // Skip if called, queues are added in the constructor
}

HailoBBox AggregatorStage::create_flattened_bbox(const HailoBBox &bbox, const HailoBBox &parent_bbox)
{
    float xmin = parent_bbox.xmin() + bbox.xmin() * parent_bbox.width();
    float ymin = parent_bbox.ymin() + bbox.ymin() * parent_bbox.height();

    float width = bbox.width() * parent_bbox.width();
    float height = bbox.height() * parent_bbox.height();

    return HailoBBox(xmin, ymin, width, height);
}

void AggregatorStage::flatten_hailo_roi(HailoROIPtr roi, HailoROIPtr parent_roi, hailo_object_t filter_type)
{
    std::vector<HailoObjectPtr> objects = roi->get_objects();
    for (uint index = 0; index < objects.size(); index++)
    {
        if (objects[index]->get_type() == filter_type)
        {
            HailoROIPtr sub_obj_roi = std::dynamic_pointer_cast<HailoROI>(objects[index]);
            sub_obj_roi->set_bbox(std::move(create_flattened_bbox(sub_obj_roi->get_bbox(), roi->get_scaling_bbox())));
            parent_roi->add_object(sub_obj_roi);
            roi->remove_object(index);
            objects.erase(objects.begin() + index);
            index--;
        }
    }
}

void AggregatorStage::remove_exceeded_bboxes(HailoROIPtr hailo_tile_roi, float border_threshold)
{
    auto detections = hailo_common::get_hailo_detections(hailo_tile_roi);
    HailoBBox tile_bbox = hailo_tile_roi->get_scaling_bbox();

    for (const HailoDetectionPtr &detection : detections)
    {
        HailoBBox bbox = detection->get_bbox();
        bool exceed_xmin = (tile_bbox.xmin() != 0 && bbox.xmin() < border_threshold);
        bool exceed_xmax = (tile_bbox.xmax() != 1 && (1 - bbox.xmax()) < border_threshold);
        bool exceed_ymin = (tile_bbox.ymin() != 0 && bbox.ymin() < border_threshold);
        bool exceed_ymax = (tile_bbox.ymax() != 1 && (1 - bbox.ymax()) < border_threshold);

        if (exceed_xmin || exceed_xmax || exceed_ymin || exceed_ymax)
            hailo_tile_roi->remove_object(detection);
    }
}

float AggregatorStage::iou_calc(const HailoBBox &box_1, const HailoBBox &box_2)
{
    // Calculate IOU between two detection boxes
    const float width_of_overlap_area = std::min(box_1.xmax(), box_2.xmax()) - std::max(box_1.xmin(), box_2.xmin());
    const float height_of_overlap_area = std::min(box_1.ymax(), box_2.ymax()) - std::max(box_1.ymin(), box_2.ymin());
    const float positive_width_of_overlap_area = std::max(width_of_overlap_area, 0.0f);
    const float positive_height_of_overlap_area = std::max(height_of_overlap_area, 0.0f);
    const float area_of_overlap = positive_width_of_overlap_area * positive_height_of_overlap_area;
    const float box_1_area = (box_1.ymax() - box_1.ymin()) * (box_1.xmax() - box_1.xmin());
    const float box_2_area = (box_2.ymax() - box_2.ymin()) * (box_2.xmax() - box_2.xmin());
    // The IOU is a ratio of how much the boxes overlap vs their size outside the overlap.
    // Boxes that are similar will have a higher overlap threshold.
    return area_of_overlap / (box_1_area + box_2_area - area_of_overlap);
}

void AggregatorStage::nms(HailoROIPtr hailo_roi, const float iou_thr)
{
    // The network may propose multiple detections of similar size/score,
    // which are actually the same detection. We want to filter out the lesser
    // detections with a simple nms.

    std::vector<HailoDetectionPtr> objects = hailo_common::get_hailo_detections(hailo_roi);
    std::sort(objects.begin(), objects.end(),
              [](HailoDetectionPtr a, HailoDetectionPtr b) { return a->get_confidence() > b->get_confidence(); });

    for (uint index = 0; index < objects.size(); index++)
    {
        for (uint jindex = index + 1; jindex < objects.size(); jindex++)
        {
            if (objects[index]->get_class_id() == objects[jindex]->get_class_id())
            {
                // For each detection, calculate the IOU against each following detection.
                float iou = iou_calc(objects[index]->get_bbox(), objects[jindex]->get_bbox());
                // If the IOU is above threshold, then we have two similar detections,
                // and want to delete the one.
                if (iou >= iou_thr)
                {
                    // The detections are arranged in highest score order,
                    // so we want to erase the latter detection.
                    hailo_roi->remove_object(objects[jindex]);
                    objects.erase(objects.begin() + jindex);
                    jindex--; // Step back jindex since we just erased the current detection.
                }
            }
        }
    }
}

int AggregatorStage::count_subframes(BufferPtr main_buffer)
{
    int num_subframes = 0;
    if (m_static_sub_frames.has_value())
    {
        num_subframes = m_static_sub_frames.value();
    }

    std::vector<MetadataPtr> metadata = main_buffer->get_metadata_of_type(MetadataType::EXPECTED_CROPS);
    if (metadata.size() > 0)
    {
        CroppingMetadataPtr cropping_metadata = std::dynamic_pointer_cast<CroppingMetadata>(metadata[0]);

        if (!m_static_sub_frames.has_value())
        {
            num_subframes = cropping_metadata->get_num_crops();
        }

        // remove the meta since we finished using it, to avoid confusing future aggregators
        main_buffer->remove_metadata(cropping_metadata);
    }

    return num_subframes;
}

void AggregatorStage::stamp_and_send(BufferPtr buffer)
{
    send_to_subscribers(buffer);
    m_tracing->trace_processing_end();
}

void AggregatorStage::migrate_metadata(BufferPtr main_buffer, std::vector<BufferPtr> &subframes)
{
    if (m_skip_migration)
    {
        // If skip migration is set, we don't want to migrate metadata from subframes to main_buffer
        return;
    }

    // copy metadata from subframes to main frame
    for (size_t i = 0; i < subframes.size(); i++)
    {
        BufferPtr subframe = subframes[i];

        if (m_copy_sub_frame_tensor_to_metadata)
        {
            // Copy TENSOR metadata from subframes to main buffer (keeps AI buffers alive)
            std::vector<MetadataPtr> tensor_metadata = subframe->get_metadata_of_type(MetadataType::TENSOR);

            // Add all tensor metadata to keep all AI buffers alive until privacy mask processing is complete
            for (const auto &tensor_meta : tensor_metadata)
            {
                main_buffer->add_metadata(tensor_meta);
            }
        }

        if (m_multi_scale)
        {
            remove_exceeded_bboxes(subframe->get_roi(), m_border_threshold);
        }
        // Flatten subframe roi detections to main_buffer roi's scales.
        // Passing HAILO_DETECTION as a filter type here request to flatten only HailoDetection objects.
        // This passes ownership of the rois to the main buffer
        flatten_hailo_roi(subframe->get_roi(), main_buffer->get_roi(), HAILO_DETECTION);
    }

    if (m_multi_scale)
    {
        // Perform NMS on the main frame's detections after aggragation is done
        nms(main_buffer->get_roi(), m_iou_threshold);
    }
}

void AggregatorStage::loop()
{
    while (!m_end_of_stream)
    {
        // the first queue is the one that is condisidered the "main stream"
        BufferPtr main_buffer = m_queues[0]->pop();
        m_tracing->trace_processing_start();
        if (main_buffer == nullptr && m_end_of_stream)
        {
            break;
        }

        // Check if the main buffer has cropping metadata
        int num_subframes = count_subframes(main_buffer);
        // If no subframes are requested, send the main buffer as is
        if (num_subframes == 0)
        {
            stamp_and_send(main_buffer);
            continue;
        }

        // Check how many sub frames are available, wait if blocking is enabled
        auto subframes_result = get_subframes(main_buffer, num_subframes);
        if (!subframes_result.has_value())
        {
            switch (subframes_result.error())
            {
            case SubframeStatus::END_OF_STREAM:
                return;
            case SubframeStatus::TIMEOUT:
                stamp_and_send(main_buffer);
                continue;
            default:
                HAILO_ANALYTICS_LOG_ERROR("Failed to get subframes in stage {}", m_stage_name);
                return;
            }
        }

        // migrate metadata from subframes to main buffer
        migrate_metadata(main_buffer, subframes_result.value());

        // pass the main_buffer to the subscribers
        stamp_and_send(main_buffer);

        trace_fps();
    }
}

tl::expected<std::vector<BufferPtr>, SubframeStatus> AggregatorStage::get_subframes(BufferPtr main_buffer,
                                                                                    int num_subframes)
{
    (void)main_buffer;
    std::vector<BufferPtr> subframes;
    for (int i = 0; i < num_subframes; i++)
    {
        subframes.push_back(m_queues[1]->pop());
        if (subframes[i] == nullptr && m_end_of_stream)
        {
            m_tracing->trace_processing_end();
            return tl::make_unexpected(SubframeStatus::END_OF_STREAM);
        }
    }
    return subframes;
}

AppStatus AggregatorStage::deinit()
{
    for (auto &queue : m_queues)
    {
        queue->flush();
    }

    return AppStatus::SUCCESS;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_static_subframes_opt(int num)
{
    m_static_sub_frames = num;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_main_inlet_name(std::string name)
{
    m_main_inlet_name = name;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_main_queue_size(size_t size)
{
    m_main_queue_size = size;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_main_leaky(bool leaky)
{
    m_main_queue_leaky = leaky;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_sub_inlet_name(std::string name)
{
    m_sub_inlet_name = name;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_sub_queue_size(size_t size)
{
    m_sub_queue_size = size;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_sub_leaky(bool leaky)
{
    m_sub_queue_leaky = leaky;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_multiscale_opt(bool multi_scale)
{
    m_multi_scale = multi_scale;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_skip_migration_opt(bool skip)
{
    m_skip_migration = skip;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_iou_threshold_opt(float threshold)
{
    m_iou_threshold = threshold;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_border_threshold_opt(float threshold)
{
    m_border_threshold = threshold;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_copy_sub_frame_tensor_to_metadata_opt(bool copy)
{
    m_copy_sub_frame_tensor_to_metadata = copy;
    return *this;
}

std::shared_ptr<AggregatorStage> AggregatorStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING(m_main_inlet_name.has_value(), "set_main_inlet_name");
    THROW_IF_MISSING(m_sub_inlet_name.has_value(), "set_sub_inlet_name");

    return std::make_shared<AggregatorStage>(
        m_stage_name.value(), m_main_inlet_name.value(), m_main_queue_size, m_main_queue_leaky,
        m_sub_inlet_name.value(), m_sub_queue_size, m_sub_queue_leaky, m_multi_scale, m_iou_threshold,
        m_border_threshold, m_skip_migration, m_trace, m_static_sub_frames, m_copy_sub_frame_tensor_to_metadata);
}

AggregatorStageBuild::Builder AggregatorStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::cropping
