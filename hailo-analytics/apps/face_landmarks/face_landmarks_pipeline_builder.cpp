#include "face_landmarks_pipeline_builder.hpp"

#include <stdint.h>
#include <map>
#include <optional>
#include <string>

#include "hailo_analytics/analytics/ai_models_config.hpp"
#include "hailo_postprocess_tools/labels/hailo_yolov8n.hpp"
#include "hailo_analytics/analytics/common_configs.hpp"
#include "hailo_analytics/analytics/detection.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/utils/platform_utils.hpp"

namespace face_landmarks_app
{

namespace tiling = hailo_analytics::analytics::tiling;
namespace face_landmarks = hailo_analytics::analytics::face_landmarks;
namespace ai_models = hailo_analytics::analytics::ai_models;

static constexpr size_t TRACKER_QUEUE_SIZE_LOW_MEMORY = 1;

// Per-profile-category max_crops caps. Heavier on-chip IQ networks get lower caps, and the 15L
// runs those networks slower than the 15H, so it needs lower caps than the 15H for each category.
static constexpr size_t MAX_CROPS_HAILO15H_HDR = 50;
static constexpr size_t MAX_CROPS_HAILO15H_HDM = 27;
static constexpr size_t MAX_CROPS_HAILO15H_POST_ISP_DENOISE = 40;
static constexpr size_t MAX_CROPS_HAILO15H_PRE_ISP_DENOISE = 45;
static constexpr size_t MAX_CROPS_HAILO15H_DEFAULT = 50;
static constexpr size_t MAX_CROPS_HAILO15L_HDR = 30;
static constexpr size_t MAX_CROPS_HAILO15L_POST_ISP_DENOISE = 12;
static constexpr size_t MAX_CROPS_HAILO15L_PRE_ISP_DENOISE = 6;
static constexpr size_t MAX_CROPS_HAILO15L_DEFAULT = 30;
// The 15L HDM cap has not been evaluated yet, so face_landmarks is disabled there until it is measured.
static constexpr size_t MAX_CROPS_HAILO15L_HDM = 0;

bool is_hailo15h_hdr(const config_profile_t &profile)
{
    return hailo_analytics::utils::get_hailo_architecture() == hailo_analytics::utils::Architecture::Hailo15H &&
           profile.iq_settings.hdr.enabled;
}

namespace
{
enum class iq_category
{
    hdr,
    hdm,
    pre_isp_denoise,
    post_isp_denoise,
    none
};

iq_category classify_iq(const config_profile_t &profile)
{
    if (profile.iq_settings.hdr.enabled)
        return iq_category::hdr;

    const auto &denoise_config = profile.iq_settings.denoise;
    const auto &bayer_net = denoise_config.bayer_network_config;
    const bool is_hdm = !bayer_net.input_fusion_feedback.empty() && !bayer_net.output_fusion_feedback.empty() &&
                        !bayer_net.input_gamma_feedback.empty() && !bayer_net.output_gamma_feedback.empty();

    if (denoise_config.enabled && denoise_config.bayer && is_hdm)
        return iq_category::hdm;
    if (denoise_config.enabled && denoise_config.bayer)
        return iq_category::pre_isp_denoise;
    if (denoise_config.enabled)
        return iq_category::post_isp_denoise;
    return iq_category::none;
}
} // namespace

size_t get_max_crops(const config_profile_t &profile)
{
    const bool is_15l =
        hailo_analytics::utils::get_hailo_architecture() == hailo_analytics::utils::Architecture::Hailo15L;

    switch (classify_iq(profile))
    {
    case iq_category::hdr:
        return is_15l ? MAX_CROPS_HAILO15L_HDR : MAX_CROPS_HAILO15H_HDR;
    case iq_category::hdm:
        if (is_15l)
        {
            HAILO_ANALYTICS_LOG_WARN(
                "face_landmarks is disabled on Hailo15L HDM profiles: max_crops cap not evaluated");
            return MAX_CROPS_HAILO15L_HDM;
        }
        return MAX_CROPS_HAILO15H_HDM;
    case iq_category::pre_isp_denoise:
        return is_15l ? MAX_CROPS_HAILO15L_PRE_ISP_DENOISE : MAX_CROPS_HAILO15H_PRE_ISP_DENOISE;
    case iq_category::post_isp_denoise:
        return is_15l ? MAX_CROPS_HAILO15L_POST_ISP_DENOISE : MAX_CROPS_HAILO15H_POST_ISP_DENOISE;
    case iq_category::none:
        break;
    }
    return is_15l ? MAX_CROPS_HAILO15L_DEFAULT : MAX_CROPS_HAILO15H_DEFAULT;
}

tiling::tiling_detection_config_t default_tiling_config()
{
    auto cfg = tiling::base_config();
    ai_models::apply_to(ai_models::YOLOV8N, cfg.detection_config);
    cfg.detection_config.ai_config.use_hailort_service = false;
    cfg.tiling_config.queue_size = 2;
    cfg.aggregator_config.main_queue_size = 3;

    cfg.tracker_config.enabled = true;
    cfg.tracker_config.queue_size = TRACKER_QUEUE_SIZE_LOW_MEMORY;
    cfg.tracker_config.labels_map = common::hailo_yolov8n;

    return cfg;
}

face_landmarks::bbox_crop_landmarks_config_t default_landmarks_config(const config_profile_t &profile)
{
    auto cfg = face_landmarks::base_config();
    ai_models::apply_to(ai_models::FACE_LANDMARKS_LITE, cfg.landmarks_config);
    cfg.bbox_crop_config.queue_size = 1;
    cfg.bbox_crop_config.crop_every_x_frames = 2;
    cfg.bbox_crop_config.max_crops = get_max_crops(profile);
    // Leak the detection->landmarks boundary queue so the landmarks branch sheds load instead
    // of stalling detection. Re-applied on runtime profile switch (see FaceLandmarksPipeline).
    cfg.bbox_crop_config.leaky = is_hailo15h_hdr(profile);
    cfg.aggregator_config.main_queue_size = 3;
    cfg.aggregator_config.sub_queue_size = 20;
    cfg.landmarks_config.ai_config.queue_size = 20;
    cfg.landmarks_config.ai_config.use_hailort_service = false;
    cfg.landmarks_config.post_config.queue_size = 20;

    return cfg;
}

} // namespace face_landmarks_app
