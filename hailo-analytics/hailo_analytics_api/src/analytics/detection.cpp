#include "hailo_analytics/analytics/detection.hpp"

#include <hailort.h>
#include <stddef.h>
#include <stdint.h>
#include <tl/expected.hpp>
#include <chrono>
#include <memory>

#include "hailo_analytics/pipeline/ai/ai_stage.hpp"
#include "hailo_analytics/pipeline/ai/postprocess_stage.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"

namespace hailo_analytics::analytics::detection
{

namespace ai_stages = hailo_analytics::pipeline::ai;

detection_config_t base_config()
{
    detection_config_t config;

    // Set default AI stage configs
    config.ai_config.stage_name = std::string(DETECTION_STAGE);
    config.ai_config.queue_size = 5;
    config.ai_config.output_pool_size = 50;
    config.ai_config.group_id = std::string(DETECTION_GROUP_ID);
    config.ai_config.batch_size = 5;
    config.ai_config.job_limit = 10;
    config.ai_config.scheduler_threshold = 5;
    config.ai_config.dynamic_threshold = false;
    config.ai_config.scheduler_timeout = std::chrono::milliseconds(100);
    config.ai_config.scheduler_priority = HAILO_SCHEDULER_PRIORITY_MAX - 1;
    config.ai_config.pool_mode = hailo_analytics::pipeline::StagePoolMode::BLOCKING;
    config.ai_config.trace = true;

    // Set default postprocess configs
    config.post_config.stage_name = std::string(DETECTION_POST_STAGE);
    config.post_config.so_path = std::string(DETECTION_POST_SO);
    config.post_config.queue_size = 5;
    config.post_config.leaky = false;
    config.post_config.trace = true;

    return config;
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> generate_detection_pipeline(
    const std::string &pipeline_name, std::optional<detection_config_t> user_configs)
{
    detection_config_t cfg = hailo_analytics::analytics::detection::base_config(); // default configs
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
    std::shared_ptr<ai_stages::HailortAsyncStage> detection_stage = ai_stage_builder.buildptr();
    std::shared_ptr<ai_stages::PostprocessStage> detection_post_stage = post_stage_builder.buildptr();

    // add stages to pipeline builder
    pip_builder.add_stage(detection_stage).add_stage(detection_post_stage);

    // connect the stages withing the pipeline
    pip_builder.connect(cfg.ai_config.stage_name.value(), cfg.post_config.stage_name.value());

    // create the pipeline
    hailo_analytics::pipeline::PipelinePtr pipeline = pip_builder.build(pipeline_name, true);

    // set the input and output stages
    pipeline->set_in_stage(detection_stage);
    pipeline->set_out_stage(detection_post_stage);

    return pipeline;
}

} // namespace hailo_analytics::analytics::detection
