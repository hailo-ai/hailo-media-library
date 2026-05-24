#include "hailo_analytics/analytics/dynamic_privacy_mask.hpp"

#include <hailort.h>
#include <stdint.h>
#include <expected.hpp>
#include <tl/expected.hpp>
#include <chrono>
#include <memory>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/utils/platform_utils.hpp"
#include "hailo/hef.hpp"
#include "hailo_analytics/pipeline/ai/ai_stage.hpp"
#include "hailo_analytics/pipeline/ai/postprocess_stage.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"

namespace hailo_analytics::analytics::dynamic_privacy_mask
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
    if (other.use_letterbox)
        use_letterbox = *other.use_letterbox;
    if (other.letterbox_alignment)
        letterbox_alignment = *other.letterbox_alignment;
    if (other.letterbox_color)
        letterbox_color = *other.letterbox_color;
    if (other.max_crops)
        max_crops = *other.max_crops;
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
    if (use_letterbox && *use_letterbox)
    {
        dsp_letterbox_alignment_t alignment = letterbox_alignment.value_or(DSP_LETTERBOX_MIDDLE);
        dsp_color_t color = letterbox_color.value_or(dsp_color_t{.y = 0, .u = 128, .v = 128});
        b.set_letterbox_opt(alignment, color);
    }
    if (max_crops)
        b.set_max_crops(*max_crops);
}

// ============================================================================
// bbox_crop_segmentation_config_t implementation
// ============================================================================

void bbox_crop_segmentation_config_t::merge_from(const bbox_crop_segmentation_config_t &other)
{
    bbox_crop_config.merge_from(other.bbox_crop_config);
    segmentation_config.merge_from(other.segmentation_config);
    aggregator_config.merge_from(other.aggregator_config);
}

void bbox_crop_segmentation_config_t::apply_to(cropping_stages::BBoxCropStageBuild::Builder &b) const
{
    bbox_crop_config.apply_to(b);
}

void bbox_crop_segmentation_config_t::apply_to(cropping_stages::AggregatorStageBuild::Builder &b) const
{
    aggregator_config.apply_to(b);
}

// ============================================================================
// segmentation_config_t implementation
// ============================================================================

segmentation_config_t segmentation_base_config()
{
    segmentation_config_t config;

    // Set default AI stage configs
    config.ai_config.stage_name = std::string(SEGMENTATION_STAGE);
    config.ai_config.queue_size = 100;

    utils::Architecture arch = utils::get_hailo_architecture();
    if (arch == utils::Architecture::Hailo15H)
    {
        config.ai_config.output_pool_size = 400;
        config.ai_config.batch_size = 60;
        config.ai_config.job_limit = 60;
        config.ai_config.scheduler_threshold = 60;
    }
    else
    {
        config.ai_config.output_pool_size = 250;
        config.ai_config.batch_size = 6;
        config.ai_config.job_limit = 6;
        config.ai_config.scheduler_threshold = 6;
    }

    config.ai_config.group_id = std::string(SEGMENTATION_GROUP_ID);
    config.ai_config.dynamic_threshold = true;
    config.ai_config.scheduler_timeout = std::chrono::milliseconds(100);
    config.ai_config.scheduler_priority = HAILO_SCHEDULER_PRIORITY_MAX - 2;
    config.ai_config.pool_mode = hailo_analytics::pipeline::StagePoolMode::BLOCKING;
    config.ai_config.trace = true;

    // Set default postprocess configs
    config.post_config.stage_name = std::string(SEGMENTATION_POST_STAGE);
    config.post_config.so_path = std::string(SEGMENTATION_POST_SO);
    config.post_config.function_name = std::string(SEGMENTATION_POST_FUNCTION);
    config.post_config.config_path = std::string(SEGMENTATION_POST_CONF);
    config.post_config.queue_size = 100;
    config.post_config.leaky = false;
    config.post_config.trace = true;

    return config;
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_segmentation_pipeline(const std::string &pipeline_name, std::optional<segmentation_config_t> user_configs)
{
    segmentation_config_t cfg = segmentation_base_config(); // default configs
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
    std::shared_ptr<ai_stages::HailortAsyncStage> segmentation_stage = ai_stage_builder.buildptr();
    std::shared_ptr<ai_stages::PostprocessStage> segmentation_post_stage = post_stage_builder.buildptr();

    // add stages to pipeline builder
    pip_builder.add_stage(segmentation_stage).add_stage(segmentation_post_stage);

    // connect the stages within the pipeline
    pip_builder.connect(cfg.ai_config.stage_name.value(), cfg.post_config.stage_name.value());

    // create the pipeline
    hailo_analytics::pipeline::PipelinePtr pipeline = pip_builder.build(pipeline_name, true);

    // set the input and output stages
    pipeline->set_in_stage(segmentation_stage);
    pipeline->set_out_stage(segmentation_post_stage);

    return pipeline;
}

// ============================================================================
// Pipeline generation functions for bbox crop + segmentation
// ============================================================================

bbox_crop_segmentation_config_t base_config()
{
    bbox_crop_segmentation_config_t config;

    config.bbox_crop_config.stage_name = std::string(SEGMENTOR_STAGE);
    config.bbox_crop_config.output_pool_size = 160;
    config.bbox_crop_config.input_width = 1920; // Default FHD input
    config.bbox_crop_config.input_height = 1080;
    // output_width/output_height are auto-detected from the segmentation HEF input shape
    // in generate_dynamic_privacy_mask_pipeline()
    config.bbox_crop_config.main_sub_name = std::string(SEGMENTATION_AGGREGATOR_STAGE);
    config.bbox_crop_config.sub_sub_name = std::string(SEGMENTATION_SUBPIPELINE);
    config.bbox_crop_config.labels = std::vector<std::string>{"person", "face"};
    config.bbox_crop_config.queue_size = 5;
    config.bbox_crop_config.leaky = false;
    config.bbox_crop_config.trace = true;
    config.bbox_crop_config.pool_mode = hailo_analytics::pipeline::StagePoolMode::BLOCKING;
    config.bbox_crop_config.crop_every_x_frames = 1;
    config.bbox_crop_config.use_letterbox = true;
    config.bbox_crop_config.letterbox_alignment = DSP_LETTERBOX_MIDDLE;
    config.bbox_crop_config.letterbox_color = dsp_color_t{.y = 0, .u = 128, .v = 128};
    config.bbox_crop_config.max_crops = 15;

    // Set default segmentation configs
    config.segmentation_config = segmentation_base_config();

    // Set default aggregator configs
    config.aggregator_config.stage_name = std::string(SEGMENTATION_AGGREGATOR_STAGE);
    config.aggregator_config.main_inlet_name = std::string(SEGMENTOR_STAGE);
    config.aggregator_config.main_queue_size = 3;
    config.aggregator_config.main_queue_leaky = false;
    config.aggregator_config.sub_inlet_name = std::string(SEGMENTATION_POST_STAGE);
    config.aggregator_config.sub_queue_size = 100;
    config.aggregator_config.sub_queue_leaky = false;
    config.aggregator_config.multi_scale = false;
    config.aggregator_config.skip_migration = false;
    config.aggregator_config.trace = true;
    config.aggregator_config.iou_threshold = 0.99f;
    config.aggregator_config.border_threshold = 0.1f;
    config.aggregator_config.copy_sub_frame_tensor_to_metadata = true;

    return config;
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_dynamic_privacy_mask_pipeline(const std::string &pipeline_name,
                                       std::optional<bbox_crop_segmentation_config_t> user_configs)
{
    bbox_crop_segmentation_config_t cfg = base_config(); // default configs
    if (user_configs.has_value())
    {
        cfg.merge_from(user_configs.value()); // user overrides
    }

    // Parse the segmentation HEF to get the model's expected input dimensions
    // and use them for the bbox crop output size
    if (!cfg.segmentation_config.ai_config.hef_path.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Segmentation hef_path is required but was not set by the caller");
        return tl::unexpected(hailo_analytics::pipeline::AppStatus::CONFIGURATION_ERROR);
    }
    const std::string &hef_path = cfg.segmentation_config.ai_config.hef_path.value();
    auto hef_expected = hailort::Hef::create(hef_path);
    if (!hef_expected)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to parse HEF file '{}', status = {}", hef_path, hef_expected.status());
        return tl::unexpected(hailo_analytics::pipeline::AppStatus::HAILORT_ERROR);
    }
    auto input_vstream_infos = hef_expected->get_input_vstream_infos();
    if (!input_vstream_infos || input_vstream_infos->empty())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get input vstream infos from HEF '{}'", hef_path);
        return tl::unexpected(hailo_analytics::pipeline::AppStatus::HAILORT_ERROR);
    }
    const auto &input_info = input_vstream_infos->front();
    const auto &input_shape = input_info.shape;
    uint32_t pixel_width = input_shape.width;
    // NV12 shape reports tensor dimensions (H*1.5/features, W, features),
    // convert back to pixel height
    uint32_t pixel_height = (input_shape.height * input_shape.features * 2) / 3;
    cfg.bbox_crop_config.output_width = static_cast<int>(pixel_width);
    cfg.bbox_crop_config.output_height = static_cast<int>(pixel_height);
    HAILO_ANALYTICS_LOG_INFO("Segmentation HEF input shape: {}x{} pixels (from '{}')", pixel_width, pixel_height,
                             hef_path);

    // Create pipeline builder
    hailo_analytics::pipeline::PipelineBuilder pip_builder;

    cropping_stages::BBoxCropStageBuild::Builder segmentor_builder = cropping_stages::BBoxCropStageBuild::create();
    cfg.apply_to(segmentor_builder);
    std::shared_ptr<cropping_stages::BBoxCropStage> segmentor = segmentor_builder.buildptr();

    // Generate segmentation sub-pipeline using the segmentation generator
    auto segmentation_pipeline_result =
        generate_segmentation_pipeline(std::string(SEGMENTATION_SUBPIPELINE), cfg.segmentation_config);

    if (!segmentation_pipeline_result.has_value())
    {
        return tl::unexpected(segmentation_pipeline_result.error());
    }

    hailo_analytics::pipeline::PipelinePtr segmentation_pipeline = segmentation_pipeline_result.value();

    // Create aggregator stage
    cropping_stages::AggregatorStageBuild::Builder aggregator_builder = cropping_stages::AggregatorStageBuild::create();
    cfg.apply_to(aggregator_builder);
    std::shared_ptr<cropping_stages::AggregatorStage> aggregator_stage = aggregator_builder.buildptr();

    // Add stages to pipeline builder
    pip_builder.add_stage(segmentor).add_stage(segmentation_pipeline).add_stage(aggregator_stage);

    pip_builder.connect(cfg.bbox_crop_config.stage_name.value(), cfg.aggregator_config.stage_name.value());
    pip_builder.connect(cfg.bbox_crop_config.stage_name.value(), segmentation_pipeline->get_name());
    pip_builder.connect(segmentation_pipeline->get_name(), cfg.aggregator_config.stage_name.value());

    // Create the pipeline
    hailo_analytics::pipeline::PipelinePtr pipeline = pip_builder.build(pipeline_name, true);

    // Set the input and output stages
    pipeline->set_in_stage(segmentor);
    pipeline->set_out_stage(aggregator_stage);

    return pipeline;
}

} // namespace hailo_analytics::analytics::dynamic_privacy_mask
