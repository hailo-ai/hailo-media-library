#pragma once

/**
 * @file face_recognition.hpp
 * @brief Face recognition analytics pipeline with gaze tracking.
 *
 * Wraps the existing bbox_crop + landmarks pipeline and appends a gaze tracking
 * callback stage that classifies each face as "gaze: on" or "gaze: off" based on
 * 5-point landmark geometry (yaw/pitch estimation).
 **/

#include <deque>
#include <string_view>
#include <optional>
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/core/pipeline_database.hpp"
#include "hailo_analytics/analytics/face_landmarks.hpp"
#include "tl/expected.hpp"

namespace hailo_analytics::analytics::face_recognition
{

// Stage name constants
inline constexpr std::string_view GAZE_TRACKING_STAGE = "gaze_tracking";
inline constexpr std::string_view FACE_RECOGNITION_PIPELINE = "face_recognition_pipeline";
inline constexpr std::string_view QUALITY_GATE_STAGE = "face_rec_quality_gate";
inline constexpr std::string_view COMMIT_STAGE = "face_rec_commit";

/**
 * @brief Database entry for tracking-based face recognition rolling window.
 *
 * Stores a sliding window of the last N recognition results for a tracked face.
 * Once the same name appears >= M times in the window, the entry is confirmed
 * and subsequent frames skip the ArcFace inference, reusing the cached result.
 */
struct FaceRecognitionDBEntry : public hailo_analytics::pipeline::PipelineDBEntry
{
    struct RecognitionResult
    {
        std::string name;
        float similarity;
    };

    std::deque<RecognitionResult> window; ///< Rolling window of last N results
    int window_n;                         ///< Max window size
    int window_m;                         ///< Required match count
    bool confirmed = false;
    std::string confirmed_name;
    float confirmed_similarity = 0.0f;

    FaceRecognitionDBEntry(int n, int m) : window_n(n), window_m(m)
    {
    }

    /**
     * @brief Push a recognition result into the rolling window.
     * Trims the window to at most window_n entries.
     *
     * @param name The recognized name (empty string for unrecognized frames)
     * @param similarity The similarity score
     */
    void push_result(const std::string &name, float similarity);

    /**
     * @brief Check if the M/N threshold is met for any name in the window.
     * If passed, sets confirmed=true, confirmed_name, and confirmed_similarity.
     *
     * @return true if the threshold is met (entry is now confirmed)
     */
    bool check_threshold();
};

/**
 * @brief Configuration for the quality gate that gates recognition results
 * by requiring M consistent identifications in a rolling window of N frames.
 */
struct face_recognition_quality_gate_config_t
{
    std::optional<int> ttl_seconds;
    std::optional<size_t> max_entries;
    std::optional<int> window_n;
    std::optional<int> window_m;

    /**
     * @brief Merge configuration from another face_recognition_quality_gate_config_t.
     * Non-empty optional values from 'other' will override this config's values.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const face_recognition_quality_gate_config_t &other);
};

/**
 * @brief Configuration for gaze tracking thresholds.
 *
 * Controls the yaw and pitch angle thresholds (in degrees) used to classify
 * whether a face is looking at the camera ("gaze: on") or away ("gaze: off").
 * Also allows overriding the geometric scale factors used in yaw/pitch estimation.
 */
struct gaze_config_t
{
    std::optional<float> yaw_threshold_degrees;   ///< Maximum absolute yaw for "gaze: on" (default: 15.0)
    std::optional<float> pitch_threshold_degrees; ///< Maximum absolute pitch for "gaze: on" (default: 20.0)
    std::optional<float> yaw_scale_factor; ///< Multiplier converting nose-offset ratio to yaw degrees (default: 60.0)
    std::optional<float>
        pitch_scale_factor; ///< Multiplier converting nose-position ratio to pitch degrees (default: 120.0)
    std::optional<float> neutral_nose_y_ratio; ///< Expected nose Y position for a neutral face (default: 0.45)

    /**
     * @brief Merge configuration from another gaze_config_t.
     * Non-empty optional values from 'other' will override this config's values.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const gaze_config_t &other);
};

/**
 * @brief Combined configuration for face recognition pipeline.
 *
 * Wraps the existing bbox_crop + landmarks configuration together with
 * gaze tracking thresholds.
 */
struct face_recognition_config_t
{
    face_landmarks::bbox_crop_landmarks_config_t landmarks_config;
    gaze_config_t gaze_config;
    face_recognition_quality_gate_config_t quality_gate_config;

    /**
     * @brief Merge configuration from another face_recognition_config_t.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const face_recognition_config_t &other);
};

/**
 * @brief Get default configuration for gaze tracking.
 *
 * @return gaze_config_t with sensible defaults
 */
gaze_config_t gaze_base_config();

/**
 * @brief Get default configuration for quality gate.
 *
 * @return face_recognition_quality_gate_config_t with sensible defaults
 */
face_recognition_quality_gate_config_t quality_gate_base_config();

/**
 * @brief Get default configuration for face recognition pipeline.
 *
 * @return face_recognition_config_t with sensible defaults
 */
face_recognition_config_t base_config();

/**
 * @brief Generate a face recognition pipeline with gaze tracking.
 *
 * Creates a pipeline that:
 * - Runs bbox crop + face landmarks (via generate_bbox_landmarks_pipeline)
 * - Appends a gaze tracking callback stage that estimates head pose from
 *   5-point SCRFD landmarks and classifies as "gaze: on" or "gaze: off"
 *
 * @param pipeline_name Name for the generated pipeline
 * @param configs Optional user-provided configuration (will be merged with defaults if provided)
 * @return Expected<PipelinePtr, AppStatus> The constructed face recognition pipeline or error status
 */
tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_face_recognition_pipeline(const std::string &pipeline_name = std::string(FACE_RECOGNITION_PIPELINE),
                                   std::optional<face_recognition_config_t> configs = std::nullopt);

} // namespace hailo_analytics::analytics::face_recognition
