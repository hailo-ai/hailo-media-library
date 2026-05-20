#include <hailo/hailodsp.h>
#include <hailo/hailodsp_base.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <tl/expected.hpp>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <utility>

#include "privacy_mask.hpp"
#include "analytics_metadata.hpp"
#include "buffer_pool.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "polygon_math.hpp"
#include "privacy_mask_dynamic.hpp" // shared scale_detection_coordinates
#include "privacy_mask_types.hpp"
#include "media_library_buffer.hpp"
#include "dsp_utils.hpp"
#include "encoder_config_types.hpp"

#define MODULE_NAME LoggerType::PrivacyMask

size_t PrivacyMask::get_adjusted_frame_width(size_t width)
{
    int line_division = 8 / PRIVACY_MASK_QUANTIZATION;
    return ((width + (line_division - 1)) & ~(line_division - 1)) / line_division;
}

size_t PrivacyMask::get_adjusted_frame_height(size_t height)
{
    return height / 4;
}

bool PrivacyMask::should_adjust_buffer_pool(HailoMediaLibraryBufferPtr input_buffer)
{
    const size_t adjusted_width = get_adjusted_frame_width(input_buffer->buffer_data->width);
    const size_t adjusted_height = get_adjusted_frame_height(input_buffer->buffer_data->height);

    return (m_buffer_pool == nullptr) || (m_buffer_pool->get_width() != adjusted_width) ||
           (m_buffer_pool->get_height() != adjusted_height);
}

media_library_return PrivacyMask::adjust_buffer_pool(HailoMediaLibraryBufferPtr input_buffer)
{
    // Round up m_frame_width to be a multiple of byte_size / PRIVACY_MASK_QUANTIZATION (32)
    const size_t adjusted_width = get_adjusted_frame_width(input_buffer->buffer_data->width);
    const size_t adjusted_height = get_adjusted_frame_height(input_buffer->buffer_data->height);

    // Round bytes_per_line to be a multiple of byte_size (8)
    const uint bytes_per_line = (adjusted_width + 7) & ~7;

    std::string name = m_stream_id + "_privacy_mask";
    // TODO: set pool size
    m_buffer_pool = std::make_shared<MediaLibraryBufferPool>(adjusted_width, adjusted_height, HAILO_FORMAT_GRAY8, 1,
                                                             HAILO_MEMORY_TYPE_DMABUF, bytes_per_line, name);
    if (m_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to initialize buffer pool");
        return media_library_return::MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    LOGGER__MODULE__INFO(MODULE_NAME,
                         "Buffer pool initialized successfully with frame size {}x{} "
                         "bytes_per_line {}",
                         adjusted_width, adjusted_height, bytes_per_line);
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMask::blend(HailoMediaLibraryBufferPtr input_buffer)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Blending privacy mask");

    auto &encoded_streams = input_buffer->get_attached_profile()->encoded_output_streams;
    auto stream_it = encoded_streams.find(m_stream_id);
    if (stream_it == encoded_streams.end())
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Stream id {} not found in profile during blend, skipping frame",
                             m_stream_id);
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }
    auto &input_frame_encoded_stream_config = stream_it->second;

    if (should_adjust_buffer_pool(input_buffer))
    {
        media_library_return ret = adjust_buffer_pool(input_buffer);
        if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to adjust buffer pool");
            return ret;
        }
    }

    std::chrono::time_point<std::chrono::steady_clock> start_blend = std::chrono::steady_clock::now();

    auto updated_masks_expected = get_updated_privacy_masks(input_frame_encoded_stream_config, input_buffer);
    if (!updated_masks_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to blend privacy mask");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    PrivacyMasksPtr privacy_mask_data = updated_masks_expected.value();

    // Prepare the static privacy mask parameters
    std::optional<dsp_static_privacy_mask_t> static_privacy_mask = std::nullopt;
    std::vector<dsp_roi_t> dsp_rois;
    if (privacy_mask_data->static_data.rois_count != 0)
    {
        static_privacy_mask.emplace();
        dsp_rois.resize(privacy_mask_data->static_data.rois_count);

        static_privacy_mask->bitmask = (uint8_t *)privacy_mask_data->static_data.bitmask->get_plane_ptr(0);
        if (privacy_mask_data->info.type == PrivacyMaskType::COLOR)
        {
            static_privacy_mask->color.y = privacy_mask_data->info.color.y;
            static_privacy_mask->color.u = privacy_mask_data->info.color.u;
            static_privacy_mask->color.v = privacy_mask_data->info.color.v;
            static_privacy_mask->type = DSP_PRIVACY_MASK_COLOR;
        }
        else
        {
            static_privacy_mask->type = DSP_PRIVACY_MASK_BLUR;
            static_privacy_mask->blur_radius = privacy_mask_data->info.pixelization_size;
        }
        static_privacy_mask->rois = dsp_rois.data();
        static_privacy_mask->rois_count = privacy_mask_data->static_data.rois_count;

        for (uint i = 0; i < privacy_mask_data->static_data.rois_count; i++)
        {
            dsp_rois[i] = {
                .start_x = privacy_mask_data->static_data.rois[i].x,
                .start_y = privacy_mask_data->static_data.rois[i].y,
                .end_x = privacy_mask_data->static_data.rois[i].x + privacy_mask_data->static_data.rois[i].width,
                .end_y = privacy_mask_data->static_data.rois[i].y + privacy_mask_data->static_data.rois[i].height};
        }
    }

    // Assemble the unified privacy mask parameters
    unified_dsp_privacy_mask_t privacy_mask_params;

    if (privacy_mask_data->info.type == PrivacyMaskType::COLOR)
    {
        privacy_mask_params.color.y = privacy_mask_data->info.color.y;
        privacy_mask_params.color.u = privacy_mask_data->info.color.u;
        privacy_mask_params.color.v = privacy_mask_data->info.color.v;
        privacy_mask_params.type = DSP_PRIVACY_MASK_COLOR;
    }
    else
    {
        privacy_mask_params.type = DSP_PRIVACY_MASK_BLUR;
        privacy_mask_params.pixelization_size = privacy_mask_data->info.pixelization_size;
    }
    privacy_mask_params.static_privacy_mask_params = static_privacy_mask ? &static_privacy_mask.value() : nullptr;
    privacy_mask_params.dynamic_privacy_mask_params =
        (0 < privacy_mask_data->dynamic_data.dynamic_mask_group.masks_count)
            ? &privacy_mask_data->dynamic_data.dynamic_mask_group
            : nullptr;
    LOGGER__MODULE__TRACE(MODULE_NAME, "Blending {} static masks and {} dynamic masks",
                          privacy_mask_data->static_data.rois_count,
                          privacy_mask_data->dynamic_data.dynamic_mask_group.masks_count);
    dsp_status status = dsp_utils::perform_dsp_privacy_mask(input_buffer->buffer_data.get(), &privacy_mask_params);

    if (input_buffer->m_analytics_metadata)
        input_buffer->m_analytics_metadata->m_source_keepalives.clear();

    if (status != DSP_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "DSP privacy mask blend failed with {}", status);
        return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
    }

    std::chrono::time_point<std::chrono::steady_clock> end_blend = std::chrono::steady_clock::now();
    std::chrono::milliseconds blend_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_blend - start_blend);
    LOGGER__MODULE__TRACE(MODULE_NAME, "Blending privacy masks took {} milliseconds ({} fps)", blend_time.count(),
                          (1000 / blend_time.count()));

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

static privacy_mask_types::yuv_color_t rgb_to_yuv(const rgb_color_t &rgb_color)
{
    privacy_mask_types::yuv_color_t yuv_color;
    yuv_color.y = 0.257 * rgb_color.r + 0.504 * rgb_color.g + 0.098 * rgb_color.b + 16;
    yuv_color.u = -0.148 * rgb_color.r - 0.291 * rgb_color.g + 0.439 * rgb_color.b + 128;
    yuv_color.v = 0.439 * rgb_color.r - 0.368 * rgb_color.g - 0.071 * rgb_color.b + 128;
    return yuv_color;
}

media_library_return PrivacyMask::update_info(const config_encoded_output_stream_t &encoded_output_streams_config)
{

    auto &masking_config = encoded_output_streams_config.masking;
    switch (masking_config.mask_type)
    {
    case PrivacyMaskType::COLOR:
        m_latest_privacy_masks->info.color = rgb_to_yuv(masking_config.color_value);
        m_latest_privacy_masks->info.type = PrivacyMaskType::COLOR;
        break;
    case PrivacyMaskType::PIXELIZATION:
        m_latest_privacy_masks->info.pixelization_size = masking_config.pixelization_size;
        m_latest_privacy_masks->info.type = PrivacyMaskType::PIXELIZATION;
        break;
    default:
        LOGGER__MODULE__ERROR(MODULE_NAME, "Invalid privacy mask type");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMask::update_static_mask(
    const config_encoded_output_stream_t &encoded_output_streams_config)
{
    auto &masking_config = encoded_output_streams_config.masking;

    if (!masking_config.static_privacy_mask_config.has_value() || !masking_config.static_privacy_mask_config->enabled ||
        masking_config.static_privacy_mask_config->masks.empty())
    {
        m_latest_privacy_masks->static_data.rois_count = 0;
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    m_latest_privacy_masks->static_data = {};
    if (m_buffer_pool == NULL)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Buffer pool is uninitialized");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    if (m_buffer_pool->acquire_buffer(m_latest_privacy_masks->static_data.bitmask) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to acquire buffer");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    size_t input_width, input_height;
    if (const hailo_encoder_config_t *encoder_config =
            std::get_if<hailo_encoder_config_t>(&encoded_output_streams_config.encoding);
        encoder_config != nullptr)
    {
        input_width = encoder_config->input_stream.width;
        input_height = encoder_config->input_stream.height;
    }
    else if (const jpeg_encoder_config_t *jpeg_encoder_config =
                 std::get_if<jpeg_encoder_config_t>(&encoded_output_streams_config.encoding);
             jpeg_encoder_config != nullptr)
    {
        input_width = jpeg_encoder_config->input_stream.width;
        input_height = jpeg_encoder_config->input_stream.height;
    }
    else
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Unsupported encoder config type");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    m_latest_privacy_masks->static_data.bitmask->sync_start();
    if (write_polygons_to_privacy_mask_data(masking_config.static_privacy_mask_config->masks, input_width, input_height,
                                            m_latest_privacy_masks->static_data) !=
        media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to write polygon");
        m_latest_privacy_masks->static_data.bitmask->sync_end();
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    m_latest_privacy_masks->static_data.bitmask->sync_end();
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMask::update_dynamic_mask(
    const config_encoded_output_stream_t &encoded_output_streams_config, HailoMediaLibraryBufferPtr input_buffer)
{
    auto &dynamic_mask_config = encoded_output_streams_config.masking.dynamic_privacy_mask_config;

    m_latest_privacy_masks->dynamic_data = {};
    m_dynamic_masks_rois.clear();

    if (!dynamic_mask_config.has_value() || !dynamic_mask_config->enabled)
    {
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    if (input_buffer && input_buffer->m_analytics_metadata)
        return update_dynamic_mask_from_buffer(encoded_output_streams_config, input_buffer);
    return update_dynamic_mask_from_db(encoded_output_streams_config, input_buffer);
}

media_library_return PrivacyMask::update_dynamic_mask_from_buffer(
    const config_encoded_output_stream_t &encoded_output_streams_config, HailoMediaLibraryBufferPtr input_buffer)
{
    auto &dynamic_mask_config = encoded_output_streams_config.masking.dynamic_privacy_mask_config;

    if (!input_buffer || !input_buffer->m_analytics_metadata)
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "No analytics metadata on buffer — skipping dynamic mask");
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    LOGGER__MODULE__TRACE(MODULE_NAME, "Updating dynamic mask from buffer-attached metadata");

    const auto &masked_labels = dynamic_mask_config->masked_labels;
    const auto &label_to_class_id = dynamic_mask_config->label_to_class_id;
    const size_t dilation_size = dynamic_mask_config->dilation_size;
    const auto &md = *input_buffer->m_analytics_metadata;

    // Same dims the producer scaled with — DSP's network_frame_* and detection_* must agree.
    const size_t frame_width = input_buffer->buffer_data ? input_buffer->buffer_data->width : 0;
    const size_t frame_height = input_buffer->buffer_data ? input_buffer->buffer_data->height : 0;
    if (frame_width == 0 || frame_height == 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME,
                              "Encoded-frame dimensions unavailable on input buffer — can't overlay masks");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    if (md.m_semantic_segmentation)
        privacy_mask::dynamic::append_rois_from_buffer_semantic_segmentation(
            *md.m_semantic_segmentation, masked_labels, label_to_class_id, dilation_size, frame_width, frame_height,
            m_dynamic_masks_rois);
    if (md.m_detections)
        privacy_mask::dynamic::append_rois_from_buffer_detections(*md.m_detections, masked_labels, dilation_size,
                                                                  frame_width, frame_height, m_dynamic_masks_rois);

    auto &dynamic_mask_group_dsp_params = m_latest_privacy_masks->dynamic_data.dynamic_mask_group;
    dynamic_mask_group_dsp_params.masks = m_dynamic_masks_rois.data();
    dynamic_mask_group_dsp_params.masks_count = m_dynamic_masks_rois.size();
    // DSP unwraps the +2*mask_size letterbox using this; non-square frame needs real aspect.
    dynamic_mask_group_dsp_params.original_aspect_ratio =
        static_cast<float>(frame_width) / static_cast<float>(frame_height);
    dynamic_mask_group_dsp_params.scaling_mode = DSP_SCALING_MODE_STRETCH;
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

tl::expected<PrivacyMasksPtr, media_library_return> PrivacyMask::get_updated_privacy_masks(
    const config_encoded_output_stream_t &encoded_stream_config, HailoMediaLibraryBufferPtr input_buffer)
{

    if (m_latest_privacy_masks == NULL)
    {
        m_latest_privacy_masks = std::make_shared<privacy_masks_t>();
    }

    if (update_dynamic_mask(encoded_stream_config, input_buffer) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }

    if (encoded_stream_config.masking == m_masking_config)
    {
        return m_latest_privacy_masks;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Updating privacy masks");

    if (update_info(encoded_stream_config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }

    if (update_static_mask(encoded_stream_config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }

    m_masking_config = encoded_stream_config.masking;
    return m_latest_privacy_masks;
}

tl::expected<PrivacyMaskPtr, media_library_return> PrivacyMask::create()
{
    PrivacyMaskPtr privacy_mask_blender_ptr = std::make_shared<PrivacyMask>();

    dsp_status dsp_ret = dsp_utils::acquire_device();
    if (dsp_ret != DSP_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to acquire DSP device, status: {}", dsp_ret);
        return tl::make_unexpected(MEDIA_LIBRARY_OUT_OF_RESOURCES);
    }

    return privacy_mask_blender_ptr;
}

void PrivacyMask::set_stream_id(const std::string &stream_id)
{
    m_stream_id = stream_id;
}

PrivacyMask::PrivacyMask() : m_buffer_pool(nullptr)
{
}

PrivacyMask::~PrivacyMask()
{
    dsp_status status = dsp_utils::release_device();
    if (status != DSP_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to release DSP device, status: {}", status);
    }
}
