#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <unordered_set>

// Postprocess Tools includes
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// Pipeline infra includes
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/ai/ai_stage.hpp"
#include "hailo_analytics/pipeline/cropping/tiling_stage.hpp"
#include "hailo_analytics/pipeline/cropping/bbox_crop_stage.hpp"
#include "hailo_analytics/pipeline/ai/postprocess_stage.hpp"
#include "hailo_analytics/pipeline/overlay/overlay_stage.hpp"
#include "hailo_analytics/pipeline/ai/lightweight_tracker_stage.hpp"
#include "hailo_analytics/pipeline/cropping/aggregator_stage.hpp"
#include "hailo_analytics/pipeline/cropping/sync_aggregator_stage.hpp"
#include "hailo_analytics/pipeline/muxing/muxer_stage.hpp"
#include "hailo_analytics/pipeline/muxing/demuxer_stage.hpp"
#include "hailo_analytics/pipeline/routing/tee_stage.hpp"
#include "hailo_analytics/pipeline/routing/callback_stage.hpp"

namespace ai_example_app
{
/**
 * @brief Configuration parameters for the AI pipeline
 */
struct AIPipelineConfig
{
    // General params
    bool print_fps = false;
    bool print_latency = false;

    // Input resolution (from frontend)
    int input_width = 0;
    int input_height = 0;

    // Tiling params
    int tiling_input_width = 1920;
    int tiling_input_height = 1080;
    int tiling_output_width = 640;
    int tiling_output_height = 384;
    std::vector<HailoBBox> tiles;

    // Detection AI params
    std::string yolo_hef_file;
    std::string yolo_post_so;
    std::string yolo_func_name;
    std::string yolo_post_conf;

    // Bbox crop params
    std::vector<std::string> bbox_crop_labels = {"face"};
    int bbox_crop_output_width = 192;
    int bbox_crop_output_height = 192;

    // Landmarks AI params
    std::string landmarks_hef_file;
    std::string landmarks_post_so;
    std::string landmarks_func_name;

    // Overlay params
    bool draw_overlay_sink0 = false;
    bool draw_overlay_sink1 = false;
    bool full_landmarks = false;
    std::unordered_set<size_t> landmark_indices_to_draw;

    // Stage names (allow customization if needed)
    std::string muxer_stage_name = "muxer";
    std::string callback_stage_name = "callback_stage";
    std::string tiling_stage_name = "tiling";
    std::string detection_ai_stage_name = "yolo_detection";
    std::string post_stage_name = "yolo_post";
    std::string detection_aggregator_name = "detection_aggregator";
    std::string stage_1_aggregator_name = "stage_1_aggregator";
    std::string tee_stage_name = "vision_tee";
    std::string bbox_crop_stage_name = "bbox_crops";
    std::string landmarks_ai_stage_name = "face_landmarks";
    std::string landmarks_post_stage_name = "landmarks_post";
    std::string landmarks_aggregator_name = "landmarks_aggregator";
    std::string stage_2_aggregator_name = "stage_2_aggregator";
    std::string tracker_stage_name = "tracker";
    std::string demux_stage_name = "demuxer";
    std::string overlay_stage_sink0_name = "overlay_sink0";
    std::string overlay_stage_sink1_name = "overlay_sink1";

    // Stream IDs
    std::string vision_sink = "sink0";
    std::string secondary_vision_sink = "sink1";
    std::string ai_sink = "sink2";
};

/**
 * @brief Container for all created AI pipeline stages
 */
struct AIPipelineStages
{
    std::shared_ptr<hailo_analytics::pipeline::muxing::MuxerStage> muxer;
    std::shared_ptr<hailo_analytics::pipeline::routing::CallbackStage> callback;
    std::shared_ptr<hailo_analytics::pipeline::cropping::TilingCropStage> tiling;
    std::shared_ptr<hailo_analytics::pipeline::ai::HailortAsyncStage> detection_ai;
    std::shared_ptr<hailo_analytics::pipeline::ai::PostprocessStage> detection_post;
    std::shared_ptr<hailo_analytics::pipeline::cropping::AggregatorStage> detection_aggregator;
    std::shared_ptr<hailo_analytics::pipeline::cropping::SyncAggregatorStage> stage_1_aggregator;
    std::shared_ptr<hailo_analytics::pipeline::routing::TeeStage> tee;
    std::shared_ptr<hailo_analytics::pipeline::cropping::BBoxCropStage> bbox_crop;
    std::shared_ptr<hailo_analytics::pipeline::ai::HailortAsyncStage> landmarks_ai;
    std::shared_ptr<hailo_analytics::pipeline::ai::PostprocessStage> landmarks_post;
    std::shared_ptr<hailo_analytics::pipeline::cropping::AggregatorStage> landmarks_aggregator;
    std::shared_ptr<hailo_analytics::pipeline::cropping::SyncAggregatorStage> stage_2_aggregator;
    std::shared_ptr<hailo_analytics::pipeline::ai::LightweightTrackerStage> tracker;
    std::shared_ptr<hailo_analytics::pipeline::muxing::DemuxerStage> demuxer;
    std::shared_ptr<hailo_analytics::pipeline::overlay::OverlayStage> overlay_sink0;
    std::shared_ptr<hailo_analytics::pipeline::overlay::OverlayStage> overlay_sink1;
};

/**
 * @brief Builder class for creating reusable AI pipelines
 *
 * This class provides methods to create and configure AI pipeline stages
 * that can be shared across multiple applications.
 */
class AIPipelineBuilder
{
  public:
    /**
     * @brief Create all AI pipeline stages based on the provided configuration
     *
     * @param config Configuration parameters for the AI pipeline
     * @return AIPipelineStages Container with all created stages
     */
    static AIPipelineStages create_ai_stages(const AIPipelineConfig &config);

    /**
     * @brief Add all AI pipeline stages to a PipelineBuilder
     *
     * @param pip_builder Reference to the PipelineBuilder to add stages to
     * @param stages Container with all created AI pipeline stages
     */
    static void add_stages_to_pipeline(hailo_analytics::pipeline::PipelineBuilder &pip_builder,
                                       const AIPipelineStages &stages);

    /**
     * @brief Connect all AI pipeline stages together
     *
     * @param pip_builder Reference to the PipelineBuilder to connect stages in
     * @param stages Container with all created AI pipeline stages
     * @param config Configuration with stage names and stream IDs
     */
    static void connect_ai_stages(hailo_analytics::pipeline::PipelineBuilder &pip_builder,
                                  const AIPipelineStages &stages, const AIPipelineConfig &config);

    /**
     * @brief Set the callback for the callback stage
     *
     * @param stages Container with all created AI pipeline stages
     * @param callback Callback function to set
     */
    static void set_callback_function(const AIPipelineStages &stages,
                                      std::function<void(hailo_analytics::pipeline::BufferPtr)> callback);
};
} // namespace ai_example_app
