#pragma once
#include "pipeline/isp_blender.hpp"
#include "resources/common/repository.hpp"
#include "media_library/media_library.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/codecs/encoder_stage.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "hailo_analytics/pipeline/sinks/udp_stage.hpp"
#include "hailo_analytics/pipeline/overlay/overlay_stage.hpp"
#include "hailo_analytics/pipeline/routing/valve_stage.hpp"
#include "hailo_analytics/pipeline/routing/freeze_stage.hpp"
#include "hailo_analytics/pipeline/sinks/rtp_converter_stage.hpp"

#define DEFAULT_STREAM_4K_NAME "sink0"

#define BASIC_DAYLIGHT_PROFILE_NAME "Daylight_Basic"
#define BASIC_AI_ISP_GEN1_PROFILE_NAME "AI_ISP_Gen1_Basic"
#define BASIC_HDR_PROFILE_NAME "High_Dynamic_Range_Basic"
#define BASIC_AI_ISP_GEN2_PROFILE_NAME "AI_ISP_Gen2_Basic"
#define BASIC_AI_ISP_GEN3_PROFILE_NAME "AI_ISP_Gen3_Basic"
#define BASIC_AI_ISP_GEN3_1_PROFILE_NAME "AI_ISP_Gen3_1_Basic"

#define DETECTION_DAYLIGHT_PROFILE_NAME "Daylight_Detection"
#define DETECTION_AI_ISP_GEN1_PROFILE_NAME "AI_ISP_Gen1_Detection"
#define DETECTION_HDR_PROFILE_NAME "High_Dynamic_Range_Detection"
#define DETECTION_AI_ISP_GEN2_PROFILE_NAME "AI_ISP_Gen2_Detection"
#define DETECTION_AI_ISP_GEN3_PROFILE_NAME "AI_ISP_Gen3_Detection"

#define FACE_LANDMARKS_DAYLIGHT_PROFILE_NAME "Daylight_FaceLandmarks"
#define FACE_LANDMARKS_AI_ISP_GEN1_PROFILE_NAME "AI_ISP_Gen1_FaceLandmarks"
#define FACE_LANDMARKS_HDR_PROFILE_NAME "High_Dynamic_Range_FaceLandmarks"
#define FACE_LANDMARKS_AI_ISP_GEN2_PROFILE_NAME "AI_ISP_Gen2_FaceLandmarks"
#define FACE_LANDMARKS_AI_ISP_GEN3_PROFILE_NAME "AI_ISP_Gen3_FaceLandmarks"

#define CLIP_PROFILE_NAME "Daylight_Clip"

#define LICENSE_PLATE_DAYLIGHT_PROFILE_NAME "Daylight_LicensePlate"
#define LICENSE_PLATE_AI_ISP_GEN1_PROFILE_NAME "AI_ISP_Gen1_LicensePlate"
#define LICENSE_PLATE_HDR_PROFILE_NAME "High_Dynamic_Range_LicensePlate"
#define LICENSE_PLATE_AI_ISP_GEN2_PROFILE_NAME "AI_ISP_Gen2_LicensePlate"
#define LICENSE_PLATE_AI_ISP_GEN3_PROFILE_NAME "AI_ISP_Gen3_LicensePlate"

#define DPM_DAYLIGHT_PROFILE_NAME "Daylight_DynamicPrivacyMask"
#define DPM_AI_ISP_GEN1_PROFILE_NAME "AI_ISP_Gen1_DynamicPrivacyMask"
#define DPM_HDR_PROFILE_NAME "High_Dynamic_Range_DynamicPrivacyMask"
#define DPM_AI_ISP_GEN2_PROFILE_NAME "AI_ISP_Gen2_DynamicPrivacyMask"
#define DPM_AI_ISP_GEN3_PROFILE_NAME "AI_ISP_Gen3_DynamicPrivacyMask"

using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline::overlay;
using namespace hailo_analytics::pipeline::routing;
using namespace hailo_analytics::pipeline::sources;
using namespace hailo_analytics::pipeline::codecs;

namespace webserver
{
namespace pipeline
{

struct AppResources
{
    MediaLibraryPtr media_library;
    std::shared_ptr<FrontendStage> frontend;
    std::shared_ptr<ValveStage> valve_stage;
    std::shared_ptr<FreezeStage> freeze_stage;
    std::shared_ptr<OverlayStage> overlay_stage;
    std::map<output_stream_id_t, std::shared_ptr<EncoderStage>> encoders;
    hailo_analytics::pipeline::PipelinePtr pipeline;
    Architecture platform;
    std::shared_ptr<IspBlender> m_isp_blender;
};

class BasePipeline
{
  public:
    BasePipeline(webserver::resources::ResourceRepository &resources, MediaLibraryPtr media_library,
                 RTPConverterStage &webrtc_stage, Architecture platform = Architecture::Hailo15H,
                 ProfileType default_profile = ProfileType::Daylight, std::vector<ProfileType> supported_profiles = {});
    virtual ~BasePipeline();

    virtual void init(ProfileType profile_type);
    virtual void uninitialize();
    virtual void start();
    virtual void stop();

    virtual ProfileType get_current_profile() const;

    virtual std::string get_profile_name_by_type(ProfileType type) const = 0;
    virtual ProfileType get_profile_type_by_name(const std::string &name) const = 0;
    std::vector<ProfileType> get_supported_profiles();

    webserver::resources::ResourceRepository *get_resources();

    ProfileType get_default_profile() const;

    virtual std::string pipeline_name() const = 0;

  protected:
    webserver::resources::ResourceRepository &m_resources;
    std::shared_ptr<AppResources> m_app_resources;
    bool m_rotate_done_in_dewarp;
    ProfileType m_current_profile_type;
    ProfileType m_default_profile_type;
    RTPConverterStage &m_webrtc_stage;
    std::vector<ProfileType> m_supported_profiles;
    std::string m_stream_4k_name = DEFAULT_STREAM_4K_NAME;
    ProfileType m_default_profile;
    bool m_hdr_valve_active = false;

    virtual void subscribe_callbacks();
    virtual void register_endpoints();
    virtual void unregister_endpoints();
    void register_framerate_endpoint();
    void register_flip_endpoint();
    void register_rotation_endpoint();
    void register_dewarp_endpoint();
    void register_grayscale_endpoint();
    void register_freeze_endpoint();
    void register_digital_zoom_endpoint();
    void register_resolution_endpoint();
    void register_denoise_endpoint();
    void register_current_profile_name_endpoint();
    void register_automatic_algorithms_endpoint();
    void register_reset_stream_endpoint();
    virtual void build_pipeline() = 0;
    std::shared_ptr<FrontendStage> configure_frontend();
    std::shared_ptr<EncoderStage> configure_encoder_and_osd(const std::string &stream_name);
    std::shared_ptr<UdpStage> configure_udp(const std::string &stream_name);
    std::string read_string_from_file(const char *file_path);
    virtual void callback_handle_profile_switch(ResourceStateChangeNotification notif);
    virtual void callback_handle_privacy_mask(ResourceStateChangeNotification notif);
    virtual void callback_handle_encoder(ResourceStateChangeNotification notif);
    virtual void callback_handle_osd(ResourceStateChangeNotification notif);
    virtual void callback_handle_update_profile(ResourceStateChangeNotification notif);
    void on_pipeline_profile_restricted(config_profile_t previous_profile, config_profile_t new_profile);
    void on_pipeline_profile_restriction_done();
    virtual std::shared_ptr<osd::Blender> get_osd_blender();
    virtual std::shared_ptr<PrivacyMaskBlender> get_privacy_blender();
    virtual hailo_encoder_config_t get_encoder_config();
    virtual void update_fps(uint32_t fps, config_profile_t &profile_config);
    virtual void update_resolution(const std::string &resolution, config_profile_t &profile_config);
    virtual void update_flip(const std::string &flip, config_profile_t &profile_config);
    virtual void update_rotation(const std::string &rotation, config_profile_t &profile_config);
    virtual void update_zoom(std::shared_ptr<ProfileDigitalZoomState> state, config_profile_t &profile_config);
    virtual void update_zoom_roi(std::shared_ptr<ProfileDigitalZoomRoiState> state, config_profile_t &profile_config);
    int relative_to_absolut(float position, uint32_t resolution_axis_size);
    float absolut_to_relative(int position, uint32_t resolution_axis_size);
    int scale(int position, int old_size, int new_size);

  private:
    void configure_profile_restriction_handlers();
};

} // namespace pipeline
} // namespace webserver

typedef std::shared_ptr<webserver::pipeline::BasePipeline> WebServerPipeline;
