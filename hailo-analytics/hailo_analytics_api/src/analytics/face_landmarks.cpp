#include "hailo_analytics/analytics/face_landmarks.hpp"

#include <hailort.h>
#include <stdint.h>
#include <tl/expected.hpp>
#include <chrono>
#include <memory>

#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/ai/ai_stage.hpp"
#include "hailo_analytics/pipeline/ai/postprocess_stage.hpp"

namespace hailo_analytics::analytics::face_landmarks
{

namespace ai_stages = hailo_analytics::pipeline::ai;
namespace cropping_stages = hailo_analytics::pipeline::cropping;

// ============================================================================
// bbox_crop_config_t implementation
// ============================================================================

void bbox_crop_config_t::merge_from(const bbox_crop_config_t &other)
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
    if (other.labels)
        labels = *other.labels;
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
    if (other.release_input_after_dsp)
        release_input_after_dsp = *other.release_input_after_dsp;
}

void bbox_crop_config_t::apply_to(cropping_stages::BBoxCropStageBuild::Builder &b) const
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
    if (labels)
        b.set_labels(*labels);
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
    if (release_input_after_dsp)
        b.set_release_input_after_dsp(*release_input_after_dsp);
}

// ============================================================================
// bbox_crop_landmarks_config_t implementation
// ============================================================================

void bbox_crop_landmarks_config_t::merge_from(const bbox_crop_landmarks_config_t &other)
{
    bbox_crop_config.merge_from(other.bbox_crop_config);
    landmarks_config.merge_from(other.landmarks_config);
    aggregator_config.merge_from(other.aggregator_config);
}

void bbox_crop_landmarks_config_t::apply_to(cropping_stages::BBoxCropStageBuild::Builder &b) const
{
    bbox_crop_config.apply_to(b);
}

void bbox_crop_landmarks_config_t::apply_to(cropping_stages::AggregatorStageBuild::Builder &b) const
{
    aggregator_config.apply_to(b);
}

// ============================================================================
// face_landmarks_config_t implementation
// ============================================================================

face_landmarks_config_t face_landmarks_base_config()
{
    face_landmarks_config_t config;

    // Set default AI stage configs
    config.ai_config.stage_name = std::string(LANDMARKS_STAGE);
    config.ai_config.queue_size = 100;
    config.ai_config.output_pool_size = 201;
    config.ai_config.group_id = std::string(LANDMARKS_GROUP_ID);
    config.ai_config.batch_size = 60;
    config.ai_config.job_limit = 60;
    config.ai_config.scheduler_threshold = 60;
    config.ai_config.dynamic_threshold = true;
    config.ai_config.scheduler_timeout = std::chrono::milliseconds(100);
    config.ai_config.scheduler_priority = HAILO_SCHEDULER_PRIORITY_MAX - 2;
    config.ai_config.pool_mode = hailo_analytics::pipeline::StagePoolMode::BLOCKING;
    config.ai_config.trace = true;

    // Set default postprocess configs (matching ai_pipeline_builder.cpp lines 170-178)
    config.post_config.stage_name = std::string(LANDMARKS_POST_STAGE);
    config.post_config.so_path = std::string(LANDMARKS_POST_SO);
    config.post_config.function_name = std::string(LANDMARKS_POST_FUNCTION);
    config.post_config.config_path = std::string(LANDMARKS_POST_CONF);
    config.post_config.queue_size = 100;
    config.post_config.leaky = false;
    config.post_config.trace = true;

    return config;
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_face_landmarks_pipeline(const std::string &pipeline_name, std::optional<face_landmarks_config_t> user_configs)
{
    face_landmarks_config_t cfg =
        hailo_analytics::analytics::face_landmarks::face_landmarks_base_config(); // default configs
    if (user_configs.has_value())
    {
        cfg.merge_from(user_configs.value()); // user overrides
    }

    // create pipeline builder
    hailo_analytics::pipeline::PipelineBuilder pip_builder;

    // create stages
    ai_stages::HailortAsyncStageBuild::Builder ai_stage_builder = ai_stages::HailortAsyncStageBuild::create();
    ai_stages::PostprocessStageBuild::Builder post_stage_builder = ai_stages::PostprocessStageBuild::create();
    // set the config params
    cfg.apply_to(ai_stage_builder);
    cfg.apply_to(post_stage_builder);
    // build the stages
    std::shared_ptr<ai_stages::HailortAsyncStage> landmarks_stage = ai_stage_builder.buildptr();
    std::shared_ptr<ai_stages::PostprocessStage> landmarks_post_stage = post_stage_builder.buildptr();

    // add stages to pipeline builder
    pip_builder.add_stage(landmarks_stage).add_stage(landmarks_post_stage);

    // connect the stages withing the pipeline
    pip_builder.connect(cfg.ai_config.stage_name.value(), cfg.post_config.stage_name.value());

    // create the pipeline
    hailo_analytics::pipeline::PipelinePtr pipeline = pip_builder.build(pipeline_name, true);

    // set the input and output stages
    pipeline->set_in_stage(landmarks_stage);
    pipeline->set_out_stage(landmarks_post_stage);

    return pipeline;
}

// ============================================================================
// Pipeline generation functions for bbox crop + landmarks
// ============================================================================

bbox_crop_landmarks_config_t base_config()
{
    bbox_crop_landmarks_config_t config;

    // Set default bbox crop stage configs (from ai_pipeline_builder.cpp lines 137-151)
    config.bbox_crop_config.stage_name = std::string(BBOX_CROP_STAGE);
    config.bbox_crop_config.output_pool_size = 150;
    config.bbox_crop_config.input_width = 1920; // Default FHD input
    config.bbox_crop_config.input_height = 1080;
    config.bbox_crop_config.output_width = 192;
    config.bbox_crop_config.output_height = 192;
    config.bbox_crop_config.main_sub_name = std::string(LANDMARKS_AGGREGATOR_STAGE);
    config.bbox_crop_config.sub_sub_name = std::string(LANDMARKS_SUBPIPELINE); // Name of the landmarks sub-pipeline
    config.bbox_crop_config.labels = std::vector<std::string>{"face"};
    config.bbox_crop_config.queue_size = 5;
    config.bbox_crop_config.leaky = false;
    config.bbox_crop_config.trace = true;
    config.bbox_crop_config.pool_mode = hailo_analytics::pipeline::StagePoolMode::BLOCKING;
    config.bbox_crop_config.crop_every_x_frames = 1;
    // Drop the FHD ref once DSP crops are done to save memory.
    config.bbox_crop_config.release_input_after_dsp = true;

    // Set default landmarks configs (use face_landmarks base_config as starting point)
    config.landmarks_config = face_landmarks_base_config();

    // Set default aggregator configs (from ai_pipeline_builder.cpp lines 180-193)
    config.aggregator_config.stage_name = std::string(LANDMARKS_AGGREGATOR_STAGE);
    config.aggregator_config.main_inlet_name = std::string(BBOX_CROP_STAGE);
    config.aggregator_config.main_queue_size = 3;
    config.aggregator_config.main_queue_leaky = false;
    config.aggregator_config.sub_inlet_name = std::string(LANDMARKS_POST_STAGE); // Name of the landmarks post stage
    config.aggregator_config.sub_queue_size = 100;
    config.aggregator_config.sub_queue_leaky = false;
    config.aggregator_config.multi_scale = false;
    config.aggregator_config.skip_migration = false;
    config.aggregator_config.trace = true;
    config.aggregator_config.iou_threshold = 0.3f;
    config.aggregator_config.border_threshold = 0.1f;

    return config;
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_bbox_landmarks_pipeline(const std::string &pipeline_name,
                                 std::optional<bbox_crop_landmarks_config_t> user_configs)
{
    bbox_crop_landmarks_config_t cfg = base_config(); // default configs
    if (user_configs.has_value())
    {
        cfg.merge_from(user_configs.value()); // user overrides
    }

    // Create pipeline builder
    hailo_analytics::pipeline::PipelineBuilder pip_builder;

    // Create bbox crop stage
    cropping_stages::BBoxCropStageBuild::Builder bbox_crop_builder = cropping_stages::BBoxCropStageBuild::create();
    cfg.apply_to(bbox_crop_builder);
    std::shared_ptr<cropping_stages::BBoxCropStage> bbox_crop_stage = bbox_crop_builder.buildptr();

    // Generate face landmarks sub-pipeline using the landmarks generator
    auto landmarks_pipeline_result =
        generate_face_landmarks_pipeline(std::string(LANDMARKS_SUBPIPELINE), cfg.landmarks_config);

    if (!landmarks_pipeline_result.has_value())
    {
        return tl::unexpected(landmarks_pipeline_result.error());
    }

    hailo_analytics::pipeline::PipelinePtr landmarks_pipeline = landmarks_pipeline_result.value();

    // Create aggregator stage
    cropping_stages::AggregatorStageBuild::Builder aggregator_builder = cropping_stages::AggregatorStageBuild::create();
    cfg.apply_to(aggregator_builder);
    std::shared_ptr<cropping_stages::AggregatorStage> aggregator_stage = aggregator_builder.buildptr();

    // Add stages to pipeline builder
    pip_builder.add_stage(bbox_crop_stage).add_stage(landmarks_pipeline).add_stage(aggregator_stage);

    // Connect the stages within the pipeline
    // BBox crop main output -> Aggregator main input
    pip_builder.connect(cfg.bbox_crop_config.stage_name.value(), cfg.aggregator_config.stage_name.value());

    // BBox crops -> Landmarks Pipeline
    pip_builder.connect(cfg.bbox_crop_config.stage_name.value(), landmarks_pipeline->get_name());

    // Landmarks Pipeline -> Aggregator sub input
    pip_builder.connect(landmarks_pipeline->get_name(), cfg.aggregator_config.stage_name.value());

    // Create the pipeline
    hailo_analytics::pipeline::PipelinePtr pipeline = pip_builder.build(pipeline_name, true);

    // Set the input and output stages
    pipeline->set_in_stage(bbox_crop_stage);
    pipeline->set_out_stage(aggregator_stage);

    return pipeline;
}

} // namespace hailo_analytics::analytics::face_landmarks
