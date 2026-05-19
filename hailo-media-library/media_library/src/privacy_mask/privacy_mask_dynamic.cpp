#include "privacy_mask_dynamic.hpp"

#include <hailo/hailort.h>
#include <algorithm>
#include <cstdint>

#include "media_library_logger.hpp"
#include "privacy_mask.hpp"       // MASK_SIZE
#include "privacy_mask_types.hpp" // MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS
#include "media_library_types.hpp"

namespace privacy_mask::dynamic
{

namespace
{
inline bool is_degenerate_roi(std::size_t x1, std::size_t y1, std::size_t x2, std::size_t y2)
{
    return x1 >= x2 || y1 >= y2;
}
} // namespace

void scale_detection_coordinates(float detection_roi_x1, float detection_roi_y1, float detection_roi_x2,
                                 float detection_roi_y2, std::size_t &scaled_x1, std::size_t &scaled_y1,
                                 std::size_t &scaled_x2, std::size_t &scaled_y2, std::size_t &network_width,
                                 std::size_t &network_height, std::size_t network_frame_width,
                                 std::size_t network_frame_height, std::size_t mask_size, bool fit_to_box)
{
    float mask_size_f = static_cast<float>(mask_size);
    std::size_t detection_roi_width = static_cast<std::size_t>(detection_roi_x2 - detection_roi_x1);
    std::size_t detection_roi_height = static_cast<std::size_t>(detection_roi_y2 - detection_roi_y1);
    float max_dimension = (std::max)(static_cast<float>(detection_roi_width), static_cast<float>(detection_roi_height));
    float scale = mask_size_f / max_dimension;

    float scaled_width = static_cast<float>(detection_roi_width) * scale;
    float scaled_height = static_cast<float>(detection_roi_height) * scale;

    float pad_x = (mask_size_f - scaled_width) / 2.0f;
    float pad_y = (mask_size_f - scaled_height) / 2.0f;

    float adjusted_roi_x1 = detection_roi_x1 * scale - pad_x;
    float adjusted_roi_y1 = detection_roi_y1 * scale - pad_y;

    float scaled_frame_width = static_cast<float>(network_frame_width) * scale;
    float scaled_frame_height = static_cast<float>(network_frame_height) * scale;

    if (max_dimension == detection_roi_height)
    {
        scaled_frame_width += 2 * mask_size_f;
        adjusted_roi_x1 += mask_size_f;
    }
    else if (max_dimension == detection_roi_width)
    {
        scaled_frame_height += 2 * mask_size_f;
        adjusted_roi_y1 += mask_size_f;
    }

    scaled_x1 = static_cast<std::size_t>(adjusted_roi_x1);
    scaled_y1 = static_cast<std::size_t>(adjusted_roi_y1);

    if (fit_to_box)
    {
        scaled_x1 += static_cast<std::size_t>(pad_x);
        scaled_y1 += static_cast<std::size_t>(pad_y);
        scaled_x2 = scaled_x1 + static_cast<std::size_t>(scaled_width);
        scaled_y2 = scaled_y1 + static_cast<std::size_t>(scaled_height);
    }
    else
    {
        scaled_x2 = scaled_x1 + mask_size;
        scaled_y2 = scaled_y1 + mask_size;
    }

    network_width = static_cast<std::size_t>(scaled_frame_width);
    network_height = static_cast<std::size_t>(scaled_frame_height);
    if (scaled_x2 > network_width)
    {
        network_width = scaled_x2;
    }
    if (scaled_y2 > network_height)
    {
        network_height = scaled_y2;
    }
    LOGGER__MODULE__TRACE(LoggerType::PrivacyMask,
                          "Scaled coordinates: x1={}, y1={}, x2={}, y2={}, network_width={}, network_height={}",
                          scaled_x1, scaled_y1, scaled_x2, scaled_y2, network_width, network_height);
}

void append_rois_from_buffer_semantic_segmentation(const std::vector<LabeledSemanticMask> &items,
                                                   const std::vector<std::string> &masked_labels,
                                                   const std::vector<label_t> &label_to_class_id,
                                                   std::size_t dilation_size, std::size_t frame_width,
                                                   std::size_t frame_height,
                                                   std::vector<dsp_dynamic_privacy_mask_roi_t> &out_rois)
{
    for (const auto &item : items)
    {
        if (std::find(masked_labels.begin(), masked_labels.end(), item.label) == masked_labels.end())
        {
            LOGGER__MODULE__TRACE(LoggerType::PrivacyMask,
                                  "Skipping segmentation mask: label '{}' not in masked_labels", item.label);
            continue;
        }

        // The encoder emits one wire-type per class_mask child of each detection. For a person
        // detection with 2 outputs (vehicle, person_face), both arrive here with label="person"
        // but mask.class_id = 0 and mask.class_id = 1 respectively. The map says person→1, so
        // drop the one whose class_id doesn't match.
        auto map_it = std::find_if(label_to_class_id.begin(), label_to_class_id.end(),
                                   [&](const label_t &m) { return m.label == item.label; });
        if (map_it == label_to_class_id.end())
        {
            LOGGER__MODULE__TRACE(LoggerType::PrivacyMask,
                                  "Skipping segmentation mask: label '{}' has no segmentor mapping", item.label);
            continue;
        }
        if (map_it->id != item.mask.class_id)
        {
            LOGGER__MODULE__TRACE(LoggerType::PrivacyMask,
                                  "Skipping segmentation mask: label '{}' expects class_id {} but got {}", item.label,
                                  map_it->id, item.mask.class_id);
            continue;
        }

        const auto &segmentation_mask = item.mask;

        if (out_rois.size() >= MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS)
        {
            LOGGER__MODULE__WARNING(LoggerType::PrivacyMask,
                                    "Reached MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS ({}), skipping remaining ROIs.",
                                    MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS);
            break;
        }

        std::size_t expected_size = segmentation_mask.width * segmentation_mask.height;
        if (segmentation_mask.mask_size != expected_size)
        {
            LOGGER__MODULE__ERROR(LoggerType::PrivacyMask, "Unexpected mask size: got {}, expected {} for {}x{} mask",
                                  segmentation_mask.mask_size, expected_size, segmentation_mask.width,
                                  segmentation_mask.height);
            continue;
        }

        float x1 = segmentation_mask.detection_x;
        float y1 = segmentation_mask.detection_y;
        float x2 = segmentation_mask.detection_x + segmentation_mask.detection_width;
        float y2 = segmentation_mask.detection_y + segmentation_mask.detection_height;

        std::size_t scaled_x1, scaled_y1, scaled_x2, scaled_y2;
        std::size_t scaled_network_width, scaled_frame_height;

        scale_detection_coordinates(x1, y1, x2, y2, scaled_x1, scaled_y1, scaled_x2, scaled_y2, scaled_network_width,
                                    scaled_frame_height,
                                    /*network_frame_width=*/frame_width,
                                    /*network_frame_height=*/frame_height, segmentation_mask.width);

        if (is_degenerate_roi(scaled_x1, scaled_y1, scaled_x2, scaled_y2))
        {
            continue;
        }

        out_rois.push_back(dsp_dynamic_privacy_mask_roi_t{
            .bytemask = segmentation_mask.mask,
            .input_frame_net_width = scaled_network_width,
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
}

void append_rois_from_buffer_detections(const std::vector<LabeledDetection> &items,
                                        const std::vector<std::string> &masked_labels, std::size_t dilation_size,
                                        std::size_t frame_width, std::size_t frame_height,
                                        std::vector<dsp_dynamic_privacy_mask_roi_t> &out_rois)
{
    static std::vector<std::uint8_t> constant_mask(MASK_SIZE * MASK_SIZE, 1);

    for (const auto &item : items)
    {
        if (std::find(masked_labels.begin(), masked_labels.end(), item.label) == masked_labels.end())
        {
            LOGGER__MODULE__TRACE(LoggerType::PrivacyMask,
                                  "Skipping overflow detection: label '{}' not in masked_labels", item.label);
            continue;
        }

        const auto &detection = item.detection;

        if (out_rois.size() >= MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS)
        {
            LOGGER__MODULE__WARNING(LoggerType::PrivacyMask,
                                    "Reached MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS ({}), skipping remaining detection ROIs.",
                                    MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS);
            break;
        }

        std::size_t scaled_x1, scaled_y1, scaled_x2, scaled_y2;
        std::size_t scaled_network_width, scaled_frame_height;

        scale_detection_coordinates(detection.x_min, detection.y_min, detection.x_max, detection.y_max, scaled_x1,
                                    scaled_y1, scaled_x2, scaled_y2, scaled_network_width, scaled_frame_height,
                                    /*network_frame_width=*/frame_width,
                                    /*network_frame_height=*/frame_height, MASK_SIZE,
                                    /*fit_to_box=*/true);

        if (is_degenerate_roi(scaled_x1, scaled_y1, scaled_x2, scaled_y2))
        {
            continue;
        }

        out_rois.push_back(dsp_dynamic_privacy_mask_roi_t{
            .bytemask = constant_mask.data(),
            .input_frame_net_width = scaled_network_width,
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
}

} // namespace privacy_mask::dynamic
