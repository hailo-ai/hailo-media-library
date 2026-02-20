#include "ai_pipeline_builder.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

namespace ai_example_app
{

using namespace hailo_analytics::pipeline;
using namespace hailo_analytics::pipeline::muxing;
using namespace hailo_analytics::pipeline::routing;
using namespace hailo_analytics::pipeline::cropping;
using namespace hailo_analytics::pipeline::ai;
using namespace hailo_analytics::pipeline::overlay;

AIPipelineStages AIPipelineBuilder::create_ai_stages(const AIPipelineConfig &config)
{
    AIPipelineStages stages;

    try
    {
        /*
         * Stage 1: Detection Pipeline
         * muxer -> callback -> tiling -> yolo -> post -> aggregator -> stage_1_aggregator
         */

        // Muxer stage: combines main and secondary vision streams
        stages.muxer = MuxerStageBuild::create()
                           .set_stage_name(config.muxer_stage_name)
                           .set_main_inlet_name(config.vision_sink)
                           .set_sub_inlet_name(config.secondary_vision_sink)
                           .set_main_queue_size(10)
                           .set_sub_queue_size(10)
                           .set_main_leaky(false)
                           .set_sub_leaky(false)
                           .set_trace_opt(true)
                           .buildptr();

        // Callback stage: can be used for custom processing
        stages.callback = CallbackStageBuild::create()
                              .set_stage_name(config.callback_stage_name)
                              .set_queue_size_opt(1)
                              .set_leaky_opt(false)
                              .set_trace_opt(true)
                              .buildptr();

        // Tiling stage: crops input into tiles for detection
        stages.tiling = TilingCropStageBuild::create()
                            .set_stage_name(config.tiling_stage_name)
                            .set_output_pool_size(50)
                            .set_input_width(config.tiling_input_width)
                            .set_input_height(config.tiling_input_height)
                            .set_output_width(config.tiling_output_width)
                            .set_output_height(config.tiling_output_height)
                            .set_main_sub_name(config.detection_aggregator_name)
                            .set_sub_sub_name(config.detection_ai_stage_name)
                            .set_bbox_tiles(config.tiles)
                            .set_queue_size(5)
                            .set_leaky_opt(true)
                            .set_trace_opt(true)
                            .set_pool_mode_opt(StagePoolMode::BLOCKING)
                            .set_crop_every_x_frames(config.crop_every_x_frames)
                            .buildptr();

        // Detection AI stage: runs YOLO model
        stages.detection_ai = HailortAsyncStageBuild::create()
                                  .set_stage_name(config.detection_ai_stage_name)
                                  .set_hef_path(config.yolo_hef_file)
                                  .set_queue_size(5)
                                  .set_output_pool_size(50)
                                  .set_group_id("device0")
                                  .set_batch_size(5)
                                  .set_job_limit(10)
                                  .set_scheduler_threshold_opt(5)
                                  .set_dynamic_threshold_opt(false)
                                  .set_scheduler_timeout_opt(std::chrono::milliseconds(100))
                                  .set_trace_opt(true)
                                  .set_pool_mode_opt(StagePoolMode::BLOCKING)
                                  .buildptr();

        // Detection postprocess stage: processes YOLO output
        stages.detection_post = PostprocessStageBuild::create()
                                    .set_stage_name(config.post_stage_name)
                                    .set_so_path(config.yolo_post_so)
                                    .set_function_name_opt(config.yolo_func_name)
                                    .set_config_path_opt(config.yolo_post_conf)
                                    .set_queue_size_opt(5)
                                    .set_leaky_opt(false)
                                    .set_trace_opt(true)
                                    .buildptr();

        // Detection aggregator: aggregates detection results from tiles
        stages.detection_aggregator = AggregatorStageBuild::create()
                                          .set_stage_name(config.detection_aggregator_name)
                                          .set_main_inlet_name(config.tiling_stage_name)
                                          .set_main_queue_size(4)
                                          .set_main_leaky(false)
                                          .set_sub_inlet_name(config.post_stage_name)
                                          .set_sub_queue_size(5)
                                          .set_sub_leaky(false)
                                          .set_multiscale_opt(true)
                                          .set_iou_threshold_opt(0.3)
                                          .set_border_threshold_opt(0.1)
                                          .set_trace_opt(true)
                                          .buildptr();

        // Stage 1 aggregator: aggregates callback and detection results
        stages.stage_1_aggregator = SyncAggregatorStageBuild::create()
                                        .set_stage_name(config.stage_1_aggregator_name)
                                        .set_main_inlet_name(config.callback_stage_name)
                                        .set_main_queue_size(4)
                                        .set_main_leaky(true)
                                        .set_sub_inlet_name(config.detection_aggregator_name)
                                        .set_sub_queue_size(3)
                                        .set_sub_leaky(false)
                                        .set_multiscale_opt(false)
                                        .set_iou_threshold_opt(0.3)
                                        .set_border_threshold_opt(0.1)
                                        .set_trace_opt(true)
                                        .set_timeout_opt(std::chrono::milliseconds(66))
                                        .set_timeout_adjustment_period_opt(std::chrono::milliseconds(500))
                                        .set_drop_rate_block_opt(true)
                                        .buildptr();

        /*
         * Stage 2: Landmarks Pipeline
         * tee -> bbox_crop -> landmarks -> post -> aggregator -> stage_2_aggregator
         */

        // Tee stage: splits the stream for landmarks processing
        stages.tee = TeeStageBuild::create()
                         .set_stage_name(config.tee_stage_name)
                         .set_queue_size(1)
                         .set_leaky_opt(false)
                         .set_trace_opt(true)
                         .buildptr();

        // BBox crop stage: crops detected faces for landmark detection
        stages.bbox_crop = BBoxCropStageBuild::create()
                               .set_stage_name(config.bbox_crop_stage_name)
                               .set_output_pool_size(150)
                               .set_input_width(config.input_width)
                               .set_input_height(config.input_height)
                               .set_output_width(config.bbox_crop_output_width)
                               .set_output_height(config.bbox_crop_output_height)
                               .set_main_sub_name(config.landmarks_aggregator_name)
                               .set_sub_sub_name(config.landmarks_ai_stage_name)
                               .set_labels(config.bbox_crop_labels)
                               .set_queue_size(5)
                               .set_leaky_opt(true)
                               .set_trace_opt(true)
                               .set_pool_mode_opt(StagePoolMode::BLOCKING)
                               .buildptr();

        // Landmarks AI stage: runs face landmarks model
        stages.landmarks_ai = HailortAsyncStageBuild::create()
                                  .set_stage_name(config.landmarks_ai_stage_name)
                                  .set_hef_path(config.landmarks_hef_file)
                                  .set_queue_size(100)
                                  .set_output_pool_size(201)
                                  .set_group_id("device0")
                                  .set_batch_size(60)
                                  .set_job_limit(60)
                                  .set_scheduler_threshold_opt(60)
                                  .set_dynamic_threshold_opt(true)
                                  .set_scheduler_timeout_opt(std::chrono::milliseconds(100))
                                  .set_trace_opt(true)
                                  .set_pool_mode_opt(StagePoolMode::BLOCKING)
                                  .buildptr();

        // Landmarks postprocess stage: processes landmarks output
        stages.landmarks_post = PostprocessStageBuild::create()
                                    .set_stage_name(config.landmarks_post_stage_name)
                                    .set_so_path(config.landmarks_post_so)
                                    .set_function_name_opt(config.landmarks_func_name)
                                    .set_config_path_opt("")
                                    .set_queue_size_opt(100)
                                    .set_leaky_opt(false)
                                    .set_trace_opt(true)
                                    .buildptr();

        // Landmarks aggregator: aggregates landmark results
        stages.landmarks_aggregator = AggregatorStageBuild::create()
                                          .set_stage_name(config.landmarks_aggregator_name)
                                          .set_main_inlet_name(config.bbox_crop_stage_name)
                                          .set_main_queue_size(3)
                                          .set_main_leaky(false)
                                          .set_sub_inlet_name(config.landmarks_post_stage_name)
                                          .set_sub_queue_size(100)
                                          .set_sub_leaky(false)
                                          .set_multiscale_opt(false)
                                          .set_iou_threshold_opt(0.3)
                                          .set_border_threshold_opt(0.1)
                                          .set_trace_opt(true)
                                          .buildptr();

        // Stage 2 aggregator: combines tee and landmarks results
        stages.stage_2_aggregator = SyncAggregatorStageBuild::create()
                                        .set_stage_name(config.stage_2_aggregator_name)
                                        .set_static_subframes_opt(1)
                                        .set_main_inlet_name(config.tee_stage_name)
                                        .set_main_queue_size(5)
                                        .set_main_leaky(false)
                                        .set_sub_inlet_name(config.landmarks_aggregator_name)
                                        .set_sub_queue_size(3)
                                        .set_sub_leaky(false)
                                        .set_multiscale_opt(false)
                                        .set_iou_threshold_opt(0.3)
                                        .set_border_threshold_opt(0.1)
                                        .set_skip_migration_opt(true)
                                        .set_trace_opt(true)
                                        .set_timeout_opt(std::chrono::milliseconds(33))
                                        .set_min_timeout_opt(std::chrono::milliseconds(33))
                                        .set_max_timeout_opt(std::chrono::milliseconds(66))
                                        .set_timeout_adjustment_period_opt(std::chrono::milliseconds(500))
                                        .set_drop_rate_threshold_opt(0.1)
                                        .buildptr();

        /*
         * Stage 3: Tracking and Overlay
         * tracker -> demuxer -> overlay_sink0/overlay_sink1
         */

        // Tracker stage: tracks detected objects
        stages.tracker = LightweightTrackerStageBuild::create()
                             .set_stage_name(config.tracker_stage_name)
                             .set_queue_size_opt(1)
                             .set_leaky_opt(false)
                             .set_trace_opt(true)
                             .set_classification_ids({1, 3})
                             .set_add_tracking_id(false)
                             .set_grace_period(4)
                             .set_smooth_alpha(0.5f)
                             .set_weighted_average_decay(0.4f)
                             .set_copy_nested_objects(false, 1)
                             .set_copy_nested_objects(true, 3)
                             .buildptr();

        // Demuxer stage: splits the stream to different outputs
        stages.demuxer = DemuxerStageBuild::create()
                             .set_stage_name(config.demux_stage_name)
                             .set_queue_size(3)
                             .set_main_outlet_name(config.overlay_stage_sink0_name)
                             .set_sub_outlet_name(config.overlay_stage_sink1_name)
                             .set_copy_roi_metadata_opt(true)
                             .set_leaky_opt(false)
                             .set_trace_opt(true)
                             .buildptr();

        // Overlay stage for sink0
        stages.overlay_sink0 = OverlayStageBuild::create()
                                   .set_stage_name(config.overlay_stage_sink0_name)
                                   .set_skip_opt(!config.draw_overlay_sink0)
                                   .set_partial_landmarks(!config.full_landmarks)
                                   .set_landmark_indices_to_draw(config.landmark_indices_to_draw)
                                   .set_queue_size(1)
                                   .set_leaky_opt(false)
                                   .set_trace_opt(true)
                                   .buildptr();

        // Overlay stage for sink1
        stages.overlay_sink1 = OverlayStageBuild::create()
                                   .set_stage_name(config.overlay_stage_sink1_name)
                                   .set_skip_opt(!config.draw_overlay_sink1)
                                   .set_partial_landmarks(!config.full_landmarks)
                                   .set_landmark_indices_to_draw(config.landmark_indices_to_draw)
                                   .set_queue_size(1)
                                   .set_leaky_opt(false)
                                   .set_trace_opt(true)
                                   .buildptr();
    }
    catch (const std::exception &e)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create AI pipeline stages: {}", e.what());
        throw;
    }

    return stages;
}

void AIPipelineBuilder::add_stages_to_pipeline(PipelineBuilder &pip_builder, const AIPipelineStages &stages)
{
    pip_builder.add_stage(stages.stage_1_aggregator)
        .add_stage(stages.muxer)
        .add_stage(stages.callback)
        .add_stage(stages.tiling)
        .add_stage(stages.detection_ai)
        .add_stage(stages.detection_post)
        .add_stage(stages.detection_aggregator)
        .add_stage(stages.tee)
        .add_stage(stages.stage_2_aggregator)
        .add_stage(stages.bbox_crop)
        .add_stage(stages.landmarks_ai)
        .add_stage(stages.landmarks_post)
        .add_stage(stages.landmarks_aggregator)
        .add_stage(stages.tracker)
        .add_stage(stages.demuxer)
        .add_stage(stages.overlay_sink0)
        .add_stage(stages.overlay_sink1);
}

void AIPipelineBuilder::connect_ai_stages(PipelineBuilder &pip_builder, [[maybe_unused]] const AIPipelineStages &stages,
                                          const AIPipelineConfig &config)
{
    // Stage 1 AI Subscriptions (Detection)
    pip_builder.connect(config.muxer_stage_name, config.callback_stage_name)
        .connect(config.callback_stage_name, config.stage_1_aggregator_name)
        .connect(config.tiling_stage_name, config.detection_aggregator_name)
        .connect(config.tiling_stage_name, config.detection_ai_stage_name)
        .connect(config.detection_ai_stage_name, config.post_stage_name)
        .connect(config.post_stage_name, config.detection_aggregator_name)
        .connect(config.detection_aggregator_name, config.stage_1_aggregator_name);

    // Stage 2 AI Subscriptions (Landmarks)
    pip_builder.connect(config.stage_1_aggregator_name, config.tee_stage_name)
        .connect(config.tee_stage_name, config.stage_2_aggregator_name)
        .connect(config.tee_stage_name, config.bbox_crop_stage_name)
        .connect(config.bbox_crop_stage_name, config.landmarks_aggregator_name)
        .connect(config.bbox_crop_stage_name, config.landmarks_ai_stage_name)
        .connect(config.landmarks_ai_stage_name, config.landmarks_post_stage_name)
        .connect(config.landmarks_post_stage_name, config.landmarks_aggregator_name)
        .connect(config.landmarks_aggregator_name, config.stage_2_aggregator_name);

    // Vision Pipeline stages (Tracking and Overlay)
    pip_builder.connect(config.stage_2_aggregator_name, config.tracker_stage_name)
        .connect(config.tracker_stage_name, config.demux_stage_name)
        .connect(config.demux_stage_name, config.overlay_stage_sink0_name)
        .connect(config.demux_stage_name, config.overlay_stage_sink1_name);
}

void AIPipelineBuilder::set_callback_function(const AIPipelineStages &stages, std::function<void(BufferPtr)> callback)
{
    stages.callback->set_callback(callback);
}
} // namespace ai_example_app
