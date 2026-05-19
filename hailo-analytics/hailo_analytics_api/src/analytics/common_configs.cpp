#include "hailo_analytics/analytics/common_configs.hpp"

namespace hailo_analytics::analytics
{

namespace cropping_stages = hailo_analytics::pipeline::cropping;

// ============================================================================
// ai_stage_config_t implementation
// ============================================================================

void ai_stage_config_t::merge_from(const ai_stage_config_t &other)
{
    if (other.hef_path)
        hef_path = *other.hef_path;
    if (other.stage_name)
        stage_name = *other.stage_name;
    if (other.queue_size)
        queue_size = *other.queue_size;
    if (other.output_pool_size)
        output_pool_size = *other.output_pool_size;
    if (other.group_id)
        group_id = *other.group_id;
    if (other.batch_size)
        batch_size = *other.batch_size;
    if (other.job_limit)
        job_limit = *other.job_limit;
    if (other.scheduler_threshold)
        scheduler_threshold = *other.scheduler_threshold;
    if (other.dynamic_threshold)
        dynamic_threshold = *other.dynamic_threshold;
    if (other.scheduler_timeout)
        scheduler_timeout = *other.scheduler_timeout;
    if (other.scheduler_priority)
        scheduler_priority = *other.scheduler_priority;
    if (other.pool_mode)
        pool_mode = *other.pool_mode;
    if (other.nms_score_threshold)
        nms_score_threshold = *other.nms_score_threshold;
    if (other.nms_classes_filter_mask)
        nms_classes_filter_mask = *other.nms_classes_filter_mask;
    if (other.nms_max_accumulated_mask_size_multiplier)
        nms_max_accumulated_mask_size_multiplier = *other.nms_max_accumulated_mask_size_multiplier;
    if (other.use_hailort_service)
        use_hailort_service = *other.use_hailort_service;
    if (other.trace)
        trace = *other.trace;
}

void ai_stage_config_t::apply_to(ai_stages::HailortAsyncStageBuild::Builder &b) const
{
    if (hef_path)
        b.set_hef_path(*hef_path);
    if (stage_name)
        b.set_stage_name(*stage_name);
    if (queue_size)
        b.set_queue_size(*queue_size);
    if (output_pool_size)
        b.set_output_pool_size(*output_pool_size);
    if (group_id)
        b.set_group_id(*group_id);
    if (batch_size)
        b.set_batch_size(*batch_size);
    if (job_limit)
        b.set_job_limit(*job_limit);
    if (scheduler_threshold)
        b.set_scheduler_threshold_opt(*scheduler_threshold);
    if (dynamic_threshold)
        b.set_dynamic_threshold_opt(*dynamic_threshold);
    if (scheduler_timeout)
        b.set_scheduler_timeout_opt(*scheduler_timeout);
    if (scheduler_priority)
        b.set_scheduler_priority_opt(*scheduler_priority);
    if (pool_mode)
        b.set_pool_mode_opt(*pool_mode);
    if (nms_score_threshold)
        b.set_nms_score_threshold(*nms_score_threshold);
    if (nms_classes_filter_mask)
        b.set_nms_classes_filter_mask(*nms_classes_filter_mask);
    if (nms_max_accumulated_mask_size_multiplier)
        b.set_nms_max_accumulated_mask_size_multiplier(*nms_max_accumulated_mask_size_multiplier);
    if (use_hailort_service)
        b.set_use_hailort_service(*use_hailort_service);
    if (trace)
        b.set_trace_opt(*trace);
}

// ============================================================================
// postprocess_stage_config_t implementation
// ============================================================================

void postprocess_stage_config_t::merge_from(const postprocess_stage_config_t &other)
{
    if (other.so_path)
        so_path = *other.so_path;
    if (other.function_name)
        function_name = *other.function_name;
    if (other.stage_name)
        stage_name = *other.stage_name;
    if (other.config_path)
        config_path = *other.config_path;
    if (other.queue_size)
        queue_size = *other.queue_size;
    if (other.leaky)
        leaky = *other.leaky;
    if (other.trace)
        trace = *other.trace;
}

void postprocess_stage_config_t::apply_to(ai_stages::PostprocessStageBuild::Builder &b) const
{
    if (so_path)
        b.set_so_path(*so_path);
    if (function_name)
        b.set_function_name_opt(*function_name);
    if (stage_name)
        b.set_stage_name(*stage_name);
    if (config_path)
        b.set_config_path_opt(*config_path);
    if (queue_size)
        b.set_queue_size_opt(*queue_size);
    if (leaky)
        b.set_leaky_opt(*leaky);
    if (trace)
        b.set_trace_opt(*trace);
}

// ============================================================================
// ai_postprocess_pair_config_t implementation
// ============================================================================

void ai_postprocess_pair_config_t::merge_from(const ai_postprocess_pair_config_t &other)
{
    ai_config.merge_from(other.ai_config);
    post_config.merge_from(other.post_config);
}

void ai_postprocess_pair_config_t::apply_to(ai_stages::HailortAsyncStageBuild::Builder &b) const
{
    ai_config.apply_to(b);
}

void ai_postprocess_pair_config_t::apply_to(ai_stages::PostprocessStageBuild::Builder &b) const
{
    post_config.apply_to(b);
}

// ============================================================================
// aggregator_config_t implementation
// ============================================================================

void aggregator_config_t::merge_from(const aggregator_config_t &other)
{
    if (other.stage_name)
        stage_name = *other.stage_name;
    if (other.static_sub_frames)
        static_sub_frames = *other.static_sub_frames;
    if (other.main_inlet_name)
        main_inlet_name = *other.main_inlet_name;
    if (other.main_queue_size)
        main_queue_size = *other.main_queue_size;
    if (other.main_queue_leaky)
        main_queue_leaky = *other.main_queue_leaky;
    if (other.sub_inlet_name)
        sub_inlet_name = *other.sub_inlet_name;
    if (other.sub_queue_size)
        sub_queue_size = *other.sub_queue_size;
    if (other.sub_queue_leaky)
        sub_queue_leaky = *other.sub_queue_leaky;
    if (other.multi_scale)
        multi_scale = *other.multi_scale;
    if (other.skip_migration)
        skip_migration = *other.skip_migration;
    if (other.trace)
        trace = *other.trace;
    if (other.iou_threshold)
        iou_threshold = *other.iou_threshold;
    if (other.border_threshold)
        border_threshold = *other.border_threshold;
    if (other.copy_sub_frame_tensor_to_metadata)
        copy_sub_frame_tensor_to_metadata = *other.copy_sub_frame_tensor_to_metadata;
}

void aggregator_config_t::apply_to(cropping_stages::AggregatorStageBuild::Builder &b) const
{
    if (stage_name)
        b.set_stage_name(*stage_name);
    if (static_sub_frames)
        b.set_static_subframes_opt(*static_sub_frames);
    if (main_inlet_name)
        b.set_main_inlet_name(*main_inlet_name);
    if (main_queue_size)
        b.set_main_queue_size(*main_queue_size);
    if (main_queue_leaky)
        b.set_main_leaky(*main_queue_leaky);
    if (sub_inlet_name)
        b.set_sub_inlet_name(*sub_inlet_name);
    if (sub_queue_size)
        b.set_sub_queue_size(*sub_queue_size);
    if (sub_queue_leaky)
        b.set_sub_leaky(*sub_queue_leaky);
    if (multi_scale)
        b.set_multiscale_opt(*multi_scale);
    if (skip_migration)
        b.set_skip_migration_opt(*skip_migration);
    if (trace)
        b.set_trace_opt(*trace);
    if (iou_threshold)
        b.set_iou_threshold_opt(*iou_threshold);
    if (border_threshold)
        b.set_border_threshold_opt(*border_threshold);
    if (copy_sub_frame_tensor_to_metadata)
        b.set_copy_sub_frame_tensor_to_metadata_opt(*copy_sub_frame_tensor_to_metadata);
}

} // namespace hailo_analytics::analytics
