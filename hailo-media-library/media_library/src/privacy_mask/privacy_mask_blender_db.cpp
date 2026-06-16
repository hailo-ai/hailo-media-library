// Deprecated AnalyticsDB-driven fallback for update_dynamic_mask, used when the input buffer
// has no attached AnalyticsMetadata.

#include <hailo/hailodsp.h>
#include <hailo/hailort.h>
#include <stdint.h>
#include <tl/expected.hpp>
#include <algorithm>
#include <chrono>
#include <vector>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "privacy_mask.hpp"
#include "analytics_db.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "privacy_mask_dynamic.hpp"
#include "privacy_mask_types.hpp"
#include "buffer_pool.hpp"

#define MODULE_NAME LoggerType::PrivacyMask

namespace
{

dsp_letterbox_alignment_t scaling_mode_to_dsp_letterbox(ScalingMode mode)
{
    switch (mode)
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

void append_rois_from_db_semantic_segmentation(const std::vector<hailo_semantic_segmentation_mask_t> &masks,
                                               const application_analytics_config_t &analytics_config,
                                               const std::string &analytics_data_id,
                                               const std::vector<std::string> &masked_labels, std::size_t dilation_size,
                                               std::vector<dsp_dynamic_privacy_mask_roi_t> &out_rois)
{
    auto cfg_it = analytics_config.semantic_segmentation_analytics_config.find(analytics_data_id);
    if (cfg_it == analytics_config.semantic_segmentation_analytics_config.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Analytics config for ID '{}' not found", analytics_data_id);
        return;
    }
    const auto &id_cfg = cfg_it->second;

    for (const auto &mask : masks)
    {
        if (out_rois.size() >= MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS)
        {
            LOGGER__MODULE__WARNING(MODULE_NAME,
                                    "Reached MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS ({}), skipping remaining ROIs.",
                                    MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS);
            break;
        }

        auto label_it = std::find_if(id_cfg.labels.begin(), id_cfg.labels.end(),
                                     [&](const label_t &l) { return l.id == mask.class_id; });
        if (label_it == id_cfg.labels.end())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Skipping segmentation mask for unknown class_id {}", mask.class_id);
            continue;
        }
        if (std::find(masked_labels.begin(), masked_labels.end(), label_it->label) == masked_labels.end())
            continue;

        std::size_t scaled_x1, scaled_y1, scaled_x2, scaled_y2;
        std::size_t scaled_network_width, scaled_frame_height;

        privacy_mask::dynamic::scale_detection_coordinates(
            mask.detection_x, mask.detection_y, mask.detection_x + mask.detection_width,
            mask.detection_y + mask.detection_height, scaled_x1, scaled_y1, scaled_x2, scaled_y2, scaled_network_width,
            scaled_frame_height, /*network_frame_width=*/id_cfg.width, /*network_frame_height=*/id_cfg.height,
            mask.width);

        out_rois.push_back(dsp_dynamic_privacy_mask_roi_t{
            .bytemask = mask.mask,
            .input_frame_net_width = scaled_network_width,
            .input_frame_net_height = scaled_frame_height,
            .letterbox = scaling_mode_to_dsp_letterbox(id_cfg.scaling_mode),
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
}

void append_rois_from_db_detections(const std::vector<hailo_detection_t> &detections,
                                    const application_analytics_config_t &analytics_config,
                                    const std::string &analytics_data_id, const std::vector<std::string> &masked_labels,
                                    std::size_t dilation_size, std::vector<dsp_dynamic_privacy_mask_roi_t> &out_rois)
{
    static std::vector<uint8_t> constant_mask(MASK_SIZE * MASK_SIZE, 1);

    auto cfg_it = analytics_config.detection_analytics_config.find(analytics_data_id);
    if (cfg_it == analytics_config.detection_analytics_config.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Analytics config for ID '{}' not found", analytics_data_id);
        return;
    }
    const auto &id_cfg = cfg_it->second;

    for (const auto &detection : detections)
    {
        if (out_rois.size() >= MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS)
        {
            LOGGER__MODULE__WARNING(MODULE_NAME,
                                    "Reached MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS ({}), skipping remaining detection ROIs.",
                                    MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS);
            break;
        }

        auto label_it = std::find_if(id_cfg.labels.begin(), id_cfg.labels.end(),
                                     [&](const label_t &l) { return l.id == detection.class_id; });
        if (label_it == id_cfg.labels.end())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Skipping detection for unknown class_id {}", detection.class_id);
            continue;
        }
        if (std::find(masked_labels.begin(), masked_labels.end(), label_it->label) == masked_labels.end())
            continue;

        std::size_t scaled_x1, scaled_y1, scaled_x2, scaled_y2;
        std::size_t scaled_network_width, scaled_frame_height;

        privacy_mask::dynamic::scale_detection_coordinates(detection.x_min, detection.y_min, detection.x_max,
                                                           detection.y_max, scaled_x1, scaled_y1, scaled_x2, scaled_y2,
                                                           scaled_network_width, scaled_frame_height,
                                                           /*network_frame_width=*/id_cfg.width,
                                                           /*network_frame_height=*/id_cfg.height, MASK_SIZE,
                                                           /*fit_to_box=*/true);

        out_rois.push_back(dsp_dynamic_privacy_mask_roi_t{
            .bytemask = constant_mask.data(),
            .input_frame_net_width = scaled_network_width,
            .input_frame_net_height = scaled_frame_height,
            .letterbox = scaling_mode_to_dsp_letterbox(id_cfg.scaling_mode),
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
}

} // anonymous namespace

media_library_return PrivacyMask::update_dynamic_mask_from_db(
    const config_encoded_output_stream_t &encoded_output_streams_config, HailoMediaLibraryBufferPtr input_buffer)
{
    auto &dynamic_mask_config = encoded_output_streams_config.masking.dynamic_privacy_mask_config;

    LOGGER__MODULE__TRACE(MODULE_NAME, "Updating dynamic mask via AnalyticsDB");

    auto &db = AnalyticsDB::instance();
    std::chrono::nanoseconds isp_timestamp(input_buffer->isp_timestamp_ns);
    std::chrono::time_point<std::chrono::steady_clock> isp_timestamp_tp(isp_timestamp);

    AnalyticsQueryOptions opts{
        .m_type = dynamic_mask_config->query_type,
        .m_ts = isp_timestamp_tp,
        .m_delta = std::chrono::milliseconds(dynamic_mask_config->delta_ms),
        .m_timeout = std::chrono::milliseconds(dynamic_mask_config->timeout_ms),
    };

    auto analytics_config = db.get_application_analytics_config();
    const std::size_t dilation_size = dynamic_mask_config->dilation_size;

    auto &dynamic_mask_group_dsp_params = m_latest_privacy_masks->dynamic_data.dynamic_mask_group;
    float last_original_aspect_ratio = 1.0f;

    for (const auto &entry : dynamic_mask_config->analytics)
    {
        const std::string &analytics_data_id = entry.analytics_data_id;
        const std::vector<std::string> &masked_labels = entry.masked_labels;

        auto seg_it = analytics_config.semantic_segmentation_analytics_config.find(analytics_data_id);
        auto det_it = analytics_config.detection_analytics_config.find(analytics_data_id);

        if (seg_it != analytics_config.semantic_segmentation_analytics_config.end())
        {
            auto query_result = db.query_semantic_segmentation_entry(analytics_data_id, opts);
            if (!query_result.has_value())
            {
                LOGGER__MODULE__TRACE(MODULE_NAME, "No semantic segmentation entry in DB for id '{}'",
                                      analytics_data_id);
                continue;
            }
            append_rois_from_db_semantic_segmentation(query_result.value().analytics_buffer, analytics_config,
                                                      analytics_data_id, masked_labels, dilation_size,
                                                      m_dynamic_masks_rois);
            // Last-id-wins, matching historical semantics for byte-identical DSP commands.
            last_original_aspect_ratio =
                static_cast<float>(seg_it->second.original_width_ratio) / seg_it->second.original_height_ratio;
        }
        else if (det_it != analytics_config.detection_analytics_config.end())
        {
            auto query_result = db.query_detection_entry(analytics_data_id, opts);
            if (!query_result.has_value())
            {
                LOGGER__MODULE__TRACE(MODULE_NAME, "No detection entry in DB for id '{}'", analytics_data_id);
                continue;
            }
            append_rois_from_db_detections(query_result.value().analytics_buffer, analytics_config, analytics_data_id,
                                           masked_labels, dilation_size, m_dynamic_masks_rois);
            last_original_aspect_ratio =
                static_cast<float>(det_it->second.original_width_ratio) / det_it->second.original_height_ratio;
        }
        else
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Analytics config for id '{}' not found in any analytics type",
                                  analytics_data_id);
        }
    }

    dynamic_mask_group_dsp_params.masks = m_dynamic_masks_rois.data();
    dynamic_mask_group_dsp_params.masks_count = m_dynamic_masks_rois.size();
    dynamic_mask_group_dsp_params.original_aspect_ratio = last_original_aspect_ratio;
    dynamic_mask_group_dsp_params.scaling_mode = DSP_SCALING_MODE_STRETCH;
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}
