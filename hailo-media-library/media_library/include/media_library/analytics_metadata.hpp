#pragma once

/**
 * @file analytics_metadata.hpp
 * @brief Per-frame carrier of AI inference results attached directly to a
 *        hailo_media_library_buffer.
 *
 * Analytics Metadata is the buffer-attached alternative to the Analytics Database
 * flow: an analytics-pipeline producer writes results onto the buffer's
 * m_analytics_metadata field and consumers (e.g. the dynamic privacy-mask blender)
 * read them directly, without querying the AnalyticsDB singleton.
 *
 * Producers must scale normalized AI outputs into the encoded frame's pixel space
 * (using the wrapped buffer's buffer_data->width/height) before stamping the wire-
 * types. Consumers filter entries by the label string directly, with no class_id-to-
 * label resolution needed.
 */

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "hailo/hailort.h"
#include "media_library_types.hpp"

struct LabeledSemanticMask
{
    hailo_semantic_segmentation_mask_t mask;
    std::string label;
};

/**
 * @brief Detection wire-type for entries without an associated segmentation mask
 *        (consumers apply a constant-fill mask sized to the bbox).
 */
struct LabeledDetection
{
    hailo_detection_t detection; ///< Detection bbox and score in encoded-frame pixel space.
    std::string label;           ///< Detection label stamped by the producer; used by consumers as the filter key.
};

/**
 * @brief Per-frame container of AI inference results attached to a media-library
 *        buffer's m_analytics_metadata field. A null carrier means the frame has no
 *        AI results.
 */
struct AnalyticsMetadata
{
    std::shared_ptr<std::vector<LabeledSemanticMask>> m_semantic_segmentation; ///< Optional segmentation entries.
    std::shared_ptr<std::vector<LabeledDetection>>
        m_detections; ///< Optional detection-only entries (no segmentation mask).
    std::vector<std::shared_ptr<void>>
        m_source_keepalives; ///< Type-erased refs to source tensor buffers; pins HailoRT pool entries so mask pointers
                             ///< stay valid until the DSP call completes.
};

using AnalyticsMetadataPtr = std::shared_ptr<AnalyticsMetadata>;
