#pragma once

// Internal helpers for the dynamic-privacy-mask path: scale detection ROIs into the network
// frame's pixel space and adapt buffer-attached AnalyticsMetadata into DSP ROI entries.
// Not part of the public privacy_mask API.

#include <cstddef>
#include <string>
#include <vector>

#include <hailo/hailodsp.h> // dsp_dynamic_privacy_mask_roi_t

#include "analytics_metadata.hpp"  // LabeledSemanticMask, LabeledDetection
#include "media_library_types.hpp" // label_t

namespace privacy_mask::dynamic
{

// Scale detection ROI coordinates (input) into the network frame's pixel space, with
// letterbox padding for square segmentation masks.
//
// @param fit_to_box  When true (detection-only masking), the output ROI matches the actual
//                    detection bbox dimensions. The mask is a constant fill so no letterbox
//                    scaling is needed.
//                    When false (segmentation masking), the output ROI is a square
//                    mask_size x mask_size to match the segmentation model's square mask
//                    output with letterbox padding.
void scale_detection_coordinates(float detection_roi_x1, float detection_roi_y1, float detection_roi_x2,
                                 float detection_roi_y2, std::size_t &scaled_x1, std::size_t &scaled_y1,
                                 std::size_t &scaled_x2, std::size_t &scaled_y2, std::size_t &network_width,
                                 std::size_t &network_height, std::size_t network_frame_width,
                                 std::size_t network_frame_height, std::size_t mask_size, bool fit_to_box = false);

void append_rois_from_buffer_semantic_segmentation(const std::vector<LabeledSemanticMask> &items,
                                                   const std::vector<std::string> &masked_labels,
                                                   const std::vector<label_t> &label_to_class_id,
                                                   std::size_t dilation_size, std::size_t frame_width,
                                                   std::size_t frame_height,
                                                   std::vector<dsp_dynamic_privacy_mask_roi_t> &out_rois);

void append_rois_from_buffer_detections(const std::vector<LabeledDetection> &items,
                                        const std::vector<std::string> &masked_labels, std::size_t dilation_size,
                                        std::size_t frame_width, std::size_t frame_height,
                                        std::vector<dsp_dynamic_privacy_mask_roi_t> &out_rois,
                                        std::uint8_t *constant_overflow_mask);

} // namespace privacy_mask::dynamic
