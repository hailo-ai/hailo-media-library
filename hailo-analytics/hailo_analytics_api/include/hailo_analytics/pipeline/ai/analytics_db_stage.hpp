#pragma once

// General includes
#include <memory>
#include <optional>
#include <cstdlib>
#include <ctime>

// HailoRT includes
#include "hailo/hailort.hpp"

// Postprocess Tools includes
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// Media library includes
#include "media_library/analytics_db.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::ai
{

class AnalyticsDBStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    std::string m_analytics_data_id;
    AnalyticsType m_type;

    AppStatus process_detection(BufferPtr data, HailoMediaLibraryBufferPtr /*media_lib_buffer*/);
    AppStatus process_instance_segmentation(BufferPtr data, HailoMediaLibraryBufferPtr media_lib_buffer);
    AppStatus process_semantic_segmentation(BufferPtr data, HailoMediaLibraryBufferPtr media_lib_buffer);

    std::chrono::time_point<std::chrono::steady_clock> get_timestamp_from_buffer(BufferPtr data) const;
    std::vector<HailoMediaLibraryBufferPtr> collect_tensor_buffers(BufferPtr data) const;

    // Used to maintain synchronization when no masks are found
    AppStatus add_empty_semantic_segmentation_entry(BufferPtr data, const std::string &reason);

    // Returns float coordinates to preserve sub-pixel accuracy
    static void convert_bbox_to_pixel_coords(const HailoBBox &bbox, uint32_t width, uint32_t height, float32_t &x_min,
                                             float32_t &y_min, float32_t &x_max, float32_t &y_max);

    // Uses floor for min coords and ceil for max coords, clamped to [0, dimension]
    static void convert_bbox_to_clamped_pixel_coords(float norm_x_min, float norm_y_min, float norm_x_max,
                                                     float norm_y_max, uint32_t width, uint32_t height, uint32_t &x_min,
                                                     uint32_t &y_min, uint32_t &x_max, uint32_t &y_max);

    tl::expected<detection_analytics_config_t, AppStatus> get_detection_config() const;
    tl::expected<instance_segmentation_analytics_config_t, AppStatus> get_instance_segmentation_config() const;
    tl::expected<semantic_segmentation_analytics_config_t, AppStatus> get_semantic_segmentation_config() const;

    // Matching strategy: checks if either label contains the other (bidirectional substring match)
    static std::optional<uint32_t> find_class_id_for_label(const std::string &detection_label,
                                                           const std::vector<label_t> &labels);

    static std::optional<hailo_semantic_segmentation_mask_t> extract_mask_from_detection(HailoDetectionPtr detection,
                                                                                         int target_class_id,
                                                                                         uint32_t image_width,
                                                                                         uint32_t image_height);

  public:
    AnalyticsDBStage(const std::string &name, size_t queue_size, bool leaky, const std::string &analytics_data_id,
                     AnalyticsType type = AnalyticsType::INSTANCE_SEGMENTATION,
                     bool trace_processing_operations = true);

    AppStatus process(BufferPtr data) override;
};

class AnalyticsDBStageBuild : public AnalyticsDBStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = 10;
        bool m_leaky = false;
        std::optional<std::string> m_analytics_data_id;
        AnalyticsType m_type = AnalyticsType::INSTANCE_SEGMENTATION;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_queue_size(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_analytics_data_id(const std::string &analytics_data_id);
        Builder &set_type(AnalyticsType type);
        Builder &set_trace_opt(bool activate);

        std::shared_ptr<AnalyticsDBStage> buildptr() const;
    };

    static Builder create();
};

inline std::optional<uint32_t> AnalyticsDBStage::find_class_id_for_label(const std::string &detection_label,
                                                                         const std::vector<label_t> &labels)
{
    // Bidirectional substring matching handles cases where:
    // - Detection label "person" matches config label "person_face"
    // - Detection label "face" matches config label "person_face"
    // - Detection label "person_face" matches config label "person"
    for (const auto &label_entry : labels)
    {
        if (label_entry.label.find(detection_label) != std::string::npos ||
            detection_label.find(label_entry.label) != std::string::npos)
        {
            return label_entry.id;
        }
    }
    return std::nullopt;
}

} // namespace hailo_analytics::pipeline::ai
