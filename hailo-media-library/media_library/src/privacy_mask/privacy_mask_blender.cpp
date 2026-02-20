#include "privacy_mask.hpp"

#include <chrono>

#include "analytics_db.hpp"
#include "buffer_pool.hpp"
#include "encoder_config_types.hpp"
#include "logger_macros.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "polygon_math.hpp"
#include "privacy_mask_types.hpp"

#define MODULE_NAME LoggerType::PrivacyMask

static void scale_detection_coordinates(float detection_roi_x1, float detection_roi_y1, float detection_roi_x2,
                                        float detection_roi_y2, size_t &scaled_x1, size_t &scaled_y1, size_t &scaled_x2,
                                        size_t &scaled_y2, size_t &network_width, size_t &network_height,
                                        size_t network_frame_width, size_t network_frame_height)
{
    size_t detection_roi_width = static_cast<size_t>(detection_roi_x2 - detection_roi_x1);
    size_t detection_roi_height = static_cast<size_t>(detection_roi_y2 - detection_roi_y1);
    float max_dimension = (std::max)(static_cast<float>(detection_roi_width), static_cast<float>(detection_roi_height));
    float scale = MASK_SIZE / max_dimension;

    float scaled_width = static_cast<float>(detection_roi_width) * scale;
    float scaled_height = static_cast<float>(detection_roi_height) * scale;

    // Calculate padding within the mask (letterbox padding on each side)
    float pad_x = (static_cast<float>(MASK_SIZE) - scaled_width) / 2.0f;
    float pad_y = (static_cast<float>(MASK_SIZE) - scaled_height) / 2.0f;

    float adjusted_roi_x1 = detection_roi_x1 * scale - pad_x;
    float adjusted_roi_y1 = detection_roi_y1 * scale - pad_y;

    // Scale frame by the same factors
    float scaled_frame_width = static_cast<float>(network_frame_width) * scale;
    float scaled_frame_height = static_cast<float>(network_frame_height) * scale;

    if (max_dimension == detection_roi_height)
    {
        scaled_frame_width += 2 * MASK_SIZE;
        adjusted_roi_x1 += MASK_SIZE;
    }
    else if (max_dimension == detection_roi_width)
    {
        scaled_frame_height += 2 * MASK_SIZE;
        adjusted_roi_y1 += MASK_SIZE;
    }

    scaled_x1 = static_cast<size_t>(adjusted_roi_x1);
    scaled_y1 = static_cast<size_t>(adjusted_roi_y1);
    scaled_x2 = scaled_x1 + MASK_SIZE;
    scaled_y2 = scaled_y1 + MASK_SIZE;

    network_width = static_cast<size_t>(scaled_frame_width);
    network_height = static_cast<size_t>(scaled_frame_height);
    if (scaled_x2 > network_width)
    {
        network_width = scaled_x2;
    }
    if (scaled_y2 > network_height)
    {
        network_height = scaled_y2;
    }
    LOGGER__MODULE__TRACE(MODULE_NAME,
                          "Scaled coordinates: x1={}, y1={}, x2={}, y2={}, network_width={}, network_height={}",
                          scaled_x1, scaled_y1, scaled_x2, scaled_y2, network_width, network_height);
}

static dsp_letterbox_alignment_t scaling_mode_to_dsp_letterbox(ScalingMode scaling_mode)
{
    switch (scaling_mode)
    {
    case ScalingMode::STRETCH:
        return DSP_NO_LETTERBOX;
    case ScalingMode::LETTERBOX_MIDDLE:
        return DSP_LETTERBOX_MIDDLE;
    case ScalingMode::LETTERBOX_UP_LEFT:
        return DSP_LETTERBOX_UP_LEFT;
    default:
        return DSP_NO_LETTERBOX;
    }
}

size_t PrivacyMask::get_adjusted_frame_width(size_t width)
{
    // Round up m_frame_width to be a multiple of byte_size / PRIVACY_MASK_QUANTIZATION (32)
    // TODO explain
    int line_division = 8 / PRIVACY_MASK_QUANTIZATION;
    return ((width + (line_division - 1)) & ~(line_division - 1)) / line_division;
}

size_t PrivacyMask::get_adjusted_frame_height(size_t height)
{
    // TODO explain
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

    auto &input_frame_encoded_stream_config =
        input_buffer->get_attached_profile()->encoded_output_streams.at(m_stream_id);

    auto updated_masks_expected =
        get_updated_privacy_masks(input_frame_encoded_stream_config, input_buffer->isp_timestamp_ns);
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
        input_width = encoder_config->input_stream.width;
        input_height = encoder_config->input_stream.height;
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
    const config_encoded_output_stream_t &encoded_output_streams_config, uint64_t isp_timestamp_ns)
{
    auto &dynamic_mask_config = encoded_output_streams_config.masking.dynamic_privacy_mask_config;
    if (!dynamic_mask_config.has_value() || !dynamic_mask_config->enabled)
    {
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }
    LOGGER__MODULE__TRACE(MODULE_NAME, "Updating dynamic mask");

    m_latest_privacy_masks->dynamic_data = {};
    m_dynamic_masks_rois.clear();

    auto &db = AnalyticsDB::instance();
    std::chrono::nanoseconds isp_timestamp(isp_timestamp_ns);
    std::chrono::time_point<std::chrono::steady_clock> isp_timestamp_tp(isp_timestamp);

    AnalyticsQueryOptions opts{
        .m_type = AnalyticsQueryType::Exact, .m_ts = isp_timestamp_tp, .m_timeout = std::chrono::milliseconds(10000)};

    auto analytics_config = db.get_application_analytics_config();
    const size_t dilation_size = dynamic_mask_config->dilation_size;
    // Process each analytics entry
    for (const auto &entry : dynamic_mask_config->analytics)
    {
        const std::string &analytics_data_id = entry.analytics_data_id;
        const std::vector<std::string> &masked_labels = entry.masked_labels;

        // Determine analytics type by checking which config contains this analytics_data_id
        bool found_config = false;
        AnalyticsType analytics_type;

        if (analytics_config.instance_segmentation_analytics_config.find(analytics_data_id) !=
            analytics_config.instance_segmentation_analytics_config.end())
        {
            analytics_type = AnalyticsType::INSTANCE_SEGMENTATION;
            found_config = true;
            LOGGER__MODULE__TRACE(MODULE_NAME, "Using instance segmentation analytics for ID: {}", analytics_data_id);
        }
        else if (analytics_config.semantic_segmentation_analytics_config.find(analytics_data_id) !=
                 analytics_config.semantic_segmentation_analytics_config.end())
        {
            analytics_type = AnalyticsType::SEMANTIC_SEGMENTATION;
            found_config = true;
            LOGGER__MODULE__TRACE(MODULE_NAME, "Using semantic segmentation analytics for ID: {}", analytics_data_id);
        }
        else if (analytics_config.detection_analytics_config.find(analytics_data_id) !=
                 analytics_config.detection_analytics_config.end())
        {
            analytics_type = AnalyticsType::DETECTION;
            found_config = true;
            LOGGER__MODULE__TRACE(MODULE_NAME, "Using detection analytics for ID: {}", analytics_data_id);
        }

        if (!found_config)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Analytics config for ID {} not found in any analytics type",
                                  analytics_data_id);
            continue; // Skip this entry and continue with others
        }

        // Process based on analytics type
        media_library_return result;
        switch (analytics_type)
        {
        case AnalyticsType::INSTANCE_SEGMENTATION:
            result = process_instance_segmentation_masks(opts, analytics_config, analytics_data_id, masked_labels,
                                                         dilation_size);
            break;

        case AnalyticsType::SEMANTIC_SEGMENTATION:
            result = process_semantic_segmentation_masks(opts, analytics_config, analytics_data_id, masked_labels,
                                                         dilation_size);
            break;

        case AnalyticsType::DETECTION:
            result = process_detection_masks(opts, analytics_config, analytics_data_id, masked_labels, dilation_size);
            break;

        default:
            LOGGER__MODULE__ERROR(MODULE_NAME, "Unsupported analytics type for dynamic privacy mask");
            continue;
        }

        if (result != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to process analytics for ID: {}", analytics_data_id);
            // Continue processing other analytics entries even if one fails
        }
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

tl::expected<PrivacyMasksPtr, media_library_return> PrivacyMask::get_updated_privacy_masks(
    const config_encoded_output_stream_t &encoded_stream_config, uint64_t isp_timestamp_ns)
{

    if (m_latest_privacy_masks == NULL)
    {
        m_latest_privacy_masks = std::make_shared<privacy_masks_t>();
    }

    // updated based on analytics data and not only config
    if (update_dynamic_mask(encoded_stream_config, isp_timestamp_ns) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }

    if (encoded_stream_config.masking == m_masking_config)
    {
        return m_latest_privacy_masks;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Updating privacy masks");

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

media_library_return PrivacyMask::process_instance_segmentation_masks(
    const AnalyticsQueryOptions &opts, const application_analytics_config_t &analytics_config,
    const std::string &analytics_data_id, const std::vector<std::string> &masked_labels, const size_t dilation_size)
{
    auto &db = AnalyticsDB::instance();
    auto closet_instance_segmentation_entry_expected = db.query_instance_segmentation_entry(analytics_data_id, opts);
    if (!closet_instance_segmentation_entry_expected.has_value())
    {
        // No analytics data available - this is normal when there are no detections
        LOGGER__MODULE__TRACE(MODULE_NAME, "No instance segmentation entry found in DB - no detections to mask");
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }
    auto closet_instance_segmentation_entry = closet_instance_segmentation_entry_expected.value();
    auto delta_ns = std::abs((closet_instance_segmentation_entry.ts - opts.m_ts).count());
    LOGGER__MODULE__TRACE(MODULE_NAME, "Instance segmentation entry found with delta: {} ns ({} ms)", delta_ns,
                          delta_ns / 1000000);
    for (const auto &segmentation_data : closet_instance_segmentation_entry.analytics_buffer)
    {
        if (m_dynamic_masks_rois.size() >= MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS)
        {
            LOGGER__MODULE__WARNING(MODULE_NAME,
                                    "Reached MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS ({}), skipping remaining ROIs.",
                                    MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS);
            break;
        }

        // Verify that the dynamic analytics data ID exists in the config
        if (analytics_config.instance_segmentation_analytics_config.find(analytics_data_id) ==
            analytics_config.instance_segmentation_analytics_config.end())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Analytics config for ID {} not found", analytics_data_id);
            return media_library_return::MEDIA_LIBRARY_ERROR;
        }

        if (std::find_if(analytics_config.instance_segmentation_analytics_config.at(analytics_data_id).labels.begin(),
                         analytics_config.instance_segmentation_analytics_config.at(analytics_data_id).labels.end(),
                         [&](const label_t &label) { return label.id == segmentation_data.class_id; }) ==
            analytics_config.instance_segmentation_analytics_config.at(analytics_data_id).labels.end())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Skipping segmentation data for unknown class_id {}",
                                  segmentation_data.class_id);
            continue;
        }

        // Find the label name for this class_id
        auto &labels = analytics_config.instance_segmentation_analytics_config.at(analytics_data_id).labels;
        auto label_it = std::find_if(labels.begin(), labels.end(),
                                     [&](const label_t &label) { return label.id == segmentation_data.class_id; });

        if (label_it == labels.end())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Skipping segmentation data for unknown class_id {}",
                                  segmentation_data.class_id);
            continue;
        }

        // Check if the label name is in the masked labels
        if (std::find(masked_labels.begin(), masked_labels.end(), label_it->label) == masked_labels.end())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME,
                                  "Skipping segmentation data for label '{}' (class_id {}) not in masked labels",
                                  label_it->label, segmentation_data.class_id);
            continue;
        }

        // Execute the dynamic mask
        auto input_frame_net_width =
            analytics_config.instance_segmentation_analytics_config.at(analytics_data_id).width;
        auto input_frame_net_height =
            analytics_config.instance_segmentation_analytics_config.at(analytics_data_id).height;
        auto scaling_mode = analytics_config.instance_segmentation_analytics_config.at(analytics_data_id).scaling_mode;
        LOGGER__MODULE__TRACE(
            MODULE_NAME,
            "Processing segmentation data for class_id {}, box: ({}, {}), ({}, {}), "
            "input_frame_net_width: {}, input_frame_net_height: {}, scaling_mode: {}, mask_size: {}, ",
            segmentation_data.class_id, segmentation_data.box.x_min, segmentation_data.box.y_min,
            segmentation_data.box.x_max, segmentation_data.box.y_max, input_frame_net_width, input_frame_net_height,
            static_cast<int>(scaling_mode), segmentation_data.mask_size);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        m_dynamic_masks_rois.push_back(dsp_dynamic_privacy_mask_roi_t{
            .bytemask = segmentation_data.mask,
            .input_frame_net_width = input_frame_net_width,
            .input_frame_net_height = input_frame_net_height,
            .letterbox = scaling_mode_to_dsp_letterbox(scaling_mode),
            .roi =
                {
                    .start_x = static_cast<size_t>(segmentation_data.box.x_min),
                    .start_y = static_cast<size_t>(segmentation_data.box.y_min),
                    .end_x = static_cast<size_t>(segmentation_data.box.x_max),
                    .end_y = static_cast<size_t>(segmentation_data.box.y_max),
                },
            .dilation_size = dilation_size,
        });
#pragma GCC diagnostic pop
    }

    m_latest_privacy_masks->dynamic_data.dynamic_mask_group.masks = m_dynamic_masks_rois.data();
    m_latest_privacy_masks->dynamic_data.dynamic_mask_group.masks_count = m_dynamic_masks_rois.size();

    auto original_width_ratio =
        analytics_config.instance_segmentation_analytics_config.at(analytics_data_id).original_width_ratio;
    auto original_height_ratio =
        analytics_config.instance_segmentation_analytics_config.at(analytics_data_id).original_height_ratio;
    m_latest_privacy_masks->dynamic_data.dynamic_mask_group.original_aspect_ratio =
        static_cast<float>(original_width_ratio) / original_height_ratio;

    // TODO : add support for aspect ratio preservation
    m_latest_privacy_masks->dynamic_data.dynamic_mask_group.scaling_mode = DSP_SCALING_MODE_STRETCH;

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMask::process_semantic_segmentation_masks(
    const AnalyticsQueryOptions &opts, const application_analytics_config_t &analytics_config,
    const std::string &analytics_data_id, const std::vector<std::string> &masked_labels, const size_t dilation_size)
{
    auto &db = AnalyticsDB::instance();
    auto closest_semantic_segmentation_entry_expected = db.query_semantic_segmentation_entry(analytics_data_id, opts);
    if (!closest_semantic_segmentation_entry_expected.has_value())
    {
        // No analytics data available within delta window - this is normal
        LOGGER__MODULE__TRACE(MODULE_NAME,
                              "No semantic segmentation entry found in DB within delta for analytics_data_id: {}",
                              analytics_data_id);
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }
    auto closest_semantic_segmentation_entry = closest_semantic_segmentation_entry_expected.value();
    auto delta_ns = std::abs((closest_semantic_segmentation_entry.ts - opts.m_ts).count());
    LOGGER__MODULE__TRACE(MODULE_NAME, "Semantic segmentation entry found with delta: {} ns ({} ms)", delta_ns,
                          delta_ns / 1000000);

    // If the analytics buffer is empty (no detections), that's normal
    if (closest_semantic_segmentation_entry.analytics_buffer.empty())
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "Analytics buffer is empty (no detections) for analytics_data_id: {}",
                              analytics_data_id);
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    for (size_t i = 0; i < closest_semantic_segmentation_entry.analytics_buffer.size(); i++)
    {
        const auto &segmentation_mask = closest_semantic_segmentation_entry.analytics_buffer[i];
        if (m_dynamic_masks_rois.size() >= MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS)
        {
            LOGGER__MODULE__WARNING(MODULE_NAME,
                                    "Reached MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS ({}), skipping remaining ROIs.",
                                    MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS);
            break;
        }

        // Verify that the dynamic analytics data ID exists in the config
        if (analytics_config.semantic_segmentation_analytics_config.find(analytics_data_id) ==
            analytics_config.semantic_segmentation_analytics_config.end())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Analytics config for ID {} not found", analytics_data_id);
            return media_library_return::MEDIA_LIBRARY_ERROR;
        }

        // Check if this class should be masked
        if (std::find_if(analytics_config.semantic_segmentation_analytics_config.at(analytics_data_id).labels.begin(),
                         analytics_config.semantic_segmentation_analytics_config.at(analytics_data_id).labels.end(),
                         [&](const label_t &label) { return label.id == segmentation_mask.class_id; }) ==
            analytics_config.semantic_segmentation_analytics_config.at(analytics_data_id).labels.end())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Skipping segmentation mask for unknown class_id {}",
                                  segmentation_mask.class_id);
            continue;
        }

        // Find the label name for this class_id
        auto &labels = analytics_config.semantic_segmentation_analytics_config.at(analytics_data_id).labels;
        auto label_it = std::find_if(labels.begin(), labels.end(),
                                     [&](const label_t &label) { return label.id == segmentation_mask.class_id; });

        if (label_it == labels.end())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Skipping segmentation mask for unknown class_id {}",
                                  segmentation_mask.class_id);
            continue;
        }

        // Check if the label name is in the masked labels
        if (std::find(masked_labels.begin(), masked_labels.end(), label_it->label) == masked_labels.end())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME,
                                  "Skipping segmentation mask for label '{}' (class_id {}) not in masked labels",
                                  label_it->label, segmentation_mask.class_id);
            continue;
        }

        // Validate mask size
        size_t expected_float_size = segmentation_mask.width * segmentation_mask.height;
        if (segmentation_mask.mask_size != expected_float_size)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Unexpected mask size: got {}, expected {} for {}x{} float mask",
                                  segmentation_mask.mask_size, expected_float_size, segmentation_mask.width,
                                  segmentation_mask.height);
            continue;
        }

        // Detection coordinates are in pixels relative to the original FHD frame
        float x1 = segmentation_mask.detection_x;
        float y1 = segmentation_mask.detection_y;
        float x2 = segmentation_mask.detection_x + segmentation_mask.detection_width;
        float y2 = segmentation_mask.detection_y + segmentation_mask.detection_height;

        // Get network frame dimensions from analytics config
        size_t network_frame_width =
            analytics_config.semantic_segmentation_analytics_config.at(analytics_data_id).width;
        size_t network_frame_height =
            analytics_config.semantic_segmentation_analytics_config.at(analytics_data_id).height;

        // Scale coordinates based on scaling mode
        size_t scaled_x1, scaled_y1, scaled_x2, scaled_y2;
        size_t scaled_fnetwwork_width, scaled_frame_height;

        LOGGER__MODULE__TRACE(MODULE_NAME,
                              "Processing segmentation mask for class_id {}, box: ({}, {}), ({}, {}), "
                              "input_frame_net_width: {}, input_frame_net_height: {} ",
                              segmentation_mask.class_id, x1, y1, x2, y2, network_frame_width, network_frame_height);

        scale_detection_coordinates(x1, y1, x2, y2, scaled_x1, scaled_y1, scaled_x2, scaled_y2, scaled_fnetwwork_width,
                                    scaled_frame_height, network_frame_width, network_frame_height);

        // log m_dynamic_masks_rois info
        LOGGER__MODULE__TRACE(MODULE_NAME,
                              "Adding dynamic privacy mask ROI ({},{}) to ({},{}), "
                              "input_frame_net_width: {}, input_frame_net_height: {}, dilation_size: {}",
                              scaled_x1, scaled_y1, scaled_x2, scaled_y2, scaled_fnetwwork_width, scaled_frame_height,
                              dilation_size);

        m_dynamic_masks_rois.push_back(dsp_dynamic_privacy_mask_roi_t{
            .bytemask = segmentation_mask.mask,
            .input_frame_net_width = scaled_fnetwwork_width,
            .input_frame_net_height = scaled_frame_height,
            .letterbox = DSP_LETTERBOX_MIDDLE,
            .roi =
                {
                    .start_x = scaled_x1,
                    .start_y = scaled_y1,
                    .end_x = scaled_x2,
                    .end_y = scaled_y2,
                },
            .dilation_size = dilation_size,
        });
    }

    m_latest_privacy_masks->dynamic_data.dynamic_mask_group.masks = m_dynamic_masks_rois.data();
    m_latest_privacy_masks->dynamic_data.dynamic_mask_group.masks_count = m_dynamic_masks_rois.size();

    auto original_width_ratio =
        analytics_config.semantic_segmentation_analytics_config.at(analytics_data_id).original_width_ratio;
    auto original_height_ratio =
        analytics_config.semantic_segmentation_analytics_config.at(analytics_data_id).original_height_ratio;
    m_latest_privacy_masks->dynamic_data.dynamic_mask_group.original_aspect_ratio =
        static_cast<float>(original_width_ratio) / original_height_ratio;

    m_latest_privacy_masks->dynamic_data.dynamic_mask_group.scaling_mode = DSP_SCALING_MODE_STRETCH;

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMask::process_detection_masks(const AnalyticsQueryOptions &opts,
                                                          const application_analytics_config_t &analytics_config,
                                                          const std::string &analytics_data_id,
                                                          const std::vector<std::string> &masked_labels,
                                                          const size_t dilation_size)
{
    static std::vector<uint8_t> constant_mask(MASK_SIZE * MASK_SIZE, 1); // All 1's

    LOGGER__MODULE__TRACE(MODULE_NAME, "Processing detections for analytics_id: {}", analytics_data_id);

    auto db_query_result = AnalyticsDB::instance().query_detection_entry(analytics_data_id, opts);
    if (!db_query_result.has_value())
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "No detection data available for analytics_id {}", analytics_data_id);
        return MEDIA_LIBRARY_SUCCESS; // Not an error - just no data yet
    }

    auto &detection_data = db_query_result.value();
    auto delta_ns = std::abs((detection_data.ts - opts.m_ts).count());
    LOGGER__MODULE__TRACE(MODULE_NAME, "Detection entry found with delta: {} ns ({} ms)", delta_ns, delta_ns / 1000000);
    auto &detections = detection_data.analytics_buffer;

    if (detections.empty())
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "No detections found for analytics_id: {}", analytics_data_id);
        return MEDIA_LIBRARY_SUCCESS;
    }

    LOGGER__MODULE__TRACE(MODULE_NAME, "Found {} detections", detections.size());
    (void)masked_labels; // Currently unused
    // Get image dimensions from analytics config

    // Process each detection bbox
    for (const auto &detection : detections)
    {

        LOGGER__MODULE__TRACE(MODULE_NAME,
                              "[process_detection_masks] Creating constant mask for detection: "
                              "class_id={}, score={}, bbox=({}, {}, {}, {})",
                              detection.class_id, detection.score, detection.x_min, detection.y_min, detection.x_max,
                              detection.y_max);

        // Get network frame dimensions from analytics config
        size_t network_frame_width = analytics_config.detection_analytics_config.at(analytics_data_id).width;
        size_t network_frame_height = analytics_config.detection_analytics_config.at(analytics_data_id).height;

        // Scale coordinates based on scaling mode
        size_t scaled_x1, scaled_y1, scaled_x2, scaled_y2;
        size_t scaled_fnetwwork_width, scaled_frame_height;

        scale_detection_coordinates(detection.x_min, detection.y_min, detection.x_max, detection.y_max, scaled_x1,
                                    scaled_y1, scaled_x2, scaled_y2, scaled_fnetwwork_width, scaled_frame_height,
                                    network_frame_width, network_frame_height);

        // log m_dynamic_masks_rois info
        LOGGER__MODULE__TRACE(MODULE_NAME,
                              "Adding dynamic privacy mask ROI ({},{}) to ({},{}), "
                              "input_frame_net_width: {}, input_frame_net_height: {}, dilation_size: {}",
                              scaled_x1, scaled_y1, scaled_x2, scaled_y2, scaled_fnetwwork_width, scaled_frame_height,
                              dilation_size);
        m_dynamic_masks_rois.push_back(dsp_dynamic_privacy_mask_roi_t{
            .bytemask = constant_mask.data(),
            .input_frame_net_width = scaled_fnetwwork_width,
            .input_frame_net_height = scaled_frame_height,
            .letterbox = DSP_LETTERBOX_MIDDLE,
            .roi =
                {
                    .start_x = scaled_x1,
                    .start_y = scaled_y1,
                    .end_x = scaled_x2,
                    .end_y = scaled_y2,
                },
            .dilation_size = dilation_size,
        });
    }

    m_latest_privacy_masks->dynamic_data.dynamic_mask_group.masks = m_dynamic_masks_rois.data();
    m_latest_privacy_masks->dynamic_data.dynamic_mask_group.masks_count = m_dynamic_masks_rois.size();

    auto original_width_ratio = analytics_config.detection_analytics_config.at(analytics_data_id).original_width_ratio;
    auto original_height_ratio =
        analytics_config.detection_analytics_config.at(analytics_data_id).original_height_ratio;
    m_latest_privacy_masks->dynamic_data.dynamic_mask_group.original_aspect_ratio =
        static_cast<float>(original_width_ratio) / original_height_ratio;

    m_latest_privacy_masks->dynamic_data.dynamic_mask_group.scaling_mode = DSP_SCALING_MODE_STRETCH;

    return MEDIA_LIBRARY_SUCCESS;
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
