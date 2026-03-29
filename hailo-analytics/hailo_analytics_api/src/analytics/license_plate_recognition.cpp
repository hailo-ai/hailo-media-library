#include "hailo_analytics/analytics/license_plate_recognition.hpp"
#include "hailo_analytics/pipeline/core/pipeline_database.hpp"
#include "hailo_analytics/pipeline/routing/callback_stage.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace hailo_analytics::analytics::license_plate_recognition
{

namespace ai_stages = hailo_analytics::pipeline::ai;
namespace cropping_stages = hailo_analytics::pipeline::cropping;
namespace routing = hailo_analytics::pipeline::routing;

static constexpr float DEFAULT_RE_OCR_CONFIDENCE_MARGIN = 0.2f;
static constexpr float DEFAULT_OCR_CONFIDENCE_THRESHOLD = 0.8f;
static constexpr float DEFAULT_MIN_PIXEL_RATIO = 0.1f;
static constexpr float DEFAULT_MAX_ASPECT_RATIO_DEVIATION = 0.8f;
static constexpr bool DEFAULT_REQUIRE_LP_IN_VEHICLE = true;
static constexpr std::string_view OCR_CLASSIFICATION_TYPE = "ocr";

namespace
{

/**
 * @brief Check if a detection should be skipped due to low pixel count.
 *
 * Compares the actual crop pixel count against the network's expected input pixels.
 * If the actual count is below the threshold ratio, the crop would require upscaling
 * which produces poor OCR results.
 *
 * @return true if the detection should be skipped (too few pixels)
 */
bool should_skip_low_pixel_count(const HailoDetectionPtr &detection, int input_width, int input_height,
                                 int output_width, int output_height, float min_pixel_ratio)
{
    auto bbox = detection->get_bbox();
    float actual_width = bbox.width() * static_cast<float>(input_width);
    float actual_height = bbox.height() * static_cast<float>(input_height);
    float actual_pixels = actual_width * actual_height;
    float required_pixels = static_cast<float>(output_width) * static_cast<float>(output_height) * min_pixel_ratio;

    if (actual_pixels < required_pixels)
    {
        HAILO_ANALYTICS_LOG_TRACE("Skipping detection: pixel count {:.0f} < required {:.0f}", actual_pixels,
                                  required_pixels);
        return true;
    }
    return false;
}

/**
 * @brief Check if a detection should be skipped due to bad aspect ratio.
 *
 * Compares the actual crop aspect ratio against the network's expected aspect ratio.
 * Large deviations indicate the crop shape is very different from what the network expects,
 * leading to distortion and poor OCR results.
 *
 * @return true if the detection should be skipped (aspect ratio too different)
 */
bool should_skip_bad_aspect_ratio(const HailoDetectionPtr &detection, int input_width, int input_height,
                                  int output_width, int output_height, float max_aspect_ratio_deviation)
{
    auto bbox = detection->get_bbox();
    float actual_width = bbox.width() * static_cast<float>(input_width);
    float actual_height = bbox.height() * static_cast<float>(input_height);

    if (actual_height <= 0.0f || output_height <= 0)
        return true;

    float actual_aspect_ratio = actual_width / actual_height;
    float expected_aspect_ratio = static_cast<float>(output_width) / static_cast<float>(output_height);

    if (expected_aspect_ratio <= 0.0f)
        return true;

    // Compute symmetric deviation: how far off the ratio is from 1.0
    float ratio = actual_aspect_ratio / expected_aspect_ratio;
    float deviation = std::abs(ratio - 1.0f);

    if (deviation > max_aspect_ratio_deviation)
    {
        HAILO_ANALYTICS_LOG_TRACE("Skipping detection: aspect ratio deviation {:.2f} > max {:.2f}", deviation,
                                  max_aspect_ratio_deviation);
        return true;
    }
    return false;
}

/**
 * @brief Check if a tracked detection should be skipped because it already has a cached OCR result.
 *
 * Looks up the detection's tracking ID in the database. If a cached result exists and the
 * current detection confidence is not significantly better, applies the cached classification
 * and returns true to skip re-running OCR.
 *
 * @return true if the detection should be skipped (cached result applied)
 */
bool should_skip_already_classified(const HailoDetectionPtr &detection,
                                    const hailo_analytics::pipeline::PipelineDatabasePtr &database, float re_ocr_margin)
{
    auto unique_ids = hailo_common::get_hailo_unique_id(detection);
    if (unique_ids.empty())
        return false;

    int tracking_id = unique_ids[0]->get_id();
    auto base_entry = database->get(tracking_id);
    if (!base_entry)
        return false;

    auto entry = std::dynamic_pointer_cast<LprDBEntry>(base_entry);
    if (!entry)
    {
        HAILO_ANALYTICS_LOG_WARN("Database entry for tracking_id {} is not an LprDBEntry — skipping", tracking_id);
        return false;
    }

    // If detection confidence is significantly higher than cached, re-run OCR
    float current_detection_confidence = detection->get_confidence();
    if (current_detection_confidence > entry->detection_confidence + re_ocr_margin)
        return false;

    // Confidence is not significantly better — use cached result, skip OCR
    auto cached_classification =
        std::make_shared<HailoClassification>(std::string(OCR_CLASSIFICATION_TYPE), entry->value, entry->confidence);
    detection->add_object(cached_classification);

    return true;
}

/**
 * @brief Check if a point (cx, cy) falls within a bounding box.
 */
bool is_point_inside_bbox(float cx, float cy, const HailoBBox &bbox)
{
    return (cx >= bbox.xmin() && cx <= bbox.xmax() && cy >= bbox.ymin() && cy <= bbox.ymax());
}

/**
 * @brief Find license plate detections that should be skipped because they are
 * not inside any vehicle detection, or because a higher-confidence LP already
 * exists in the same vehicle.
 *
 * @param detections All detections from the ROI
 * @return Vector of LP detections to skip
 */
std::vector<HailoDetectionPtr> find_lp_not_in_vehicle(const std::vector<HailoDetectionPtr> &detections)
{
    std::vector<HailoDetectionPtr> vehicle_detections;
    std::vector<HailoDetectionPtr> lp_detections;

    for (const auto &detection : detections)
    {
        const auto &label = detection->get_label();
        if (label == "vehicle")
            vehicle_detections.push_back(detection);
        else if (label == "license_plate")
            lp_detections.push_back(detection);
    }

    std::vector<HailoDetectionPtr> skipped;
    if (vehicle_detections.empty() || lp_detections.empty())
        return skipped;

    // Map each vehicle detection to its best (highest-confidence) LP
    std::unordered_map<size_t, HailoDetectionPtr> best_lp_per_vehicle;

    // For each LP, find which vehicle contains its center point.
    // Uses first-match policy: if a LP center falls in overlapping vehicles,
    // it is assigned to the first matching vehicle in iteration order.
    std::vector<int> lp_vehicle_index(lp_detections.size(), -1);

    for (size_t lp_idx = 0; lp_idx < lp_detections.size(); ++lp_idx)
    {
        const auto &lp = lp_detections[lp_idx];
        auto lp_bbox = lp->get_bbox();
        float lp_center_x = lp_bbox.xmin() + lp_bbox.width() / 2.0f;
        float lp_center_y = lp_bbox.ymin() + lp_bbox.height() / 2.0f;

        for (size_t v_idx = 0; v_idx < vehicle_detections.size(); ++v_idx)
        {
            if (is_point_inside_bbox(lp_center_x, lp_center_y, vehicle_detections[v_idx]->get_bbox()))
            {
                lp_vehicle_index[lp_idx] = static_cast<int>(v_idx);
                break;
            }
        }
    }

    // Group LPs by vehicle and keep only the highest-confidence one per vehicle
    for (size_t lp_idx = 0; lp_idx < lp_detections.size(); ++lp_idx)
    {
        int vehicle_idx = lp_vehicle_index[lp_idx];
        const auto &lp = lp_detections[lp_idx];

        if (vehicle_idx < 0)
        {
            HAILO_ANALYTICS_LOG_TRACE("Skipping LP detection: not inside any vehicle bbox");
            skipped.push_back(lp);
            continue;
        }

        size_t v_idx = static_cast<size_t>(vehicle_idx);
        auto it = best_lp_per_vehicle.find(v_idx);
        if (it == best_lp_per_vehicle.end())
        {
            best_lp_per_vehicle[v_idx] = lp;
        }
        else if (lp->get_confidence() > it->second->get_confidence())
        {
            // Current LP has higher confidence — demote the previous best
            skipped.push_back(it->second);
            it->second = lp;
        }
        else
        {
            // Current LP has lower or equal confidence — skip it
            skipped.push_back(lp);
        }
    }

    return skipped;
}

void quality_gate_callback(hailo_analytics::pipeline::BufferPtr data,
                           hailo_analytics::pipeline::PipelineDatabasePtr database,
                           const bbox_crop_ocr_config_t &config)
{
    auto roi = data->get_roi();
    if (!roi)
        return;

    const auto &crop_cfg = config.bbox_crop_config;
    const auto &gate_cfg = config.quality_gate_config;

    int input_width = crop_cfg.input_width.value();
    int input_height = crop_cfg.input_height.value();
    int output_width = crop_cfg.output_width.value();
    int output_height = crop_cfg.output_height.value();
    float re_ocr_margin = gate_cfg.re_ocr_confidence_margin.value();
    float min_pixel_ratio = gate_cfg.min_pixel_ratio.value();
    float max_aspect_ratio_deviation = gate_cfg.max_aspect_ratio_deviation.value();

    auto detections = hailo_common::get_hailo_detections(roi);
    std::vector<HailoDetectionPtr> skipped;

    // First check: skip LPs not inside any vehicle, and keep only the best LP per vehicle
    std::unordered_set<HailoDetectionPtr> vehicle_skip_set;
    bool require_lp_in_vehicle = gate_cfg.require_lp_in_vehicle.value();
    if (require_lp_in_vehicle)
    {
        auto lp_to_skip = find_lp_not_in_vehicle(detections);
        vehicle_skip_set.insert(lp_to_skip.begin(), lp_to_skip.end());
        skipped.insert(skipped.end(), lp_to_skip.begin(), lp_to_skip.end());
    }

    for (const auto &detection : detections)
    {
        if (detection->get_label() != "license_plate")
            continue;

        // Skip detections already marked by LP-in-vehicle check
        if (vehicle_skip_set.count(detection))
            continue;

        // Cheapest check first: pixel count
        if (should_skip_low_pixel_count(detection, input_width, input_height, output_width, output_height,
                                        min_pixel_ratio))
        {
            skipped.push_back(detection);
            continue;
        }

        // Second check: aspect ratio
        if (should_skip_bad_aspect_ratio(detection, input_width, input_height, output_width, output_height,
                                         max_aspect_ratio_deviation))
        {
            skipped.push_back(detection);
            continue;
        }

        // Third check: already classified via tracking cache
        if (should_skip_already_classified(detection, database, re_ocr_margin))
        {
            skipped.push_back(detection);
            continue;
        }
    }

    // Remove skipped detections from ROI and attach as metadata for later re-addition
    if (!skipped.empty())
    {
        hailo_common::remove_detections(roi, skipped);
        auto skipped_metadata = std::make_shared<hailo_analytics::pipeline::SkippedDetectionsMetadata>(skipped);
        data->add_metadata(skipped_metadata);
    }
}

void commit_callback(hailo_analytics::pipeline::BufferPtr data, hailo_analytics::pipeline::PipelineDatabasePtr database,
                     float ocr_confidence_threshold)
{
    auto roi = data->get_roi();
    if (!roi)
        return;

    // Write new OCR results to the database
    auto detections = hailo_common::get_hailo_detections(roi);
    for (const auto &detection : detections)
    {
        auto unique_ids = hailo_common::get_hailo_unique_id(detection);
        if (unique_ids.empty())
            continue;

        int tracking_id = unique_ids[0]->get_id();
        auto classifications = hailo_common::get_hailo_classifications(detection, std::string(OCR_CLASSIFICATION_TYPE));
        if (classifications.empty())
            continue;

        const auto &best = classifications[0];

        // Filter out low-confidence OCR results
        if (best->get_confidence() < ocr_confidence_threshold)
        {
            detection->remove_object(best);
            continue;
        }

        auto entry = std::make_shared<LprDBEntry>();
        entry->value = best->get_label();
        entry->confidence = best->get_confidence();
        entry->detection_confidence = detection->get_confidence();
        database->put(tracking_id, entry);
    }

    // Re-add skipped detections back to the ROI
    auto skipped_metadata_list =
        data->get_metadata_of_type(hailo_analytics::pipeline::MetadataType::SKIPPED_DETECTIONS);
    for (const auto &metadata : skipped_metadata_list)
    {
        auto skipped = std::dynamic_pointer_cast<hailo_analytics::pipeline::SkippedDetectionsMetadata>(metadata);
        if (!skipped)
            continue;

        for (const auto &detection : skipped->get_skipped_detections())
        {
            hailo_common::add_object(roi, detection);
        }
        data->remove_metadata(metadata);
    }
}

} // anonymous namespace

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
}

// ============================================================================
// bbox_crop_ocr_config_t implementation
// ============================================================================

void quality_gate_config_t::merge_from(const quality_gate_config_t &other)
{
    if (other.ttl_seconds)
        ttl_seconds = *other.ttl_seconds;
    if (other.max_entries)
        max_entries = *other.max_entries;
    if (other.re_ocr_confidence_margin)
        re_ocr_confidence_margin = *other.re_ocr_confidence_margin;
    if (other.ocr_confidence_threshold)
        ocr_confidence_threshold = *other.ocr_confidence_threshold;
    if (other.min_pixel_ratio)
        min_pixel_ratio = *other.min_pixel_ratio;
    if (other.max_aspect_ratio_deviation)
        max_aspect_ratio_deviation = *other.max_aspect_ratio_deviation;
    if (other.require_lp_in_vehicle)
        require_lp_in_vehicle = *other.require_lp_in_vehicle;
}

void bbox_crop_ocr_config_t::merge_from(const bbox_crop_ocr_config_t &other)
{
    bbox_crop_config.merge_from(other.bbox_crop_config);
    ocr_config.merge_from(other.ocr_config);
    aggregator_config.merge_from(other.aggregator_config);
    quality_gate_config.merge_from(other.quality_gate_config);
}

void bbox_crop_ocr_config_t::apply_to(cropping_stages::BBoxCropStageBuild::Builder &b) const
{
    bbox_crop_config.apply_to(b);
}

void bbox_crop_ocr_config_t::apply_to(cropping_stages::AggregatorStageBuild::Builder &b) const
{
    aggregator_config.apply_to(b);
}

// ============================================================================
// Configuration defaults
// ============================================================================

ocr_config_t ocr_base_config()
{
    ocr_config_t config;

    // Set default AI stage configs
    config.ai_config.stage_name = std::string(OCR_STAGE);
    config.ai_config.hef_path = std::string(OCR_BASE_HEF);
    config.ai_config.queue_size = 5;
    config.ai_config.output_pool_size = 50;
    config.ai_config.group_id = std::string(OCR_GROUP_ID);
    config.ai_config.batch_size = 1;
    config.ai_config.job_limit = 10;
    config.ai_config.scheduler_threshold = 1;
    config.ai_config.dynamic_threshold = false;
    config.ai_config.scheduler_timeout = std::chrono::milliseconds(100);
    config.ai_config.pool_mode = hailo_analytics::pipeline::StagePoolMode::BLOCKING;
    config.ai_config.trace = true;

    // Set default postprocess configs
    config.post_config.stage_name = std::string(OCR_POST_STAGE);
    config.post_config.so_path = std::string(OCR_POST_SO);
    config.post_config.queue_size = 5;
    config.post_config.leaky = false;
    config.post_config.trace = true;

    return config;
}

quality_gate_config_t quality_gate_base_config()
{
    quality_gate_config_t config;
    config.ttl_seconds = 30;
    config.max_entries = 1000;
    config.re_ocr_confidence_margin = DEFAULT_RE_OCR_CONFIDENCE_MARGIN;
    config.ocr_confidence_threshold = DEFAULT_OCR_CONFIDENCE_THRESHOLD;
    config.min_pixel_ratio = DEFAULT_MIN_PIXEL_RATIO;
    config.max_aspect_ratio_deviation = DEFAULT_MAX_ASPECT_RATIO_DEVIATION;
    config.require_lp_in_vehicle = DEFAULT_REQUIRE_LP_IN_VEHICLE;
    return config;
}

bbox_crop_ocr_config_t base_config()
{
    bbox_crop_ocr_config_t config;

    // Set default bbox crop stage configs
    config.bbox_crop_config.stage_name = std::string(BBOX_CROP_STAGE);
    config.bbox_crop_config.output_pool_size = 150;
    config.bbox_crop_config.input_width = 3840;
    config.bbox_crop_config.input_height = 2160;
    config.bbox_crop_config.output_width = 320;
    config.bbox_crop_config.output_height = 48;
    config.bbox_crop_config.main_sub_name = std::string(OCR_AGGREGATOR_STAGE);
    config.bbox_crop_config.sub_sub_name = std::string(OCR_SUBPIPELINE);
    config.bbox_crop_config.labels = std::vector<std::string>{"license_plate"};
    config.bbox_crop_config.queue_size = 5;
    config.bbox_crop_config.leaky = false;
    config.bbox_crop_config.trace = true;
    config.bbox_crop_config.pool_mode = hailo_analytics::pipeline::StagePoolMode::BLOCKING;
    config.bbox_crop_config.crop_every_x_frames = 1;

    // Set default OCR configs (reuse ocr_base_config)
    config.ocr_config = ocr_base_config();

    // Set default aggregator configs
    config.aggregator_config.stage_name = std::string(OCR_AGGREGATOR_STAGE);
    config.aggregator_config.main_inlet_name = std::string(BBOX_CROP_STAGE);
    config.aggregator_config.main_queue_size = 3;
    config.aggregator_config.main_queue_leaky = false;
    config.aggregator_config.sub_inlet_name = std::string(OCR_POST_STAGE);
    config.aggregator_config.sub_queue_size = 100;
    config.aggregator_config.sub_queue_leaky = false;
    config.aggregator_config.multi_scale = false;
    config.aggregator_config.skip_migration = false;
    config.aggregator_config.trace = true;
    config.aggregator_config.iou_threshold = 0.3f;
    config.aggregator_config.border_threshold = 0.1f;

    // Set default quality gate configs
    config.quality_gate_config = quality_gate_base_config();

    return config;
}

// ============================================================================
// Pipeline generation functions
// ============================================================================

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> generate_ocr_pipeline(
    const std::string &pipeline_name, std::optional<ocr_config_t> user_configs)
{
    ocr_config_t cfg = ocr_base_config();
    if (user_configs)
        cfg.merge_from(*user_configs);

    hailo_analytics::pipeline::PipelineBuilder pip_builder;

    auto ai_stage_builder = ai_stages::HailortAsyncStageBuild::create();
    auto post_stage_builder = ai_stages::PostprocessStageBuild::create();

    cfg.apply_to(ai_stage_builder);
    cfg.apply_to(post_stage_builder);

    auto ocr_stage = ai_stage_builder.buildptr();
    auto ocr_post_stage = post_stage_builder.buildptr();

    pip_builder.add_stage(ocr_stage).add_stage(ocr_post_stage);

    pip_builder.connect(cfg.ai_config.stage_name.value(), cfg.post_config.stage_name.value());

    hailo_analytics::pipeline::PipelinePtr pipeline = pip_builder.build(pipeline_name, true);

    pipeline->set_in_stage(ocr_stage);
    pipeline->set_out_stage(ocr_post_stage);

    return pipeline;
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_bbox_crop_ocr_pipeline(const std::string &pipeline_name, std::optional<bbox_crop_ocr_config_t> user_configs)
{
    bbox_crop_ocr_config_t cfg = base_config();
    if (user_configs)
        cfg.merge_from(*user_configs);

    // Create pipeline builder
    hailo_analytics::pipeline::PipelineBuilder pip_builder;

    // Create shared database for quality gate
    auto database = std::make_shared<hailo_analytics::pipeline::PipelineDatabase>(
        std::chrono::seconds(cfg.quality_gate_config.ttl_seconds.value()), cfg.quality_gate_config.max_entries.value());
    const float ocr_confidence_threshold = cfg.quality_gate_config.ocr_confidence_threshold.value();

    // Build quality gate callback stage
    auto quality_gate_stage = routing::CallbackStageBuild::create()
                                  .set_stage_name(std::string(QUALITY_GATE_STAGE))
                                  .set_queue_size_opt(1)
                                  .set_leaky_opt(false)
                                  .set_trace_opt(true)
                                  .buildptr();

    quality_gate_stage->set_callback(
        [database, cfg](hailo_analytics::pipeline::BufferPtr data) { quality_gate_callback(data, database, cfg); });

    // Build commit callback stage
    auto commit_stage = routing::CallbackStageBuild::create()
                            .set_stage_name(std::string(COMMIT_STAGE))
                            .set_queue_size_opt(5)
                            .set_leaky_opt(false)
                            .set_trace_opt(true)
                            .buildptr();

    commit_stage->set_callback([database, ocr_confidence_threshold](hailo_analytics::pipeline::BufferPtr data) {
        commit_callback(data, database, ocr_confidence_threshold);
    });

    // Create bbox crop stage
    auto bbox_crop_builder = cropping_stages::BBoxCropStageBuild::create();
    cfg.apply_to(bbox_crop_builder);
    auto bbox_crop_stage = bbox_crop_builder.buildptr();

    // Generate OCR sub-pipeline using the OCR generator
    auto ocr_pipeline_result = generate_ocr_pipeline(std::string(OCR_SUBPIPELINE), cfg.ocr_config);
    if (!ocr_pipeline_result)
        return tl::unexpected(ocr_pipeline_result.error());

    auto ocr_pipeline = ocr_pipeline_result.value();

    // Create aggregator stage
    auto aggregator_builder = cropping_stages::AggregatorStageBuild::create();
    cfg.apply_to(aggregator_builder);
    auto aggregator_stage = aggregator_builder.buildptr();

    // Add stages to pipeline builder
    pip_builder.add_stage(quality_gate_stage)
        .add_stage(bbox_crop_stage)
        .add_stage(ocr_pipeline)
        .add_stage(aggregator_stage)
        .add_stage(commit_stage);

    // Connect the stages within the pipeline
    // Quality gate -> BBox crop
    pip_builder.connect(std::string(QUALITY_GATE_STAGE), cfg.bbox_crop_config.stage_name.value());

    // BBox crop main output -> Aggregator main input
    pip_builder.connect(cfg.bbox_crop_config.stage_name.value(), cfg.aggregator_config.stage_name.value());

    // BBox crops -> OCR Pipeline
    pip_builder.connect(cfg.bbox_crop_config.stage_name.value(), ocr_pipeline->get_name());

    // OCR Pipeline -> Aggregator sub input
    pip_builder.connect(ocr_pipeline->get_name(), cfg.aggregator_config.stage_name.value());

    // Aggregator -> Commit stage
    pip_builder.connect(cfg.aggregator_config.stage_name.value(), std::string(COMMIT_STAGE));

    // Create the pipeline
    hailo_analytics::pipeline::PipelinePtr pipeline = pip_builder.build(pipeline_name, true);

    // Set the input and output stages
    pipeline->set_in_stage(quality_gate_stage);
    pipeline->set_out_stage(commit_stage);

    return pipeline;
}

} // namespace hailo_analytics::analytics::license_plate_recognition
