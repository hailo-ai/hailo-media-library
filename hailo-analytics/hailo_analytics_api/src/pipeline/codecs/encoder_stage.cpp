#include <stddef.h>
#include <media_library/buffer_pool.hpp>
#include <media_library/media_library.hpp>
#include <media_library/media_library_api_types.hpp>
#include <hailort.h>
#include <stdint.h>
#include <hailo_gst_tensor_metadata.hpp>
#include <media_library/media_library_buffer.hpp>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/codecs/encoder_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "media_library/analytics_metadata.hpp"

namespace hailo_analytics::pipeline::codecs
{

namespace
{

LabeledSemanticMask make_labeled_mask(const HailoClassMaskPtr &hailo_mask, const HailoBBox &detection_bbox,
                                      uint32_t ai_width, uint32_t ai_height, uint16_t class_id,
                                      const std::string &label)
{
    LabeledSemanticMask labeled{};
    labeled.mask.class_id = class_id;
    labeled.mask.width = static_cast<uint32_t>(hailo_mask->get_width());
    labeled.mask.height = static_cast<uint32_t>(hailo_mask->get_height());
    labeled.mask.transparency = hailo_mask->get_transparency();
    labeled.mask.mask_size = hailo_mask->get_width() * hailo_mask->get_height();

    labeled.mask.detection_x = detection_bbox.xmin() * ai_width;
    labeled.mask.detection_y = detection_bbox.ymin() * ai_height;
    labeled.mask.detection_width = detection_bbox.width() * ai_width;
    labeled.mask.detection_height = detection_bbox.height() * ai_height;

    labeled.mask.mask = hailo_mask->get_data();

    labeled.label = label;
    return labeled;
}

std::optional<std::vector<LabeledSemanticMask>> build_semantic_segmentation(const HailoROIPtr &roi, uint32_t ai_width,
                                                                            uint32_t ai_height)
{
    std::vector<LabeledSemanticMask> masks;
    for (auto detection : hailo_common::get_hailo_detections(roi))
    {
        const auto detection_bbox = detection->get_bbox();
        const auto detection_label = detection->get_label();
        uint16_t child_index = 0;
        for (const auto &nested : detection->get_objects())
        {
            if (nested->get_type() != HAILO_CLASS_MASK)
                continue;
            auto hailo_mask = std::dynamic_pointer_cast<HailoClassMask>(nested);
            if (!hailo_mask)
            {
                ++child_index;
                continue;
            }
            masks.push_back(make_labeled_mask(hailo_mask, detection_bbox, ai_width, ai_height,
                                              /*class_id=*/child_index, /*label=*/detection_label));
            ++child_index;
        }
    }

    if (masks.empty())
        return std::nullopt;

    return masks;
}

std::optional<std::vector<LabeledDetection>> build_overflow_detections(const HailoROIPtr &roi, uint32_t ai_width,
                                                                       uint32_t ai_height)
{
    std::vector<LabeledDetection> overflow;
    for (auto detection : hailo_common::get_hailo_detections(roi))
    {
        bool has_mask = false;
        for (const auto &nested : detection->get_objects())
        {
            if (nested->get_type() == HAILO_CLASS_MASK)
            {
                has_mask = true;
                break;
            }
        }
        if (has_mask)
            continue;

        HailoBBox bbox = detection->get_bbox();
        LabeledDetection labeled{};
        labeled.detection.score = detection->get_confidence();
        labeled.detection.class_id = static_cast<uint16_t>(detection->get_class_id());
        labeled.detection.x_min = bbox.xmin() * static_cast<float32_t>(ai_width);
        labeled.detection.y_min = bbox.ymin() * static_cast<float32_t>(ai_height);
        labeled.detection.x_max = bbox.xmax() * static_cast<float32_t>(ai_width);
        labeled.detection.y_max = bbox.ymax() * static_cast<float32_t>(ai_height);
        labeled.label = detection->get_label();
        overflow.push_back(std::move(labeled));
    }

    if (overflow.empty())
        return std::nullopt;

    return overflow;
}

} // namespace

EncoderStage::EncoderStage(std::string name, size_t queue_size, bool leaky, bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations)
{
}

AppStatus EncoderStage::create(MediaLibraryInterfacePtr media_library, const output_stream_id_t &stream_id)
{
    m_media_library = media_library;
    m_stream_id = stream_id;
    auto status = m_media_library->subscribe_to_encoder_output(
        m_stream_id, [this](HailoMediaLibraryBufferPtr buffer, size_t size) {
            hailo_analytics::pipeline::BufferPtr wrapped_buffer =
                std::make_shared<hailo_analytics::pipeline::Buffer>(buffer);
            SizeMetadataPtr size_meta = std::make_shared<SizeMetadata>(this->m_stage_name, size);
            wrapped_buffer->add_metadata(size_meta);
            this->send_to_subscribers(wrapped_buffer);
        });
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to subscribe to encoder output for stream '{}'", stream_id);
        m_media_library.reset();
        return AppStatus::INVALID_ARGUMENT;
    }
    return AppStatus::SUCCESS;
}

AppStatus EncoderStage::init()
{
    if (!m_media_library)
    {
        HAILO_ANALYTICS_LOG_ERROR("Encoder {} not configured. Call configure()", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }
    return AppStatus::SUCCESS;
}

AppStatus EncoderStage::deinit()
{
    if (m_media_library)
    {
        m_media_library->unsubscribe_from_encoder_output(m_stream_id);
        m_media_library.reset();
    }
    return AppStatus::SUCCESS;
}

AppStatus EncoderStage::configure(MediaLibraryInterfacePtr media_library, const output_stream_id_t &stream_id)
{
    if (m_media_library)
    {
        m_media_library.reset();
    }
    return create(media_library, stream_id);
}

void EncoderStage::set_attach_analytics_metadata(bool enabled)
{
    m_attach_analytics_metadata = enabled;
}

void EncoderStage::attach_dpm_metadata(hailo_analytics::pipeline::BufferPtr data)
{
    auto roi = data->get_roi();
    auto mlib_buf = data->get_buffer();
    if (!roi || !mlib_buf || !mlib_buf->buffer_data)
        return;

    const uint32_t frame_width = static_cast<uint32_t>(mlib_buf->buffer_data->width);
    const uint32_t frame_height = static_cast<uint32_t>(mlib_buf->buffer_data->height);

    auto semantic = build_semantic_segmentation(roi, frame_width, frame_height);
    auto detections = build_overflow_detections(roi, frame_width, frame_height);

    // Always attach, even empty — null downstream trips the DB-fallback 10s/frame timeout.
    auto carrier = std::make_shared<AnalyticsMetadata>();
    if (semantic.has_value())
    {
        carrier->m_semantic_segmentation = std::make_shared<std::vector<LabeledSemanticMask>>(std::move(*semantic));
        for (auto &tensor_metadata : data->get_metadata_of_type(MetadataType::TENSOR))
        {
            carrier->m_source_keepalives.push_back(std::shared_ptr<void>(std::move(tensor_metadata)));
        }
    }
    if (detections.has_value())
        carrier->m_detections = std::make_shared<std::vector<LabeledDetection>>(std::move(*detections));
    mlib_buf->m_analytics_metadata = std::move(carrier);
}

AppStatus EncoderStage::process(hailo_analytics::pipeline::BufferPtr data)
{
    if (m_attach_analytics_metadata)
        attach_dpm_metadata(data);

    if (!m_media_library)
    {
        // Conversion-only: forward downstream. Wired encoders emit via the create() callback.
        send_to_subscribers(data);
        return AppStatus::SUCCESS;
    }

    m_media_library->add_buffer_to_encoder(m_stream_id, data->get_buffer());
    return AppStatus::SUCCESS;
}

EncoderStageBuild::Builder &EncoderStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}
EncoderStageBuild::Builder &EncoderStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}
EncoderStageBuild::Builder &EncoderStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}
EncoderStageBuild::Builder &EncoderStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

EncoderStageBuild::Builder &EncoderStageBuild::Builder::set_attach_analytics_metadata(bool enabled)
{
    m_attach_analytics_metadata = enabled;
    return *this;
}

std::shared_ptr<EncoderStage> EncoderStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    auto stage = std::make_shared<EncoderStage>(m_stage_name.value(), m_queue_size, m_leaky, m_trace);
    // Always propagate — stage default true, a guard here would silently drop set(false).
    stage->set_attach_analytics_metadata(m_attach_analytics_metadata);
    return stage;
}

EncoderStageBuild::Builder EncoderStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::codecs
