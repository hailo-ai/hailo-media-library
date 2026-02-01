#pragma once

#include <cstdint>
#include <vector>
#include <nlohmann/json.hpp>
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"

#define ANALYTIC_METADATA_QUEUE_SIZE_DEFAULT (20)

namespace analytic_metadata_fields
{

constexpr const char *ISP_TIMESTAMP = "isp_timestamp_ns";
constexpr const char *FRAME_WIDTH = "frame_width";
constexpr const char *FRAME_HEIGHT = "frame_height";
constexpr const char *DETECTIONS = "detections";
constexpr const char *LANDMARKS = "landmarks";

namespace detection
{
constexpr const char *LABEL = "label";
constexpr const char *CONFIDENCE = "confidence";
constexpr const char *BBOX = "bbox";
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
constexpr const char *CONFIDENCE = "confidence";
} // namespace point

namespace pairs
{
constexpr const char *START_POINT = "start_point";
constexpr const char *END_POINT = "end_point";
} // namespace pairs

} // namespace landmark

} // namespace analytic_metadata_fields

namespace hailo_analytics::pipeline::codecs
{

class AnalyticMetadataPackagerStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    nlohmann::json &process_detection(HailoDetectionPtr detection, const HailoBBox &roi_bbox, uint32_t native_width,
                                      uint32_t native_height, nlohmann::json &metadata_json);
    nlohmann::json &process_landmarks(HailoLandmarksPtr landmarks, const HailoBBox &roi_bbox, uint32_t native_width,
                                      uint32_t native_height, nlohmann::json &metadata_json);
    void process_objects_recursive(HailoROIPtr roi, const HailoBBox &parent_bbox, uint32_t native_width,
                                   uint32_t native_height, nlohmann::json &metadata_json);

    std::vector<float> m_points_buffer;
    std::vector<int> m_pairs_buffer;

  public:
    AnalyticMetadataPackagerStage(std::string name, size_t queue_size = ANALYTIC_METADATA_QUEUE_SIZE_DEFAULT,
                                  bool leaky = false, bool trace_processing_operations = true);
    AppStatus init() override;
    AppStatus deinit() override;
    inline void loop() override;
};

class AnalyticMetadataPackagerStageBuild : public AnalyticMetadataPackagerStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = ANALYTIC_METADATA_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_trace_opt(bool activate);

        std::shared_ptr<AnalyticMetadataPackagerStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::codecs
