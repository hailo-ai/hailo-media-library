#include "hailo_analytics/analytics/tiling.hpp"

#include <tl/expected.hpp>
#include <memory>

#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/ai/ai_stage.hpp"

namespace hailo_analytics::analytics::tiling
{

namespace ai_stages = hailo_analytics::pipeline::ai;
using TrackerBuilder = ai_stages::DetectionTrackerStageBuild::Builder;

// ============================================================================
// tiling_config_t implementation
// ============================================================================

void tiling_config_t::merge_from(const tiling_config_t &other)
{
    if (other.stage_name)
        stage_name = *other.stage_name;
    if (other.output_pool_size)
        output_pool_size = *other.output_pool_size;
    if (other.input_width)
        input_width = *other.input_width;
    if (other.input_height)
        input_height = *other.input_height;
    if (other.output_width)
        output_width = *other.output_width;
    if (other.output_height)
        output_height = *other.output_height;
    if (other.main_sub_name)
        main_sub_name = *other.main_sub_name;
    if (other.sub_sub_name)
        sub_sub_name = *other.sub_sub_name;
    if (other.bbox_tiles)
        bbox_tiles = *other.bbox_tiles;
    if (other.queue_size)
        queue_size = *other.queue_size;
    if (other.leaky)
        leaky = *other.leaky;
    if (other.trace)
        trace = *other.trace;
    if (other.pool_mode)
        pool_mode = *other.pool_mode;
    if (other.crop_every_x_frames)
        crop_every_x_frames = *other.crop_every_x_frames;
}

void tiling_config_t::apply_to(cropping_stages::TilingCropStageBuild::Builder &b) const
{
    if (stage_name)
        b.set_stage_name(*stage_name);
    if (output_pool_size)
        b.set_output_pool_size(*output_pool_size);
    if (input_width)
        b.set_input_width(*input_width);
    if (input_height)
        b.set_input_height(*input_height);
    if (output_width)
        b.set_output_width(*output_width);
    if (output_height)
        b.set_output_height(*output_height);
    if (main_sub_name)
        b.set_main_sub_name(*main_sub_name);
    if (sub_sub_name)
        b.set_sub_sub_name(*sub_sub_name);
    if (bbox_tiles)
        b.set_bbox_tiles(*bbox_tiles);
    if (queue_size)
        b.set_queue_size(*queue_size);
    if (leaky)
        b.set_leaky_opt(*leaky);
    if (trace)
        b.set_trace_opt(*trace);
    if (pool_mode)
        b.set_pool_mode_opt(*pool_mode);
    if (crop_every_x_frames)
        b.set_crop_every_x_frames(*crop_every_x_frames);
}

// ============================================================================
// tracker_config_t implementation
// ============================================================================

void tracker_config_t::merge_from(const tracker_config_t &other)
{
    if (other.enabled)
        enabled = *other.enabled;
    if (other.stage_name)
        stage_name = *other.stage_name;
    if (other.queue_size)
        queue_size = *other.queue_size;
    if (other.leaky)
        leaky = *other.leaky;
    if (other.trace)
        trace = *other.trace;
    if (other.labels_map)
        labels_map = *other.labels_map;
    if (other.max_tracklets)
        max_tracklets = *other.max_tracklets;
    if (other.max_missed_frames)
        max_missed_frames = *other.max_missed_frames;
    if (other.min_confirmed_frames)
        min_confirmed_frames = *other.min_confirmed_frames;
    if (other.aging_threshold)
        aging_threshold = *other.aging_threshold;
    if (other.add_threshold)
        add_threshold = *other.add_threshold;
    if (other.association_threshold)
        association_threshold = *other.association_threshold;
    if (other.iou_weight)
        iou_weight = *other.iou_weight;
    if (other.class_aware_tracking)
        class_aware_tracking = *other.class_aware_tracking;
    if (other.enable_kalman_filter)
        enable_kalman_filter = *other.enable_kalman_filter;
    if (other.position_std_weight)
        position_std_weight = *other.position_std_weight;
    if (other.velocity_std_weight)
        velocity_std_weight = *other.velocity_std_weight;
    if (other.smoothing_alpha)
        smoothing_alpha = *other.smoothing_alpha;
}

void tracker_config_t::apply_to(TrackerBuilder &b) const
{
    if (stage_name)
        b.set_stage_name(*stage_name);
    if (queue_size)
        b.set_queue_size_opt(*queue_size);
    if (leaky)
        b.set_leaky_opt(*leaky);
    if (trace)
        b.set_trace_opt(*trace);
    if (labels_map)
        b.set_labels_map(*labels_map);
    if (max_tracklets)
        b.set_max_tracklets(*max_tracklets);
    if (max_missed_frames)
        b.set_max_missed_frames(*max_missed_frames);
    if (min_confirmed_frames)
        b.set_min_confirmed_frames(*min_confirmed_frames);
    if (aging_threshold)
        b.set_aging_threshold(*aging_threshold);
    if (add_threshold)
        b.set_add_threshold(*add_threshold);
    if (association_threshold)
        b.set_association_threshold(*association_threshold);
    if (iou_weight)
        b.set_iou_weight(*iou_weight);
    if (class_aware_tracking)
        b.set_class_aware_tracking(*class_aware_tracking);
    if (enable_kalman_filter)
        b.set_enable_kalman_filter(*enable_kalman_filter);
    if (position_std_weight)
        b.set_position_std_weight(*position_std_weight);
    if (velocity_std_weight)
        b.set_velocity_std_weight(*velocity_std_weight);
    if (smoothing_alpha)
        b.set_smoothing_alpha(*smoothing_alpha);
}

// ============================================================================
// tiling_detection_config_t implementation
// ============================================================================

void tiling_detection_config_t::merge_from(const tiling_detection_config_t &other)
{
    tiling_config.merge_from(other.tiling_config);
    detection_config.merge_from(other.detection_config);
    aggregator_config.merge_from(other.aggregator_config);
    tracker_config.merge_from(other.tracker_config);
}

void tiling_detection_config_t::apply_to(cropping_stages::TilingCropStageBuild::Builder &b) const
{
    tiling_config.apply_to(b);
}

void tiling_detection_config_t::apply_to(cropping_stages::AggregatorStageBuild::Builder &b) const
{
    aggregator_config.apply_to(b);
}

// ============================================================================
// Pipeline generation functions
// ============================================================================

tiling_detection_config_t base_config()
{
    tiling_detection_config_t config;

    // Set default tiling stage configs
    config.tiling_config.stage_name = std::string(TILING_STAGE);
    config.tiling_config.output_pool_size = 50;
    config.tiling_config.input_width = 1920;
    config.tiling_config.input_height = 1080;
    config.tiling_config.output_width = 640;
    config.tiling_config.output_height = 384;
    config.tiling_config.main_sub_name = std::string(TILING_AGGREGATOR_STAGE);
    config.tiling_config.sub_sub_name = std::string(DETECTION_SUBPIPELINE); // Name of the detection sub-pipeline
    config.tiling_config.bbox_tiles = DEFAULT_TILES;
    config.tiling_config.queue_size = 5;
    config.tiling_config.leaky = true;
    config.tiling_config.trace = true;
    config.tiling_config.pool_mode = hailo_analytics::pipeline::StagePoolMode::BLOCKING;
    // Run detection on every other frame; the tracker fills the gaps with Kalman-predicted detections.
    // Apps that ingest at a low FPS (e.g. dual sensor at 15 FPS) should override this to 1.
    config.tiling_config.crop_every_x_frames = 2;

    // Set default detection configs (use detection base_config as starting point)
    config.detection_config = detection::base_config();

    // Set default aggregator configs
    config.aggregator_config.stage_name = std::string(TILING_AGGREGATOR_STAGE);
    config.aggregator_config.main_inlet_name = std::string(TILING_STAGE);
    config.aggregator_config.main_queue_size = 4;
    config.aggregator_config.main_queue_leaky = false;
    config.aggregator_config.sub_inlet_name =
        std::string(detection::DETECTION_POST_STAGE); // Name of the detection sub-pipeline
    config.aggregator_config.sub_queue_size = 5;
    config.aggregator_config.sub_queue_leaky = false;
    config.aggregator_config.multi_scale = true;
    config.aggregator_config.skip_migration = false;
    config.aggregator_config.trace = true;
    config.aggregator_config.iou_threshold = 0.3f;
    config.aggregator_config.border_threshold = 0.1f;

    return config;
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_tiling_detection_pipeline(const std::string &pipeline_name,
                                   std::optional<tiling_detection_config_t> user_configs)
{
    tiling_detection_config_t cfg = base_config(); // default configs
    if (user_configs.has_value())
    {
        cfg.merge_from(user_configs.value()); // user overrides
    }
    if (cfg.tracker_config.enabled.value_or(false))
    {
        // The tracker needs to know which frames were AI-processed, so preserve
        // sub-frame tensor metadata on the aggregator output.
        cfg.aggregator_config.copy_sub_frame_tensor_to_metadata = true;
    }

    // Create pipeline builder
    hailo_analytics::pipeline::PipelineBuilder pip_builder;

    // Create tiling stage
    cropping_stages::TilingCropStageBuild::Builder tiling_builder = cropping_stages::TilingCropStageBuild::create();
    cfg.apply_to(tiling_builder);
    std::shared_ptr<cropping_stages::TilingCropStage> tiling_stage = tiling_builder.buildptr();

    // Generate detection sub-pipeline using the detection generator
    auto detection_pipeline_result =
        detection::generate_detection_pipeline(std::string(DETECTION_SUBPIPELINE), cfg.detection_config);

    if (!detection_pipeline_result.has_value())
    {
        return tl::unexpected(detection_pipeline_result.error());
    }

    hailo_analytics::pipeline::PipelinePtr detection_pipeline = detection_pipeline_result.value();

    // Create aggregator stage
    cropping_stages::AggregatorStageBuild::Builder aggregator_builder = cropping_stages::AggregatorStageBuild::create();
    cfg.apply_to(aggregator_builder);
    std::shared_ptr<cropping_stages::AggregatorStage> aggregator_stage = aggregator_builder.buildptr();

    // Add stages to pipeline builder
    pip_builder.add_stage(tiling_stage).add_stage(detection_pipeline).add_stage(aggregator_stage);

    // Connect the stages within the pipeline
    // Tiling main output -> Aggregator main input
    pip_builder.connect(cfg.tiling_config.stage_name.value(), cfg.aggregator_config.stage_name.value());

    // Tiling crops -> Detection Pipeline
    pip_builder.connect(cfg.tiling_config.stage_name.value(), detection_pipeline->get_name());

    // Detection Pipeline -> Aggregator sub input
    pip_builder.connect(detection_pipeline->get_name(), cfg.aggregator_config.stage_name.value());

    // Optionally add detection tracker stage after the aggregator
    std::shared_ptr<ai_stages::DetectionTrackerStage> tracker_stage = nullptr;
    if (cfg.tracker_config.enabled.value_or(false))
    {
        auto tracker_builder = ai_stages::DetectionTrackerStageBuild::create();
        if (!cfg.tracker_config.stage_name)
        {
            tracker_builder.set_stage_name(std::string(TRACKER_STAGE));
        }
        cfg.tracker_config.apply_to(tracker_builder);
        tracker_stage = tracker_builder.buildptr();

        pip_builder.add_stage(tracker_stage);
        pip_builder.connect(cfg.aggregator_config.stage_name.value(), tracker_stage->get_name());
    }

    // Create the pipeline
    hailo_analytics::pipeline::PipelinePtr pipeline = pip_builder.build(pipeline_name, true);

    // Set the input and output stages
    pipeline->set_in_stage(tiling_stage);
    if (tracker_stage)
    {
        pipeline->set_out_stage(tracker_stage);
    }
    else
    {
        pipeline->set_out_stage(aggregator_stage);
    }

    return pipeline;
}

} // namespace hailo_analytics::analytics::tiling
