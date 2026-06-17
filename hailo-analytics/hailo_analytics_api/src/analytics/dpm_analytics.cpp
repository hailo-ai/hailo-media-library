#include "hailo_analytics/analytics/dpm_analytics.hpp"

#include <stdint.h>
#include <hailo_postprocess_tools/labels/hailo_yolov8n.hpp>
#include <hailo_postprocess_tools/objects/hailo_objects.hpp>
#include <media_library/media_library_types.hpp>
#include <tl/expected.hpp>
#include <algorithm>
#include <map>
#include <functional>
#include <utility>

#include "hailo_analytics/analytics/ai_models_config.hpp"
#include "hailo_analytics/pipeline/ai/analytics_db_stage.hpp"
#include "hailo_analytics/pipeline/routing/callback_stage.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_analytics/analytics/common_configs.hpp"
#include "hailo_analytics/analytics/detection.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"

namespace hailo_analytics::analytics::dpm_analytics
{

static void prune_roi_to_labels_snapshot(HailoROIPtr roi, const std::vector<std::string> &keep_labels)
{
    // Allow-list semantic: empty keep_labels means "no labels are interesting" — drop every
    // detection. Matches BBoxCropStage's existing behavior on its own labels list, and is what
    // the webserver DPM UI needs when the user unchecks every label (otherwise downstream stages
    // see all detections, no segmentor crops fire, and the EncoderStage emits every detection as
    // an overflow LabeledDetection — masking everything when the user asked for nothing).
    auto all_detections = hailo_common::get_hailo_detections(roi);
    for (auto detection : all_detections)
    {
        const std::string &label = detection->get_label();
        if (std::find(keep_labels.begin(), keep_labels.end(), label) == keep_labels.end())
            roi->remove_object(detection);
    }
}

DetectorLabelFilter::DetectorLabelFilter(std::string name, size_t queue_size, bool leaky,
                                         std::vector<std::string> initial_labels)
    : hailo_analytics::pipeline::routing::CallbackStage(std::move(name), queue_size, leaky, /*callback=*/nullptr,
                                                        /*trace_processing_operations=*/true),
      m_labels(std::make_shared<const std::vector<std::string>>(std::move(initial_labels)))
{
    set_callback([this](hailo_analytics::pipeline::BufferPtr data) {
        auto roi = data->get_roi();
        if (!roi)
            return;
        prune_roi_to_labels_snapshot(roi, *std::atomic_load(&m_labels));
    });
}

void DetectorLabelFilter::set_labels(std::vector<std::string> labels)
{
    std::atomic_store(&m_labels, std::make_shared<const std::vector<std::string>>(std::move(labels)));
}

std::shared_ptr<const std::vector<std::string>> DetectorLabelFilter::get_labels() const
{
    return std::atomic_load(&m_labels);
}

void detector_label_filter_config_t::merge_from(const detector_label_filter_config_t &other)
{
    if (other.labels)
        labels = *other.labels;
    if (other.queue_size)
        queue_size = *other.queue_size;
    if (other.leaky)
        leaky = *other.leaky;
}

void full_dpm_analytics_config_t::merge_from(const full_dpm_analytics_config_t &other)
{
    tiling_config.merge_from(other.tiling_config);
    detector_label_filter_config.merge_from(other.detector_label_filter_config);
    dpm_config.merge_from(other.dpm_config);
    if (other.metadata_sender_config.has_value())
    {
        if (metadata_sender_config.has_value())
        {
            metadata_sender_config->merge_from(*other.metadata_sender_config);
        }
        else
        {
            metadata_sender_config = *other.metadata_sender_config;
        }
    }
    enable_detections_db_writer = other.enable_detections_db_writer;
}

// Default tuning parameters for the DPM analytics pipeline
static constexpr float NMS_SCORE_THRESHOLD = 0.2f;
static constexpr float AGGREGATOR_IOU_THRESHOLD = 0.3f;
static constexpr size_t DEFAULT_STAGE_QUEUE_SIZE = 5;
static constexpr int TILING_CROP_EVERY_X_FRAMES = 2;
static constexpr int DETECTION_OUTPUT_POOL_SIZE = 100;

full_dpm_analytics_config_t base_config()
{
    full_dpm_analytics_config_t config;

    config.tiling_config.detection_config.ai_config.nms_score_threshold = NMS_SCORE_THRESHOLD;
    config.tiling_config.aggregator_config.iou_threshold = AGGREGATOR_IOU_THRESHOLD;

    config.tiling_config.tracker_config.enabled = true;
    config.tiling_config.tracker_config.labels_map = common::hailo_yolov8n;

    config.detector_label_filter_config.queue_size = DEFAULT_STAGE_QUEUE_SIZE;
    config.detector_label_filter_config.leaky = false;

    config.metadata_sender_config = analytic_metadata_zmq_sender::base_analytic_metadata_zmq_sender_config();

    return config;
}

full_dpm_analytics_config_t build_dpm_config(int ai_width, int ai_height, int max_detections,
                                             const std::vector<std::string> &segment_labels,
                                             const std::string &seg_hef_path)
{
    namespace ai_models = hailo_analytics::analytics::ai_models;
    full_dpm_analytics_config_t config;

    // Tiling detection config — sourced from the ai_models registry
    ai_models::apply_to(ai_models::YOLOV8N, config.tiling_config.detection_config);
    config.tiling_config.detection_config.ai_config.output_pool_size = DETECTION_OUTPUT_POOL_SIZE;
    config.tiling_config.tiling_config.crop_every_x_frames = TILING_CROP_EVERY_X_FRAMES;
    config.tiling_config.tiling_config.input_width = ai_width;
    config.tiling_config.tiling_config.input_height = ai_height;

    config.detector_label_filter_config.labels = segment_labels;

    // DPM segmentation config — caller may override the seg HEF; default is from registry.
    const std::string resolved_seg_hef =
        seg_hef_path.empty() ? ai_models::resolve_hef(ai_models::LINKNET_DPM_128.hef_relative) : seg_hef_path;
    config.dpm_config.segmentation_config.ai_config.hef_path = resolved_seg_hef;
    config.dpm_config.bbox_crop_config.input_width = ai_width;
    config.dpm_config.bbox_crop_config.input_height = ai_height;
    config.dpm_config.bbox_crop_config.labels = segment_labels;
    config.dpm_config.bbox_crop_config.max_crops = max_detections;

    return config;
}

static std::shared_ptr<DetectorLabelFilter> create_detector_label_filter_stage(
    const detector_label_filter_config_t &filter_config)
{
    return std::make_shared<DetectorLabelFilter>(
        std::string(DETECTOR_LABEL_FILTER_STAGE), filter_config.queue_size.value_or(DEFAULT_STAGE_QUEUE_SIZE),
        filter_config.leaky.value_or(false), filter_config.labels.value_or(std::vector<std::string>{}));
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_full_dpm_analytics_pipeline(const std::string &pipeline_name,
                                     const std::optional<full_dpm_analytics_config_t> &user_configs)
{
    full_dpm_analytics_config_t cfg = base_config();
    if (user_configs.has_value())
    {
        cfg.merge_from(user_configs.value());
    }

    auto tiling_pipeline_result =
        tiling::generate_tiling_detection_pipeline(std::string(tiling::TILING_DETECTION_PIPELINE), cfg.tiling_config);
    if (!tiling_pipeline_result.has_value())
    {
        return tl::unexpected(tiling_pipeline_result.error());
    }
    auto tiling_pipeline = tiling_pipeline_result.value();

    auto detector_filter = create_detector_label_filter_stage(cfg.detector_label_filter_config);

    auto dpm_pipeline_result = dynamic_privacy_mask::generate_dynamic_privacy_mask_pipeline(
        std::string(dynamic_privacy_mask::BBOX_CROP_SEGMENTATION_PIPELINE), cfg.dpm_config);
    if (!dpm_pipeline_result.has_value())
    {
        return tl::unexpected(dpm_pipeline_result.error());
    }
    auto dpm_pipeline = dpm_pipeline_result.value();

    static constexpr const char *DETECTIONS_DATA_ID = "detections";
    static constexpr const char *DETECTIONS_DB_STAGE = "detections_db";
    std::shared_ptr<hailo_analytics::pipeline::ai::AnalyticsDBStage> detections_db_stage = nullptr;
    if (cfg.enable_detections_db_writer)
    {
        detections_db_stage = hailo_analytics::pipeline::ai::AnalyticsDBStageBuild::create()
                                  .set_stage_name(DETECTIONS_DB_STAGE)
                                  .set_queue_size(10)
                                  .set_leaky_opt(true)
                                  .set_analytics_data_id(DETECTIONS_DATA_ID)
                                  .set_type(AnalyticsType::DETECTION)
                                  .buildptr();
    }

    hailo_analytics::pipeline::PipelinePtr metadata_sender_pipeline = nullptr;
    if (cfg.metadata_sender_config.has_value())
    {
        auto metadata_sender_result = analytic_metadata_zmq_sender::generate_analytic_metadata_zmq_sender_pipeline(
            "dpm_metadata_sender_pipeline", cfg.metadata_sender_config);
        if (!metadata_sender_result.has_value())
        {
            return tl::unexpected(metadata_sender_result.error());
        }
        metadata_sender_pipeline = metadata_sender_result.value();
    }

    hailo_analytics::pipeline::PipelineBuilder pip_builder;
    pip_builder.add_stage(tiling_pipeline);
    pip_builder.add_stage(detector_filter);
    pip_builder.add_stage(dpm_pipeline);
    if (detections_db_stage)
    {
        pip_builder.add_stage(detections_db_stage, hailo_analytics::pipeline::StageType::SINK);
    }
    if (metadata_sender_pipeline)
    {
        pip_builder.add_stage(metadata_sender_pipeline, hailo_analytics::pipeline::StageType::SINK);
    }

    pip_builder.connect(tiling_pipeline->get_name(), detector_filter->get_name());
    if (detections_db_stage)
    {
        pip_builder.connect(tiling_pipeline->get_name(), detections_db_stage->get_name());
    }
    pip_builder.connect(detector_filter->get_name(), dpm_pipeline->get_name());
    if (metadata_sender_pipeline)
    {
        pip_builder.connect(tiling_pipeline->get_name(), metadata_sender_pipeline->get_name());
    }

    auto pipeline = pip_builder.build(pipeline_name, true);
    pipeline->set_in_stage(tiling_pipeline);
    pipeline->set_out_stage(dpm_pipeline);
    return pipeline;
}

} // namespace hailo_analytics::analytics::dpm_analytics
