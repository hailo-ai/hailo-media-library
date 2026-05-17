#include "hailo_analytics/analytics/dpm_analytics.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/routing/callback_stage.hpp"
#include "hailo_analytics/pipeline/ai/analytics_db_stage.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "media_library/analytics_db.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>

namespace hailo_analytics::analytics::dpm_analytics
{

static void prune_roi_to_labels_snapshot(HailoROIPtr roi, const std::vector<std::string> &keep_labels)
{
    if (keep_labels.empty())
        return;
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

void analytics_db_config_t::merge_from(const analytics_db_config_t &other)
{
    if (other.stage_name)
        stage_name = *other.stage_name;
    if (other.analytics_data_id)
        analytics_data_id = *other.analytics_data_id;
    if (other.overflow_analytics_data_id)
        overflow_analytics_data_id = *other.overflow_analytics_data_id;
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
    analytics_db_config.merge_from(other.analytics_db_config);
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

    config.analytics_db_config.stage_name = std::string(DPM_ANALYTICS_DB_STAGE);
    config.analytics_db_config.analytics_data_id = std::string(DPM_ANALYTICS_DATA_ID);
    config.analytics_db_config.overflow_analytics_data_id = std::string(DPM_OVERFLOW_DETECTION_DATA_ID);
    config.analytics_db_config.queue_size = DEFAULT_STAGE_QUEUE_SIZE;
    config.analytics_db_config.leaky = false;

    return config;
}

full_dpm_analytics_config_t build_dpm_config(int ai_width, int ai_height, int max_detections,
                                             const std::vector<std::string> &segment_labels,
                                             const std::string &seg_hef_path)
{
    full_dpm_analytics_config_t config;

    // Tiling detection config
    config.tiling_config.detection_config.ai_config.hef_path = std::string(DEFAULT_YOLO_HEF);
    config.tiling_config.detection_config.ai_config.output_pool_size = DETECTION_OUTPUT_POOL_SIZE;
    config.tiling_config.detection_config.post_config.so_path = std::string(DEFAULT_YOLO_POST_SO);
    config.tiling_config.detection_config.post_config.function_name = std::string(DEFAULT_YOLO_FUNC_NAME);
    config.tiling_config.detection_config.post_config.config_path = std::string(DEFAULT_YOLO_POST_CONF);
    config.tiling_config.tiling_config.crop_every_x_frames = TILING_CROP_EVERY_X_FRAMES;
    config.tiling_config.tiling_config.input_width = ai_width;
    config.tiling_config.tiling_config.input_height = ai_height;

    config.detector_label_filter_config.labels = segment_labels;

    // DPM segmentation config
    config.dpm_config.segmentation_config.ai_config.hef_path = seg_hef_path;
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

std::shared_ptr<hailo_analytics::pipeline::ai::AnalyticsDBStage> create_analytics_db_stage(
    const analytics_db_config_t &analytics_db_config)
{
    return hailo_analytics::pipeline::ai::AnalyticsDBStageBuild::create()
        .set_stage_name(analytics_db_config.stage_name.value_or(std::string(DPM_ANALYTICS_DB_STAGE)))
        .set_analytics_data_id(analytics_db_config.analytics_data_id.value_or(std::string(DPM_ANALYTICS_DATA_ID)))
        .set_type(AnalyticsType::SEMANTIC_SEGMENTATION)
        .set_queue_size(analytics_db_config.queue_size.value_or(DEFAULT_STAGE_QUEUE_SIZE))
        .set_leaky_opt(analytics_db_config.leaky.value_or(false))
        .set_overflow_analytics_data_id(
            analytics_db_config.overflow_analytics_data_id.value_or(std::string(DPM_OVERFLOW_DETECTION_DATA_ID)))
        .buildptr();
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

    auto analytics_db_stage = create_analytics_db_stage(cfg.analytics_db_config);

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
    pip_builder.add_stage(analytics_db_stage, hailo_analytics::pipeline::StageType::SINK);
    if (metadata_sender_pipeline)
    {
        pip_builder.add_stage(metadata_sender_pipeline, hailo_analytics::pipeline::StageType::SINK);
    }

    pip_builder.connect(tiling_pipeline->get_name(), detector_filter->get_name());
    pip_builder.connect(detector_filter->get_name(), dpm_pipeline->get_name());
    if (metadata_sender_pipeline)
    {
        pip_builder.connect(tiling_pipeline->get_name(), metadata_sender_pipeline->get_name());
    }
    pip_builder.connect(dpm_pipeline->get_name(),
                        cfg.analytics_db_config.stage_name.value_or(std::string(DPM_ANALYTICS_DB_STAGE)));

    auto pipeline = pip_builder.build(pipeline_name, true);
    pipeline->set_in_stage(tiling_pipeline);
    return pipeline;
}

} // namespace hailo_analytics::analytics::dpm_analytics
