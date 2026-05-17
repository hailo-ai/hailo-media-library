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

void tracker_config_t::merge_from(const tracker_config_t &other)
{
    if (other.enabled)
        enabled = *other.enabled;
    if (other.class_ids)
        class_ids = *other.class_ids;
    if (other.queue_size)
        queue_size = *other.queue_size;
    if (other.leaky)
        leaky = *other.leaky;
    if (other.grace_period)
        grace_period = *other.grace_period;
    if (other.iou_threshold)
        iou_threshold = *other.iou_threshold;
    if (other.history_size)
        history_size = *other.history_size;
}

void detection_limiter_config_t::merge_from(const detection_limiter_config_t &other)
{
    if (other.max_detections)
        max_detections = *other.max_detections;
    if (other.segment_labels)
        segment_labels = *other.segment_labels;
    if (other.overflow_only_labels)
        overflow_only_labels = *other.overflow_only_labels;
    if (other.shared_segment_labels)
        shared_segment_labels = other.shared_segment_labels;
    if (other.shared_max_detections)
        shared_max_detections = other.shared_max_detections;
    if (other.queue_size)
        queue_size = *other.queue_size;
    if (other.leaky)
        leaky = *other.leaky;
    if (other.report_overflow_detections)
        report_overflow_detections = *other.report_overflow_detections;
    if (other.overflow_detection_analytics_data_id)
        overflow_detection_analytics_data_id = *other.overflow_detection_analytics_data_id;
}

void analytics_db_config_t::merge_from(const analytics_db_config_t &other)
{
    if (other.stage_name)
        stage_name = *other.stage_name;
    if (other.analytics_data_id)
        analytics_data_id = *other.analytics_data_id;
    if (other.queue_size)
        queue_size = *other.queue_size;
    if (other.leaky)
        leaky = *other.leaky;
}

void full_dpm_analytics_config_t::merge_from(const full_dpm_analytics_config_t &other)
{
    tiling_config.merge_from(other.tiling_config);
    tracker_config.merge_from(other.tracker_config);
    limiter_config.merge_from(other.limiter_config);
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
static constexpr int DEFAULT_MAX_DETECTIONS_PER_FRAME = 35;
static constexpr size_t DEFAULT_STAGE_QUEUE_SIZE = 5;
static constexpr int TRACKER_GRACE_PERIOD = 3;
static constexpr float TRACKER_IOU_THRESHOLD = 0.9f;
static constexpr size_t TRACKER_HISTORY_SIZE = 3;
static constexpr int TILING_CROP_EVERY_X_FRAMES = 2;
static constexpr int DETECTION_OUTPUT_POOL_SIZE = 100;

// Build tracker class IDs from the canonical yolov8n label map
static std::vector<int> all_yolov8n_class_ids()
{
    std::vector<int> ids;
    for (const auto &[id, label] : common::hailo_yolov8n)
    {
        ids.push_back(static_cast<int>(id));
    }
    return ids;
}

full_dpm_analytics_config_t base_config()
{
    full_dpm_analytics_config_t config;

    // Tiling config uses its own defaults (via tiling::base_config() in generate)
    // DPM config uses its own defaults (via dynamic_privacy_mask::base_config() in generate)

    config.tiling_config.detection_config.ai_config.nms_score_threshold = NMS_SCORE_THRESHOLD;
    config.tiling_config.aggregator_config.iou_threshold = AGGREGATOR_IOU_THRESHOLD;

    config.tracker_config.enabled = true;
    config.tracker_config.class_ids = all_yolov8n_class_ids();
    config.tracker_config.queue_size = DEFAULT_STAGE_QUEUE_SIZE;
    config.tracker_config.leaky = false;
    config.tracker_config.grace_period = TRACKER_GRACE_PERIOD;
    config.tracker_config.iou_threshold = TRACKER_IOU_THRESHOLD;
    config.tracker_config.history_size = TRACKER_HISTORY_SIZE;

    // "Excess" detections = frames where the detector finds more objects than
    // max_detections_per_frame. The lowest-scoring detections beyond the limit are
    // removed from the ROI before segmentation (to bound DSP cost), but reported
    // to the analytics DB so the encoder can still draw bounding boxes for them.
    config.limiter_config.max_detections = DEFAULT_MAX_DETECTIONS_PER_FRAME;
    config.limiter_config.segment_labels = std::vector<std::string>{"person", "vehicle", "face"};
    config.limiter_config.overflow_only_labels = std::vector<std::string>{"license_plate"};
    config.limiter_config.queue_size = DEFAULT_STAGE_QUEUE_SIZE;
    config.limiter_config.leaky = false;
    config.limiter_config.report_overflow_detections = true;
    config.limiter_config.overflow_detection_analytics_data_id = std::string(DPM_OVERFLOW_DETECTION_DATA_ID);

    config.metadata_sender_config = analytic_metadata_zmq_sender::base_analytic_metadata_zmq_sender_config();

    config.analytics_db_config.stage_name = std::string(DPM_ANALYTICS_DB_STAGE);
    config.analytics_db_config.analytics_data_id = std::string(DPM_ANALYTICS_DATA_ID);
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

    // Detection limiter config
    config.limiter_config.max_detections = max_detections;
    config.limiter_config.segment_labels = segment_labels;

    // DPM segmentation config
    config.dpm_config.segmentation_config.ai_config.hef_path = seg_hef_path;
    config.dpm_config.bbox_crop_config.input_width = ai_width;
    config.dpm_config.bbox_crop_config.input_height = ai_height;
    // BBox crop labels cover all labels that the limiter might pass through for segmentation.
    // overflow_only_labels (e.g. license_plate) are stripped by the limiter before reaching
    // the DPM stage, so they don't need to be included here.
    config.dpm_config.bbox_crop_config.labels = segment_labels;

    return config;
}

hailo_analytics::pipeline::PipelinePtr create_tracker_pipeline(const tracker_config_t &tracker_config)
{
    if (!tracker_config.enabled.value_or(true))
        return nullptr;

    auto tracker_stage = hailo_analytics::pipeline::ai::LightweightTrackerStageBuild::create()
                             .set_stage_name(std::string(TRACKER_STAGE))
                             .set_queue_size_opt(tracker_config.queue_size.value_or(DEFAULT_STAGE_QUEUE_SIZE))
                             .set_leaky_opt(tracker_config.leaky.value_or(false))
                             .set_classification_ids(tracker_config.class_ids.value_or(all_yolov8n_class_ids()))
                             .set_grace_period(tracker_config.grace_period.value_or(TRACKER_GRACE_PERIOD))
                             .set_iou_threshold(tracker_config.iou_threshold.value_or(TRACKER_IOU_THRESHOLD))
                             .set_history_size(tracker_config.history_size.value_or(TRACKER_HISTORY_SIZE))
                             .buildptr();

    hailo_analytics::pipeline::PipelineBuilder tracker_builder;
    tracker_builder.add_stage(tracker_stage);
    auto tracker_pipeline = tracker_builder.build(std::string(TRACKER_PIPELINE), true);
    tracker_pipeline->set_in_stage(tracker_stage);
    tracker_pipeline->set_out_stage(tracker_stage);
    return tracker_pipeline;
}

static std::vector<hailo_detection_t> convert_to_wire_detections(const std::vector<HailoDetectionPtr> &detections,
                                                                 uint32_t ai_width, uint32_t ai_height)
{
    std::vector<hailo_detection_t> result;
    result.reserve(detections.size());
    for (const auto &det : detections)
    {
        HailoBBox bbox = det->get_bbox();
        hailo_detection_t detection;
        detection.score = det->get_confidence();
        detection.class_id = static_cast<uint16_t>(det->get_class_id());
        detection.x_min = bbox.xmin() * static_cast<float32_t>(ai_width);
        detection.y_min = bbox.ymin() * static_cast<float32_t>(ai_height);
        detection.x_max = bbox.xmax() * static_cast<float32_t>(ai_width);
        detection.y_max = bbox.ymax() * static_cast<float32_t>(ai_height);
        result.push_back(detection);
    }
    return result;
}

void report_overflow_to_analytics_db(const std::vector<HailoDetectionPtr> &detections_to_remove,
                                     const std::string &overflow_analytics_id,
                                     hailo_analytics::pipeline::BufferPtr data, bool is_detection_frame,
                                     std::vector<hailo_detection_t> &cached_overflow)
{
    auto &analytics_db = AnalyticsDB::instance();
    auto app_config = analytics_db.get_application_analytics_config();
    auto config_it = app_config.detection_analytics_config.find(overflow_analytics_id);
    if (config_it == app_config.detection_analytics_config.end())
    {
        HAILO_ANALYTICS_LOG_TRACE(
            "[DPM] Overflow detection analytics config not found for ID '{}', skipping overflow reporting",
            overflow_analytics_id);
        return;
    }

    auto isp_timestamp = data->get_buffer()->isp_timestamp_ns;
    auto timestamp = std::chrono::time_point<std::chrono::steady_clock>(std::chrono::nanoseconds(isp_timestamp));

    if (!is_detection_frame)
    {
        // On skip frames (tiling crop_every_x_frames), repeat the cached overflow result
        DetectionAnalyticsData db_data = {.ts = timestamp, .analytics_buffer = cached_overflow};
        analytics_db.add_detection_entry(overflow_analytics_id, db_data);
        HAILO_ANALYTICS_LOG_TRACE("[DPM] Repeated {} cached overflow detections for skip frame, timestamp {}",
                                  cached_overflow.size(), isp_timestamp);
        return;
    }

    // On detection frames, convert new overflow detections and update cache
    cached_overflow =
        detections_to_remove.empty()
            ? std::vector<hailo_detection_t>{}
            : convert_to_wire_detections(detections_to_remove, config_it->second.width, config_it->second.height);

    DetectionAnalyticsData db_data = {.ts = timestamp, .analytics_buffer = cached_overflow};
    auto ret = analytics_db.add_detection_entry(overflow_analytics_id, db_data);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("[DPM] Failed to add overflow detection entry to analytics DB");
    }
    else
    {
        HAILO_ANALYTICS_LOG_TRACE("[DPM] Reported {} overflow detections to analytics DB at id {}, timestamp {}",
                                  cached_overflow.size(), overflow_analytics_id, isp_timestamp);
    }
}

static std::string format_label_summary(const std::map<std::string, size_t> &label_counts)
{
    std::string summary;
    for (const auto &[label, count] : label_counts)
    {
        if (!summary.empty())
            summary += ", ";
        summary += label + ":" + std::to_string(count);
    }
    return summary;
}

// Partition detections into relevant (matching labels) and irrelevant, removing irrelevant from ROI
static std::vector<HailoDetectionPtr> filter_detections_by_labels(
    HailoROIPtr roi, const std::vector<std::string> &labels, std::map<std::string, size_t> &label_counts,
    std::vector<HailoDetectionPtr> *out_irrelevant = nullptr)
{
    auto all_detections = hailo_common::get_hailo_detections(roi);
    std::vector<HailoDetectionPtr> relevant_detections;

    for (auto detection : all_detections)
    {
        std::string label = detection->get_label();
        label_counts[label]++;

        if (std::find(labels.begin(), labels.end(), label) != labels.end())
        {
            relevant_detections.push_back(detection);
        }
        else
        {
            if (out_irrelevant)
                out_irrelevant->push_back(detection);
            roi->remove_object(detection);
        }
    }

    return relevant_detections;
}

// Sort detections by area*confidence (descending), remove excess, and report overflow.
// extra_overflow: detections already removed from ROI (e.g. irrelevant labels) to include in the overflow report.
static size_t enforce_detection_limit(HailoROIPtr roi, std::vector<HailoDetectionPtr> &relevant_detections,
                                      int max_detections, bool report_overflow,
                                      const std::string &overflow_analytics_id,
                                      hailo_analytics::pipeline::BufferPtr data, bool is_detection_frame,
                                      std::vector<hailo_detection_t> &cached_overflow,
                                      const std::vector<HailoDetectionPtr> &extra_overflow = {})
{
    std::vector<HailoDetectionPtr> detections_to_report = extra_overflow;

    if (relevant_detections.size() > static_cast<size_t>(max_detections))
    {
        std::sort(relevant_detections.begin(), relevant_detections.end(),
                  [](const HailoDetectionPtr &a, const HailoDetectionPtr &b) {
                      HailoBBox bbox_a = a->get_bbox();
                      HailoBBox bbox_b = b->get_bbox();
                      float score_a = bbox_a.width() * bbox_a.height() * a->get_confidence();
                      float score_b = bbox_b.width() * bbox_b.height() * b->get_confidence();
                      return score_a > score_b;
                  });

        for (size_t i = max_detections; i < relevant_detections.size(); ++i)
        {
            detections_to_report.push_back(relevant_detections[i]);
            roi->remove_object(relevant_detections[i]);
        }
    }

    if (report_overflow)
    {
        report_overflow_to_analytics_db(detections_to_report, overflow_analytics_id, data, is_detection_frame,
                                        cached_overflow);
    }

    size_t extra_count = extra_overflow.size();
    return detections_to_report.size() > extra_count ? detections_to_report.size() - extra_count : 0;
}

hailo_analytics::pipeline::PipelinePtr create_limiter_pipeline(const detection_limiter_config_t &limiter_config)
{
    int static_max_detections = limiter_config.max_detections.value_or(DEFAULT_MAX_DETECTIONS_PER_FRAME);
    auto shared_max_detections = limiter_config.shared_max_detections;
    // If shared_segment_labels is provided, the callback reads from it at runtime (dynamic updates).
    // Otherwise, capture a static copy of segment_labels.
    auto shared_labels = limiter_config.shared_segment_labels;
    const std::vector<std::string> static_labels =
        limiter_config.segment_labels.value_or(std::vector<std::string>{"person", "vehicle"});
    const std::vector<std::string> overflow_only_labels =
        limiter_config.overflow_only_labels.value_or(std::vector<std::string>{});
    const bool report_overflow = limiter_config.report_overflow_detections.value_or(true);
    const std::string overflow_analytics_id =
        limiter_config.overflow_detection_analytics_data_id.value_or(std::string(DPM_OVERFLOW_DETECTION_DATA_ID));

    // Cache for overflow detections to repeat on skip frames (when tiling doesn't run detection)
    auto cached_overflow = std::make_shared<std::vector<hailo_detection_t>>();

    auto limiter_callback = [static_max_detections, shared_max_detections, shared_labels, static_labels,
                             overflow_only_labels, report_overflow, overflow_analytics_id,
                             cached_overflow](hailo_analytics::pipeline::BufferPtr data) {
        auto roi = data->get_roi();
        if (!roi)
            return;

        int max_detections = shared_max_detections ? shared_max_detections->load() : static_max_detections;

        // Thread-safe read: SharedLabels::load() returns a snapshot via atomic shared_ptr
        auto labels_snapshot = shared_labels ? shared_labels->load() : nullptr;
        const auto &base_labels = labels_snapshot ? *labels_snapshot : static_labels;

        std::vector<std::string> expanded_labels(base_labels.begin(), base_labels.end());

        // Remove overflow_only_labels from expanded_labels so they never go through segmentation.
        // They'll be classified as "irrelevant" by the filter, then selectively routed to overflow.
        for (const auto &ol : overflow_only_labels)
        {
            expanded_labels.erase(std::remove(expanded_labels.begin(), expanded_labels.end(), ol),
                                  expanded_labels.end());
        }

        std::map<std::string, size_t> label_counts;
        std::vector<HailoDetectionPtr> irrelevant_detections;
        auto relevant_detections =
            filter_detections_by_labels(roi, expanded_labels, label_counts, &irrelevant_detections);

        // Route overflow_only_labels that the user has enabled (present in base_labels) to overflow.
        // When the user disables license_plate in the UI it's removed from base_labels, so skip it.
        std::vector<HailoDetectionPtr> overflow_detections;
        for (auto &det : irrelevant_detections)
        {
            const auto &label = det->get_label();
            bool is_overflow_label = std::find(overflow_only_labels.begin(), overflow_only_labels.end(), label) !=
                                     overflow_only_labels.end();
            bool is_enabled = std::find(base_labels.begin(), base_labels.end(), label) != base_labels.end();
            if (is_overflow_label && is_enabled)
            {
                overflow_detections.push_back(det);
            }
        }

        // Heuristic: treat frames with any detections as detection frames, and frames with zero
        // detections as tiling skip frames (repeat cached overflow). Note: genuine empty detection
        // results are indistinguishable from skip frames here — both produce total_detections == 0.
        size_t total_detections = 0;
        for (const auto &[label, count] : label_counts)
            total_detections += count;
        bool is_detection_frame = total_detections > 0;

        size_t irrelevant_count = total_detections - relevant_detections.size();
        size_t overflow_only_count = overflow_detections.size();
        size_t excess_count =
            enforce_detection_limit(roi, relevant_detections, max_detections, report_overflow, overflow_analytics_id,
                                    data, is_detection_frame, *cached_overflow, overflow_detections);

        size_t passed_count = std::min(relevant_detections.size(), static_cast<size_t>(max_detections));
        HAILO_ANALYTICS_LOG_TRACE(
            "[DPM] Segmented: {}, Detection-only (overflow): {}, Overflow-only: {}, Irrelevant: {}, Total: {}, Labels: "
            "[{}]",
            passed_count, excess_count, overflow_only_count,
            irrelevant_count > overflow_only_count ? irrelevant_count - overflow_only_count : 0, total_detections,
            format_label_summary(label_counts));
    };

    auto limiter_stage = hailo_analytics::pipeline::routing::CallbackStageBuild::create()
                             .set_stage_name(std::string(DETECTION_LIMITER_STAGE))
                             .set_queue_size_opt(limiter_config.queue_size.value_or(DEFAULT_STAGE_QUEUE_SIZE))
                             .set_leaky_opt(limiter_config.leaky.value_or(false))
                             .buildptr();
    limiter_stage->set_callback(limiter_callback);

    hailo_analytics::pipeline::PipelineBuilder limiter_builder;
    limiter_builder.add_stage(limiter_stage);
    auto limiter_pipeline = limiter_builder.build(std::string(DETECTION_LIMITER_PIPELINE), true);
    limiter_pipeline->set_in_stage(limiter_stage);
    limiter_pipeline->set_out_stage(limiter_stage);
    return limiter_pipeline;
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

    // Generate sub-pipelines
    auto tiling_pipeline_result =
        tiling::generate_tiling_detection_pipeline(std::string(tiling::TILING_DETECTION_PIPELINE), cfg.tiling_config);
    if (!tiling_pipeline_result.has_value())
    {
        return tl::unexpected(tiling_pipeline_result.error());
    }
    auto tiling_pipeline = tiling_pipeline_result.value();

    auto tracker_pipeline = create_tracker_pipeline(cfg.tracker_config);
    auto limiter_pipeline = create_limiter_pipeline(cfg.limiter_config);

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

    // Assemble the composite pipeline
    hailo_analytics::pipeline::PipelineBuilder pip_builder;
    pip_builder.add_stage(tiling_pipeline);
    if (tracker_pipeline)
    {
        pip_builder.add_stage(tracker_pipeline);
    }
    pip_builder.add_stage(limiter_pipeline);
    pip_builder.add_stage(dpm_pipeline);
    pip_builder.add_stage(analytics_db_stage, hailo_analytics::pipeline::StageType::SINK);
    if (metadata_sender_pipeline)
    {
        pip_builder.add_stage(metadata_sender_pipeline, hailo_analytics::pipeline::StageType::SINK);
    }

    // Connect: tiling -> [tracker ->] limiter -> DPM -> analytics_db
    //                              \-> metadata_sender (optional)
    std::string prev_stage = tiling_pipeline->get_name();
    if (tracker_pipeline)
    {
        pip_builder.connect(prev_stage, tracker_pipeline->get_name());
        prev_stage = tracker_pipeline->get_name();
    }
    pip_builder.connect(prev_stage, limiter_pipeline->get_name());
    if (metadata_sender_pipeline)
    {
        pip_builder.connect(prev_stage, metadata_sender_pipeline->get_name());
    }
    pip_builder.connect(limiter_pipeline->get_name(), dpm_pipeline->get_name());
    pip_builder.connect(dpm_pipeline->get_name(),
                        cfg.analytics_db_config.stage_name.value_or(std::string(DPM_ANALYTICS_DB_STAGE)));

    auto pipeline = pip_builder.build(pipeline_name, true);
    pipeline->set_in_stage(tiling_pipeline);

    return pipeline;
}

} // namespace hailo_analytics::analytics::dpm_analytics
