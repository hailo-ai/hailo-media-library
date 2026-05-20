#include "hailo_analytics/analytics/face_recognition.hpp"
#include "hailo_analytics/pipeline/core/pipeline_database.hpp"
#include "hailo_analytics/pipeline/routing/callback_stage.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace hailo_analytics::analytics::face_recognition
{

namespace routing = hailo_analytics::pipeline::routing;

// Gaze estimation constants
static constexpr float DEFAULT_YAW_THRESHOLD_DEGREES = 15.0f;
static constexpr float DEFAULT_PITCH_THRESHOLD_DEGREES = 20.0f;
static constexpr float DEGENERATE_THRESHOLD = 1e-6f;
static constexpr float YAW_SCALE_FACTOR = 80.0f;
static constexpr float PITCH_SCALE_FACTOR = 150.0f;
static constexpr float NEUTRAL_NOSE_Y_RATIO = 0.45f;
static constexpr float MAX_ANGLE_DEGREES = 90.0f;
static constexpr size_t REQUIRED_LANDMARK_COUNT = 5;
static constexpr std::string_view GAZE_CLASSIFICATION_TYPE = "gaze";
static constexpr std::string_view GAZE_ON_LABEL = "on";
static constexpr std::string_view GAZE_OFF_LABEL = "off";

// Quality gate constants
static constexpr int DEFAULT_QUALITY_GATE_TTL_SECONDS = 30;
static constexpr size_t DEFAULT_QUALITY_GATE_MAX_ENTRIES = 1000;
static constexpr int DEFAULT_WINDOW_N = 30;
static constexpr int DEFAULT_WINDOW_M = 10;
static constexpr std::string_view RECOGNITION_CLASSIFICATION_TYPE = "recognition";

// SCRFD 5-point landmark indices
static constexpr size_t LEFT_EYE_IDX = 0;
static constexpr size_t RIGHT_EYE_IDX = 1;
static constexpr size_t NOSE_IDX = 2;
static constexpr size_t LEFT_MOUTH_IDX = 3;
static constexpr size_t RIGHT_MOUTH_IDX = 4;

namespace
{

/**
 * @brief Estimate gaze from 5-point face landmarks and attach classification to detection.
 *
 * Computes approximate yaw and pitch from the geometric relationship between
 * the 5 SCRFD landmarks (left eye, right eye, nose, left mouth, right mouth).
 * Classifies as "gaze: on" if both yaw and pitch are below their respective thresholds.
 *
 * @param detection The face detection containing landmarks
 * @param yaw_threshold Maximum absolute yaw angle (degrees) for "gaze: on"
 * @param pitch_threshold Maximum absolute pitch angle (degrees) for "gaze: on"
 */
void classify_gaze_for_detection(const HailoDetectionPtr &detection, float yaw_threshold, float pitch_threshold,
                                 float yaw_scale, float pitch_scale, float neutral_nose_y)
{
    auto landmarks_vec = hailo_common::get_hailo_landmarks(detection);
    if (landmarks_vec.empty())
    {
        return;
    }

    // Use the first landmarks object (SCRFD attaches one set per detection)
    auto points = landmarks_vec[0]->get_points();
    if (points.size() != REQUIRED_LANDMARK_COUNT)
    {
        HAILO_ANALYTICS_LOG_TRACE("Gaze: skipping detection with {} landmarks (need {})", points.size(),
                                  REQUIRED_LANDMARK_COUNT);
        return;
    }

    // Extract the 5 landmark points (normalized coordinates relative to detection bbox)
    float left_eye_x = points[LEFT_EYE_IDX].x();
    float left_eye_y = points[LEFT_EYE_IDX].y();
    float right_eye_x = points[RIGHT_EYE_IDX].x();
    float right_eye_y = points[RIGHT_EYE_IDX].y();
    float nose_x = points[NOSE_IDX].x();
    float nose_y = points[NOSE_IDX].y();
    float left_mouth_y = points[LEFT_MOUTH_IDX].y();
    float right_mouth_y = points[RIGHT_MOUTH_IDX].y();

    // Yaw estimation: nose horizontal offset from eye midpoint, normalized by inter-eye distance
    float eye_center_x = (left_eye_x + right_eye_x) / 2.0f;
    float inter_eye_dist = right_eye_x - left_eye_x;
    if (std::abs(inter_eye_dist) < DEGENERATE_THRESHOLD)
    {
        return; // Degenerate case: eyes at same x position
    }
    float nose_offset_x = (nose_x - eye_center_x) / inter_eye_dist;
    float yaw_degrees = nose_offset_x * yaw_scale;

    // Pitch estimation: nose vertical position relative to eye-to-mouth span
    float eye_center_y = (left_eye_y + right_eye_y) / 2.0f;
    float mouth_center_y = (left_mouth_y + right_mouth_y) / 2.0f;
    float face_height = mouth_center_y - eye_center_y;
    if (std::abs(face_height) < DEGENERATE_THRESHOLD)
    {
        return; // Degenerate case: eyes and mouth at same y position
    }
    float nose_relative_y = (nose_y - eye_center_y) / face_height;
    float pitch_degrees = (nose_relative_y - neutral_nose_y) * pitch_scale;

    // Classify gaze
    bool gaze_on = (std::abs(yaw_degrees) < yaw_threshold) && (std::abs(pitch_degrees) < pitch_threshold);

    // Compute confidence: how centrally the person is looking
    float yaw_confidence = 1.0f - std::min(std::abs(yaw_degrees) / MAX_ANGLE_DEGREES, 1.0f);
    float pitch_confidence = 1.0f - std::min(std::abs(pitch_degrees) / MAX_ANGLE_DEGREES, 1.0f);
    float gaze_confidence = (yaw_confidence + pitch_confidence) / 2.0f;

    // Remove any existing gaze classification before adding a new one
    auto existing = hailo_common::get_hailo_classifications(detection, std::string(GAZE_CLASSIFICATION_TYPE));
    for (const auto &c : existing)
    {
        detection->remove_object(c);
    }

    std::string_view label = gaze_on ? GAZE_ON_LABEL : GAZE_OFF_LABEL;
    auto classification = std::make_shared<HailoClassification>(std::string(GAZE_CLASSIFICATION_TYPE),
                                                                std::string(label), gaze_confidence);
    detection->add_object(classification);

    HAILO_ANALYTICS_LOG_TRACE("Gaze: yaw={:.1f} pitch={:.1f} -> {} (conf={:.2f})", yaw_degrees, pitch_degrees, label,
                              gaze_confidence);
}

/**
 * @brief Gaze tracking callback for the CallbackStage.
 *
 * First restores skipped detections (confirmed tracks that bypassed AI inference)
 * back into the ROI so they also receive gaze estimation. Then iterates over all
 * face detections in the buffer's ROI, estimates gaze direction from landmarks,
 * and attaches a HailoClassification ("gaze: on" / "gaze: off") to each detection.
 */
void gaze_tracking_callback(hailo_analytics::pipeline::BufferPtr data, float yaw_threshold, float pitch_threshold,
                            float yaw_scale, float pitch_scale, float neutral_nose_y)
{
    auto roi = data->get_roi();
    if (!roi)
    {
        return;
    }

    // Restore skipped detections before gaze estimation so confirmed tracks also get gaze
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

    auto detections = hailo_common::get_hailo_detections(roi);
    for (const auto &detection : detections)
    {
        classify_gaze_for_detection(detection, yaw_threshold, pitch_threshold, yaw_scale, pitch_scale, neutral_nose_y);
    }
}

/**
 * @brief Quality gate callback: skip ArcFace inference for confirmed tracks.
 *
 * For each detection with a tracking ID, checks the database. If the track
 * is already confirmed, attaches the cached recognition classification and
 * moves the detection to the skipped list (bypassing AI inference).
 * Detections without tracking IDs (enrollment app) pass through unconditionally.
 */
void quality_gate_callback(hailo_analytics::pipeline::BufferPtr data,
                           hailo_analytics::pipeline::PipelineDatabasePtr database)
{
    auto roi = data->get_roi();
    if (!roi)
        return;

    auto detections = hailo_common::get_hailo_detections(roi);
    std::vector<HailoDetectionPtr> skipped;

    for (const auto &detection : detections)
    {
        auto unique_ids = hailo_common::get_hailo_unique_id(detection);
        if (unique_ids.empty())
            continue; // No tracking ID (enrollment app) — pass through

        int tracking_id = unique_ids[0]->get_id();
        auto base_entry = database->get(tracking_id);
        if (!base_entry)
            continue; // No entry yet — pass through for AI

        auto entry = std::dynamic_pointer_cast<FaceRecognitionDBEntry>(base_entry);
        if (!entry || !entry->confirmed)
            continue; // Not confirmed yet — pass through for AI

        // Confirmed track: attach cached classification, refresh TTL, and skip AI
        auto cached_classification = std::make_shared<HailoClassification>(
            std::string(RECOGNITION_CLASSIFICATION_TYPE), 1, entry->confirmed_name, entry->confirmed_similarity);
        detection->add_object(cached_classification);
        entry->last_updated = std::chrono::steady_clock::now();
        database->put(tracking_id, entry);
        skipped.push_back(detection);
    }

    if (!skipped.empty())
    {
        hailo_common::remove_detections(roi, skipped);
        auto skipped_metadata = std::make_shared<hailo_analytics::pipeline::SkippedDetectionsMetadata>(skipped);
        data->add_metadata(skipped_metadata);
    }
}

/**
 * @brief Commit callback: update the rolling window database after AI processing.
 *
 * For each detection with a tracking ID, extracts the "recognition" classification
 * result and pushes it into the per-track rolling window. If the M/N threshold is
 * not yet met, removes the classification to prevent premature downstream emission.
 * Re-adds skipped detections from the quality gate back to the ROI.
 */
void commit_callback(hailo_analytics::pipeline::BufferPtr data, hailo_analytics::pipeline::PipelineDatabasePtr database,
                     int window_n, int window_m)
{
    auto roi = data->get_roi();
    if (!roi)
        return;

    auto detections = hailo_common::get_hailo_detections(roi);
    for (const auto &detection : detections)
    {
        auto unique_ids = hailo_common::get_hailo_unique_id(detection);
        if (unique_ids.empty())
            continue;

        int tracking_id = unique_ids[0]->get_id();

        // Get or create entry
        auto base_entry = database->get(tracking_id);
        std::shared_ptr<FaceRecognitionDBEntry> entry;
        if (base_entry)
        {
            entry = std::dynamic_pointer_cast<FaceRecognitionDBEntry>(base_entry);
        }
        if (!entry)
        {
            entry = std::make_shared<FaceRecognitionDBEntry>(window_n, window_m);
        }

        // Get recognition classification
        auto classifications =
            hailo_common::get_hailo_classifications(detection, std::string(RECOGNITION_CLASSIFICATION_TYPE));

        if (classifications.empty())
        {
            // No recognition result — push empty (counts as a miss)
            entry->push_result("", 0.0f);
        }
        else
        {
            const auto &best = classifications[0];
            entry->push_result(best->get_label(), best->get_confidence());
        }

        entry->check_threshold();

        // If not confirmed, remove any recognition classification (gating)
        if (!entry->confirmed)
        {
            for (const auto &cls : classifications)
            {
                detection->remove_object(cls);
            }
        }

        database->put(tracking_id, entry);
    }
}

} // anonymous namespace

// ============================================================================
// FaceRecognitionDBEntry implementation
// ============================================================================

void FaceRecognitionDBEntry::push_result(const std::string &name, float similarity)
{
    window.push_back({name, similarity});
    if (static_cast<int>(window.size()) > window_n)
    {
        window.pop_front();
    }
}

bool FaceRecognitionDBEntry::check_threshold()
{
    if (confirmed)
        return true;

    // Count occurrences of each non-empty name in the window
    std::unordered_map<std::string, int> name_counts;
    std::unordered_map<std::string, float> name_max_similarity;

    for (const auto &result : window)
    {
        if (result.name.empty())
            continue;

        name_counts[result.name]++;
        auto it = name_max_similarity.find(result.name);
        if (it == name_max_similarity.end() || result.similarity > it->second)
        {
            name_max_similarity[result.name] = result.similarity;
        }
    }

    // Find the name with the highest count; break ties by similarity
    std::string best_name;
    int best_count = 0;
    float best_similarity = 0.0f;

    for (const auto &[name, count] : name_counts)
    {
        if (count > best_count || (count == best_count && name_max_similarity[name] > best_similarity))
        {
            best_count = count;
            best_name = name;
            best_similarity = name_max_similarity[name];
        }
    }

    if (best_count >= window_m)
    {
        confirmed = true;
        confirmed_name = best_name;
        confirmed_similarity = best_similarity;
        return true;
    }

    return false;
}

// ============================================================================
// face_recognition_quality_gate_config_t implementation
// ============================================================================

void face_recognition_quality_gate_config_t::merge_from(const face_recognition_quality_gate_config_t &other)
{
    if (other.ttl_seconds)
        ttl_seconds = *other.ttl_seconds;
    if (other.max_entries)
        max_entries = *other.max_entries;
    if (other.window_n)
        window_n = *other.window_n;
    if (other.window_m)
        window_m = *other.window_m;
}

// ============================================================================
// gaze_config_t implementation
// ============================================================================

void gaze_config_t::merge_from(const gaze_config_t &other)
{
    if (other.yaw_threshold_degrees)
        yaw_threshold_degrees = *other.yaw_threshold_degrees;
    if (other.pitch_threshold_degrees)
        pitch_threshold_degrees = *other.pitch_threshold_degrees;
    if (other.yaw_scale_factor)
        yaw_scale_factor = *other.yaw_scale_factor;
    if (other.pitch_scale_factor)
        pitch_scale_factor = *other.pitch_scale_factor;
    if (other.neutral_nose_y_ratio)
        neutral_nose_y_ratio = *other.neutral_nose_y_ratio;
}

// ============================================================================
// face_recognition_config_t implementation
// ============================================================================

void face_recognition_config_t::merge_from(const face_recognition_config_t &other)
{
    landmarks_config.merge_from(other.landmarks_config);
    gaze_config.merge_from(other.gaze_config);
    quality_gate_config.merge_from(other.quality_gate_config);
}

// ============================================================================
// Configuration defaults
// ============================================================================

gaze_config_t gaze_base_config()
{
    return {
        .yaw_threshold_degrees = DEFAULT_YAW_THRESHOLD_DEGREES,
        .pitch_threshold_degrees = DEFAULT_PITCH_THRESHOLD_DEGREES,
        .yaw_scale_factor = YAW_SCALE_FACTOR,
        .pitch_scale_factor = PITCH_SCALE_FACTOR,
        .neutral_nose_y_ratio = NEUTRAL_NOSE_Y_RATIO,
    };
}

face_recognition_quality_gate_config_t quality_gate_base_config()
{
    return {
        .ttl_seconds = DEFAULT_QUALITY_GATE_TTL_SECONDS,
        .max_entries = DEFAULT_QUALITY_GATE_MAX_ENTRIES,
        .window_n = DEFAULT_WINDOW_N,
        .window_m = DEFAULT_WINDOW_M,
    };
}

face_recognition_config_t base_config()
{
    face_recognition_config_t config;
    config.landmarks_config = face_landmarks::base_config();
    config.gaze_config = gaze_base_config();
    config.quality_gate_config = quality_gate_base_config();
    return config;
}

// ============================================================================
// Pipeline generation
// ============================================================================

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_face_recognition_pipeline(const std::string &pipeline_name,
                                   std::optional<face_recognition_config_t> user_configs)
{
    face_recognition_config_t cfg = base_config();
    if (user_configs)
    {
        cfg.merge_from(*user_configs);
    }

    hailo_analytics::pipeline::PipelineBuilder pip_builder;

    auto landmarks_pipeline_result = face_landmarks::generate_bbox_landmarks_pipeline(
        std::string(face_landmarks::BBOX_CROP_LANDMARKS_PIPELINE), cfg.landmarks_config);
    if (!landmarks_pipeline_result)
    {
        return tl::unexpected(landmarks_pipeline_result.error());
    }
    auto landmarks_pipeline = *landmarks_pipeline_result;

    float yaw_threshold = *cfg.gaze_config.yaw_threshold_degrees;
    float pitch_threshold = *cfg.gaze_config.pitch_threshold_degrees;
    float yaw_scale = *cfg.gaze_config.yaw_scale_factor;
    float pitch_scale = *cfg.gaze_config.pitch_scale_factor;
    float neutral_nose_y = *cfg.gaze_config.neutral_nose_y_ratio;

    auto gaze_stage = routing::CallbackStageBuild::create()
                          .set_stage_name(std::string(GAZE_TRACKING_STAGE))
                          .set_queue_size_opt(5)
                          .set_leaky_opt(false)
                          .set_trace_opt(true)
                          .buildptr();

    gaze_stage->set_callback([yaw_threshold, pitch_threshold, yaw_scale, pitch_scale,
                              neutral_nose_y](hailo_analytics::pipeline::BufferPtr data) {
        gaze_tracking_callback(data, yaw_threshold, pitch_threshold, yaw_scale, pitch_scale, neutral_nose_y);
    });

    auto database = std::make_shared<hailo_analytics::pipeline::PipelineDatabase>(
        std::chrono::seconds(cfg.quality_gate_config.ttl_seconds.value()), cfg.quality_gate_config.max_entries.value());
    int window_n = cfg.quality_gate_config.window_n.value();
    int window_m = cfg.quality_gate_config.window_m.value();

    auto quality_gate_stage = routing::CallbackStageBuild::create()
                                  .set_stage_name(std::string(QUALITY_GATE_STAGE))
                                  .set_queue_size_opt(5)
                                  .set_leaky_opt(false)
                                  .set_trace_opt(true)
                                  .buildptr();

    quality_gate_stage->set_callback(
        [database](hailo_analytics::pipeline::BufferPtr data) { quality_gate_callback(data, database); });

    auto commit_stage = routing::CallbackStageBuild::create()
                            .set_stage_name(std::string(COMMIT_STAGE))
                            .set_queue_size_opt(5)
                            .set_leaky_opt(false)
                            .set_trace_opt(true)
                            .buildptr();

    commit_stage->set_callback([database, window_n, window_m](hailo_analytics::pipeline::BufferPtr data) {
        commit_callback(data, database, window_n, window_m);
    });

    pip_builder.add_stage(quality_gate_stage)
        .add_stage(landmarks_pipeline)
        .add_stage(gaze_stage)
        .add_stage(commit_stage);

    pip_builder.connect(std::string(QUALITY_GATE_STAGE), landmarks_pipeline->get_name());
    pip_builder.connect(landmarks_pipeline->get_name(), std::string(GAZE_TRACKING_STAGE));
    pip_builder.connect(std::string(GAZE_TRACKING_STAGE), std::string(COMMIT_STAGE));

    hailo_analytics::pipeline::PipelinePtr pipeline = pip_builder.build(pipeline_name, true);

    pipeline->set_in_stage(quality_gate_stage);
    pipeline->set_out_stage(commit_stage);

    return pipeline;
}

} // namespace hailo_analytics::analytics::face_recognition
