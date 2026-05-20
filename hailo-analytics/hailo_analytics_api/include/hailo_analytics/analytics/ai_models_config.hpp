#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "hailo_analytics/analytics/common_configs.hpp"
#include "hailo_analytics/utils/env_utils.hpp"

namespace hailo_analytics::analytics::ai_models
{

// ---------------------------------------------------------------------------
// Configurable deployment roots
// ---------------------------------------------------------------------------
//
// Bundles below store paths relative to one of three roots so the same binary
// can run with HEFs / post-process libraries / JSON configs installed under
// any prefix. Roots default to the current device layout but are overridable
// at process start via env vars and at runtime via the set_*_root() setters.

inline constexpr std::string_view DEFAULT_HEF_ROOT = "/home/root/apps";
inline constexpr std::string_view DEFAULT_POST_SO_ROOT = "/usr/lib/hailo-post-processes";
inline constexpr std::string_view DEFAULT_CONFIG_ROOT = "/home/root/apps";

// Function-local statics → exactly one instance per process across all TUs,
// initialised on first use from the env var if present.
inline std::string &hef_root()
{
    static std::string r = utils::get_env_variable("HAILO_HEF_ROOT", std::string(DEFAULT_HEF_ROOT));
    return r;
}

inline std::string &post_so_root()
{
    static std::string r = utils::get_env_variable("HAILO_POST_SO_ROOT", std::string(DEFAULT_POST_SO_ROOT));
    return r;
}

inline std::string &config_root()
{
    static std::string r = utils::get_env_variable("HAILO_CONFIG_ROOT", std::string(DEFAULT_CONFIG_ROOT));
    return r;
}

inline void set_hef_root(std::string root)
{
    hef_root() = std::move(root);
}

inline void set_post_so_root(std::string root)
{
    post_so_root() = std::move(root);
}

inline void set_config_root(std::string root)
{
    config_root() = std::move(root);
}

inline std::string resolve_hef(std::string_view rel)
{
    return hef_root() + "/" + std::string(rel);
}

inline std::string resolve_post_so(std::string_view rel)
{
    return post_so_root() + "/" + std::string(rel);
}

inline std::string resolve_config(std::string_view rel)
{
    return config_root() + "/" + std::string(rel);
}

// ---------------------------------------------------------------------------
// Model bundle registry
// ---------------------------------------------------------------------------

/**
 * @brief Bundle of paths describing a deployed AI model.
 *
 * Each entry pairs an HEF with the post-process .so / function / JSON it
 * needs at runtime. All path fields are relative; resolve at use time via
 * resolve_hef / resolve_post_so / resolve_config (see apply_to()).
 *
 * Empty post_so_relative + post_function_name means "no shared-lib
 * post-process" (the consuming pipeline stage decodes the tensor itself,
 * e.g. semantic segmentation, OCR). Empty post_config_relative means the
 * post-process has no JSON parameters.
 */
struct model_bundle_t
{
    std::string_view hef_relative;
    std::string_view post_so_relative;
    std::string_view post_function_name;
    std::string_view post_config_relative;
};

// Common post-process binary / JSON shared by both YOLOv8 detector variants.
inline constexpr std::string_view YOLO_HAILORTPP_POST_SO = "libyolo_hailortpp_post.so";
inline constexpr std::string_view YOLO_PERSONFACE_VEHICLE_PLATE_CONF = "shared/resources/configs/yolov8.json";

/// YOLOv8 nano person/vehicle/face/license_plate detector — small, default
/// detector used by face_landmarks, clip, lpr, dpm and the native stress apps.
inline constexpr model_bundle_t YOLOV8N = {
    .hef_relative = "shared/resources/hailo_yolov8n_384_640.hef",
    .post_so_relative = YOLO_HAILORTPP_POST_SO,
    .post_function_name = "hailo_yolov8n",
    .post_config_relative = YOLO_PERSONFACE_VEHICLE_PLATE_CONF,
};

/// YOLOv8 small person/vehicle/face/license_plate detector — used by the
/// webserver detection profile and smart_encoder_app.
inline constexpr model_bundle_t YOLOV8S = {
    .hef_relative = "shared/resources/hailo_yolov8s_384_640.hef",
    .post_so_relative = YOLO_HAILORTPP_POST_SO,
    .post_function_name = "hailo_yolov8s",
    .post_config_relative = YOLO_PERSONFACE_VEHICLE_PLATE_CONF,
};

/// MediaPipe-style facial landmarks regressor used by face_landmarks_app.
inline constexpr model_bundle_t FACE_LANDMARKS_LITE = {
    .hef_relative = "face_landmarks/resources/face_landmarks_lite.hef",
    .post_so_relative = "libmediapipe_post.so",
    .post_function_name = "facial_landmarks_nv12",
    .post_config_relative = "",
};

/// 3DDFA mobilenet face mesh used by the crops_without_ai internal app.
inline constexpr model_bundle_t TDDFA_MOBILENET = {
    .hef_relative = "face_landmarks/resources/tddfa_mobilenet_v1_nv12.hef",
    .post_so_relative = "",
    .post_function_name = "",
    .post_config_relative = "",
};

/// Linknet semantic segmentation used by the dynamic privacy mask pipeline.
inline constexpr model_bundle_t LINKNET_DPM_128 = {
    .hef_relative = "dynamic_privacy_mask/resources/linknet_mbv1_ss_dpm_128.hef",
    .post_so_relative = "liblinknet_post.so",
    .post_function_name = "linknet_post",
    .post_config_relative = "",
};

/// PaddleOCR v5 mobile recognition used by the LPR app for plate text decode.
inline constexpr model_bundle_t PADDLE_OCR = {
    .hef_relative = "license_plate_recognition/resources/paddle_ocr_v5_mobile_recognition.hef",
    .post_so_relative = "",
    .post_function_name = "",
    .post_config_relative = "",
};

/**
 * @brief Apply a model bundle to an AI + post-process config pair.
 *
 * Resolves bundle paths against the configured roots and writes them onto
 * @p cfg. Fields the bundle leaves empty are not written, so callers can opt
 * into a model whose post-process is handled by a dedicated stage rather
 * than a shared .so.
 */
inline void apply_to(const model_bundle_t &bundle, ai_postprocess_pair_config_t &cfg)
{
    cfg.ai_config.hef_path = resolve_hef(bundle.hef_relative);
    if (!bundle.post_so_relative.empty())
    {
        cfg.post_config.so_path = resolve_post_so(bundle.post_so_relative);
    }
    if (!bundle.post_function_name.empty())
    {
        cfg.post_config.function_name = std::string(bundle.post_function_name);
    }
    if (!bundle.post_config_relative.empty())
    {
        cfg.post_config.config_path = resolve_config(bundle.post_config_relative);
    }
}

} // namespace hailo_analytics::analytics::ai_models
