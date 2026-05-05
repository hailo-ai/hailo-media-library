#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

/**
 * @brief Default queue size for analytic metadata packager stage
 */
#define ANALYTIC_METADATA_QUEUE_SIZE_DEFAULT (20)

/**
 * @brief Namespace containing field name constants for analytic metadata JSON structure
 */
namespace analytic_metadata_fields
{

constexpr const char *ISP_TIMESTAMP = "isp_timestamp_ns";
constexpr const char *FRAME_WIDTH = "frame_width";
constexpr const char *FRAME_HEIGHT = "frame_height";
constexpr const char *DETECTIONS = "detections";
constexpr const char *LANDMARKS = "landmarks";
constexpr const char *CLASSIFICATIONS = "classifications";

namespace detection
{
constexpr const char *LABEL = "label";
constexpr const char *DETECTION_CONFIDENCE = "confidence";
constexpr const char *BBOX = "bbox";
constexpr const char *TRACKING_ID = "tracking_id";
namespace bbox
{
constexpr const char *XMIN = "xmin";
constexpr const char *YMIN = "ymin";
constexpr const char *XMAX = "xmax";
constexpr const char *YMAX = "ymax";
} // namespace bbox
} // namespace detection

namespace landmark
{
constexpr const char *POINTS = "points";
constexpr const char *PAIRS = "pairs";
constexpr const char *POINTS_FORMAT = "points_format";
constexpr const char *POINTS_STRIDE = "points_stride";
constexpr const char *POINTS_FORMAT_VALUE = "x,y,conf";
constexpr int POINTS_STRIDE_VALUE = 3;

namespace point
{
constexpr const char *X = "x";
constexpr const char *Y = "y";
constexpr const char *POINT_CONFIDENCE = "confidence";
} // namespace point

namespace pairs
{
constexpr const char *START_POINT = "start_point";
constexpr const char *END_POINT = "end_point";
} // namespace pairs

} // namespace landmark

namespace classification
{
constexpr const char *TYPE = "type";
constexpr const char *LABEL = "label";
constexpr const char *CLASSIFICATION_CONFIDENCE = "confidence";
} // namespace classification

} // namespace analytic_metadata_fields

namespace hailo_analytics::pipeline::codecs
{

nlohmann::json build_metadata_json(BufferPtr data);

/**
 * @brief Output serialization format for analytic metadata
 */
enum class Format
{
    MSGPACK,
    JSON
};

/**
 * @brief Stage for packaging analytic metadata into JSON format for transmission
 *
 * This stage processes AI analytics data (detections, landmarks) from the pipeline
 * and packages them into a structured JSON format. The JSON is then serialized to
 * MessagePack binary format (default) or plain JSON string for efficient transmission.
 * The metadata includes frame information and all detected objects with their
 * coordinates transformed to native frame dimensions.
 *
 * Features:
 * - Converts detections and landmarks to JSON format
 * - Transforms coordinates to native frame space
 * - Supports hierarchical object structures (nested objects)
 * - Optimized with buffer reuse to minimize allocations
 * - Outputs MessagePack binary (default) or JSON string format
 */
class AnalyticMetadataPackagerStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    /**
     * @brief Constructor for AnalyticMetadataPackagerStage
     * @param name Stage name for identification
     * @param format Output serialization format (default: MSGPACK)
     * @param queue_size Size of the processing queue (default: ANALYTIC_METADATA_QUEUE_SIZE_DEFAULT)
     * @param leaky Whether the queue should drop old frames when full (default: false)
     * @param trace_processing_operations Enable tracing for processing operations (default: true)
     */
    AnalyticMetadataPackagerStage(std::string name, Format format = Format::MSGPACK,
                                  size_t queue_size = ANALYTIC_METADATA_QUEUE_SIZE_DEFAULT, bool leaky = false,
                                  bool trace_processing_operations = true);

    /**
     * @brief Process incoming data buffer and package analytic metadata into JSON format
     * @param data Input buffer containing AI analytics data
     * @return AppStatus indicating success or failure of processing
     */
    AppStatus process(BufferPtr data) override;

  private:
    Format m_format;
};

/**
 * @brief Builder pattern implementation for AnalyticMetadataPackagerStage
 *
 * Provides a fluent interface for constructing AnalyticMetadataPackagerStage instances
 * with configurable parameters.
 */
class AnalyticMetadataPackagerStageBuild : public AnalyticMetadataPackagerStage
{
  public:
    /**
     * @brief Builder class for constructing AnalyticMetadataPackagerStage instances
     */
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        Format m_format = Format::MSGPACK;
        size_t m_queue_size = ANALYTIC_METADATA_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        /**
         * @brief Set the stage name
         * @param name Name for the stage
         * @return Builder reference for chaining
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Set the output serialization format
         * @param format Output format (MSGPACK or JSON)
         * @return Builder reference for chaining
         */
        Builder &set_format_opt(Format format);

        /**
         * @brief Set the queue size
         * @param size Size of the processing queue
         * @return Builder reference for chaining
         */
        Builder &set_queue_size_opt(size_t size);

        /**
         * @brief Set the leaky option
         * @param activate If true, queue drops old frames when full
         * @return Builder reference for chaining
         */
        Builder &set_leaky_opt(bool activate);

        /**
         * @brief Set the trace option
         * @param activate If true, enables tracing for processing operations
         * @return Builder reference for chaining
         */
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Build and return shared pointer to AnalyticMetadataPackagerStage
         * @return Shared pointer to constructed AnalyticMetadataPackagerStage
         * @throws std::runtime_error if required parameters are missing
         */
        std::shared_ptr<AnalyticMetadataPackagerStage> buildptr() const;
    };

    /**
     * @brief Create a new Builder instance
     * @return Builder instance for constructing AnalyticMetadataPackagerStage
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::codecs
