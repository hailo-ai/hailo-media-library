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

/**
 * @brief Stage for processing AI analytics data and storing it in the Analytics Database
 *
 * This stage processes different types of analytics data (detection, instance segmentation,
 * semantic segmentation) from AI pipeline buffers and stores them in the Analytics Database
 * for later retrieval and processing. The stage extracts relevant metadata, converts coordinates,
 * and maintains tensor buffer references to ensure data validity.
 *
 * Supported analytics types:
 * - Detection: Processes bounding boxes and stores detection results
 * - Instance Segmentation: Processes segmentation masks with bounding boxes
 * - Semantic Segmentation: Processes semantic masks with class labels
 */
class AnalyticsDBStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    std::string m_analytics_data_id;
    AnalyticsType m_type;
    std::string m_overflow_analytics_data_id;
    std::optional<SemanticSegmentationAnalyticsData> m_last_semantic_segmentation_data;

    /**
     * @brief Process detection analytics data
     * @param data Buffer containing detection data
     * @return AppStatus indicating success or failure
     */
    AppStatus process_detection(BufferPtr data);

    /**
     * @brief Process semantic segmentation analytics data
     * @param data Buffer containing semantic segmentation data
     * @param media_lib_buffer Media library buffer pointer containing tensor data
     * @return AppStatus indicating success or failure
     */
    AppStatus process_semantic_segmentation(BufferPtr data, HailoMediaLibraryBufferPtr media_lib_buffer);

    /**
     * @brief Walk the ROI for HailoDetections without a HailoClassMask child and write them to
     *        m_overflow_analytics_data_id. No-op if the overflow id is empty or the AnalyticsDB
     *        has no detection_analytics_config entry for it.
     */
    void process_overflow_detections(BufferPtr data);

    /**
     * @brief Extract timestamp from buffer's ISP timestamp
     * @param data Buffer containing ISP timestamp
     * @return Timestamp as chrono time_point
     */
    std::chrono::time_point<std::chrono::steady_clock> get_timestamp_from_buffer(BufferPtr data) const;

    /**
     * @brief Collect tensor buffers from metadata to maintain lifetime
     * @param data Buffer containing tensor metadata
     * @return Vector of media library buffer pointers
     */
    std::vector<HailoMediaLibraryBufferPtr> collect_tensor_buffers(BufferPtr data) const;

    /**
     * @brief Add empty semantic segmentation entry to maintain synchronization
     * @param data Buffer to extract timestamp from
     * @param reason Reason string for logging
     * @return AppStatus indicating success or failure
     */
    AppStatus add_cached_or_empty_semantic_segmentation_entry(BufferPtr data, const std::string &reason);

    /**
     * @brief Convert bounding box from normalized to pixel coordinates
     * @param bbox Input bounding box in normalized coordinates [0,1]
     * @param width Image width in pixels
     * @param height Image height in pixels
     * @param x_min Output minimum x coordinate in pixels
     * @param y_min Output minimum y coordinate in pixels
     * @param x_max Output maximum x coordinate in pixels
     * @param y_max Output maximum y coordinate in pixels
     */
    static void convert_bbox_to_pixel_coords(const HailoBBox &bbox, uint32_t width, uint32_t height, float32_t &x_min,
                                             float32_t &y_min, float32_t &x_max, float32_t &y_max);

    /**
     * @brief Convert bounding box to clamped pixel coordinates
     * @param norm_x_min Normalized minimum x coordinate [0,1]
     * @param norm_y_min Normalized minimum y coordinate [0,1]
     * @param norm_x_max Normalized maximum x coordinate [0,1]
     * @param norm_y_max Normalized maximum y coordinate [0,1]
     * @param width Image width in pixels
     * @param height Image height in pixels
     * @param x_min Output minimum x coordinate (clamped)
     * @param y_min Output minimum y coordinate (clamped)
     * @param x_max Output maximum x coordinate (clamped)
     * @param y_max Output maximum y coordinate (clamped)
     * @note Uses floor for min coords and ceil for max coords, clamped to [0, dimension]
     */
    static void convert_bbox_to_clamped_pixel_coords(float norm_x_min, float norm_y_min, float norm_x_max,
                                                     float norm_y_max, uint32_t width, uint32_t height, uint32_t &x_min,
                                                     uint32_t &y_min, uint32_t &x_max, uint32_t &y_max);

    /**
     * @brief Get detection analytics configuration
     * @return Expected containing detection config or error status
     */
    tl::expected<detection_analytics_config_t, AppStatus> get_detection_config() const;

    /**
     * @brief Get semantic segmentation analytics configuration
     * @return Expected containing semantic segmentation config or error status
     */
    tl::expected<semantic_segmentation_analytics_config_t, AppStatus> get_semantic_segmentation_config() const;

    /**
     * @brief Find class ID for a given label using bidirectional substring matching
     * @param detection_label Label from detection object
     * @param labels Vector of label configurations
     * @return Optional class ID if match found
     * @note Checks if either label contains the other (bidirectional substring match)
     */
    static std::optional<uint32_t> find_class_id_for_label(const std::string &detection_label,
                                                           const std::vector<label_t> &labels);

    /**
     * @brief Extract semantic segmentation mask from detection object
     * @param detection Detection object containing nested mask
     * @param target_class_id Target class ID index
     * @param image_width Image width in pixels
     * @param image_height Image height in pixels
     * @return Optional mask if found at target index
     */
    static std::optional<hailo_semantic_segmentation_mask_t> extract_mask_from_detection(HailoDetectionPtr detection,
                                                                                         int target_class_id,
                                                                                         uint32_t image_width,
                                                                                         uint32_t image_height);

  public:
    /**
     * @brief Constructor for AnalyticsDBStage
     * @param name Stage name for identification
     * @param queue_size Size of the processing queue
     * @param leaky Whether the queue should drop old frames when full
     * @param analytics_data_id Identifier for the analytics data in the database
     * @param type Type of analytics to process (default: SEMANTIC_SEGMENTATION)
     * @param trace_processing_operations Enable tracing for processing operations (default: true)
     * @param overflow_analytics_data_id Identifier for the overflow stream; empty disables it.
     */
    AnalyticsDBStage(const std::string &name, size_t queue_size, bool leaky, const std::string &analytics_data_id,
                     AnalyticsType type = AnalyticsType::SEMANTIC_SEGMENTATION, bool trace_processing_operations = true,
                     const std::string &overflow_analytics_data_id = "");

    /**
     * @brief Process buffer and add analytics data to database
     * @param data Buffer containing analytics data to process
     * @return AppStatus indicating success or failure
     */
    AppStatus process(BufferPtr data) override;
};

/**
 * @brief Builder pattern implementation for AnalyticsDBStage
 *
 * Provides a fluent interface for constructing AnalyticsDBStage instances
 * with configurable parameters.
 */
class AnalyticsDBStageBuild : public AnalyticsDBStage
{
  public:
    /**
     * @brief Builder class for constructing AnalyticsDBStage instances
     */
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = 10;
        bool m_leaky = false;
        std::optional<std::string> m_analytics_data_id;
        AnalyticsType m_type = AnalyticsType::SEMANTIC_SEGMENTATION;
        bool m_trace = true;
        std::string m_overflow_analytics_data_id;

      public:
        /**
         * @brief Set the stage name
         * @param name Name for the stage
         * @return Builder reference for chaining
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Set the queue size
         * @param size Size of the processing queue
         * @return Builder reference for chaining
         */
        Builder &set_queue_size(size_t size);

        /**
         * @brief Set the leaky option
         * @param activate If true, queue drops old frames when full
         * @return Builder reference for chaining
         */
        Builder &set_leaky_opt(bool activate);

        /**
         * @brief Set the analytics data ID
         * @param analytics_data_id Identifier for analytics data in database
         * @return Builder reference for chaining
         */
        Builder &set_analytics_data_id(const std::string &analytics_data_id);

        /**
         * @brief Set the analytics type
         * @param type Type of analytics to process
         * @return Builder reference for chaining
         */
        Builder &set_type(AnalyticsType type);

        /**
         * @brief Set the trace option
         * @param activate If true, enables tracing for processing operations
         * @return Builder reference for chaining
         */
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Set the overflow analytics_data_id. Empty disables the overflow write.
         * @param overflow_analytics_data_id Identifier for the overflow stream
         * @return Builder reference for chaining
         */
        Builder &set_overflow_analytics_data_id(const std::string &overflow_analytics_data_id);

        /**
         * @brief Build and return shared pointer to AnalyticsDBStage
         * @return Shared pointer to constructed AnalyticsDBStage
         * @throws std::runtime_error if required parameters are missing
         */
        std::shared_ptr<AnalyticsDBStage> buildptr() const;
    };

    /**
     * @brief Create a new Builder instance
     * @return Builder instance for constructing AnalyticsDBStage
     */
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
