#pragma once
#include "pipeline/isp_blender.hpp"
#include "resources/common/resources.hpp"
#include "resources/common/repository.hpp"
#include "media_library/encoder_config.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/media_library_api_types.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/codecs/encoder_stage.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "hailo_analytics/pipeline/sinks/udp_stage.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/ai/postprocess_stage.hpp"
#include "hailo_analytics/pipeline/ai/ai_stage.hpp"
#include "hailo_analytics/pipeline/cropping/aggregator_stage.hpp"
#include "hailo_analytics/pipeline/overlay/overlay_stage.hpp"
#include "hailo_analytics/pipeline/routing/valve_stage.hpp"
#include "hailo_analytics/pipeline/routing/freeze_stage.hpp"
#include "hailo_analytics/pipeline/sinks/app_sink_stage.hpp"
#include "clip_pipeline_ai_defines.hpp"
#include "hailo_analytics/pipeline/sinks/rtp_converter_stage.hpp"
#include "clip_pipeline_ai.hpp"

#define DEFAULT_STREAM_4K_NAME "sink0"

#define BASIC_DAYLIGHT_PROFILE_NAME "Daylight_Basic"
#define BASIC_LOWLIGHT_PROFILE_NAME "Lowlight_Basic"
#define BASIC_HDR_PROFILE_NAME "High_Dynamic_Range_Basic"
#define BASIC_LOWLIGHT_BAYER_PROFILE_NAME "Lowlight_Bayer_Basic"
#define BASIC_DENOISE_HDR_PROFILE_NAME "Denoise_Hdr_Basic"

#define DETECTION_DAYLIGHT_PROFILE_NAME "Daylight_Detection"
#define DETECTION_LOWLIGHT_PROFILE_NAME "Lowlight_Detection"
#define DETECTION_HDR_PROFILE_NAME "High_Dynamic_Range_Detection"
#define DETECTION_LOWLIGHT_BAYER_PROFILE_NAME "Lowlight_Bayer_Detection"

#define FACE_LANDMARKS_DAYLIGHT_PROFILE_NAME "Daylight_FaceLandmarks"
#define FACE_LANDMARKS_LOWLIGHT_PROFILE_NAME "Lowlight_FaceLandmarks"
#define FACE_LANDMARKS_HDR_PROFILE_NAME "High_Dynamic_Range_FaceLandmarks"
#define FACE_LANDMARKS_LOWLIGHT_BAYER_PROFILE_NAME "Lowlight_Bayer_FaceLandmarks"

#define CLIP_PROFILE_NAME "Daylight_Clip"

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
    std::shared_ptr<MediaLibrary> media_library;
    std::shared_ptr<FrontendStage> frontend;
    std::shared_ptr<ValveStage> valve_stage;
    std::shared_ptr<FreezeStage> freeze_stage;
    std::shared_ptr<OverlayStage> overlay_stage;
    std::map<output_stream_id_t, std::shared_ptr<EncoderStage>> encoders;
    PipelinePtr pipeline;
    Architecture platform;
    std::shared_ptr<IspBlender> m_isp_blender;
};

class BasePipeline
{
  public:
    BasePipeline(WebserverResourceRepository resources, std::shared_ptr<MediaLibrary> media_library,
                 std::shared_ptr<RTPConverterStage> webrtc_stage, Architecture platform = Architecture::Hailo15H,
                 ProfileType default_profile = ProfileType::Daylight, std::vector<ProfileType> supported_profiles = {});
    virtual ~BasePipeline();

    virtual void init(ProfileType profile_type);
    virtual void start();
    virtual void stop();

    virtual ProfileType get_current_profile() const
    {
        return m_current_profile_type;
    }

    virtual std::string get_profile_name_by_type(ProfileType type) const = 0;
    virtual ProfileType get_profile_type_by_name(const std::string &name) const = 0;
    std::vector<ProfileType> get_supported_profiles()
    {
        return m_supported_profiles;
    }

    WebserverResourceRepository get_resources()
    {
        return m_resources;
    }

    ProfileType get_default_profile() const
    {
        return m_default_profile;
    }

    virtual std::string pipeline_name() const = 0;

  protected:
    WebserverResourceRepository m_resources;
    std::shared_ptr<AppResources> m_app_resources;
    bool m_rotate_done_in_dewarp;
    ProfileType m_current_profile_type;
    ProfileType m_default_profile_type;
    std::shared_ptr<RTPConverterStage> m_webrtc_stage;
    std::vector<ProfileType> m_supported_profiles;
    std::string m_stream_4k_name = DEFAULT_STREAM_4K_NAME;
    ProfileType m_default_profile;

    virtual void subscribe_callbacks();
    virtual void register_endpoints();
    virtual void unregister_endpoints();
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

// Basic pipeline implementation (simple streaming without AI)
class BasicPipeline : public BasePipeline
{
  public:
    BasicPipeline(WebserverResourceRepository resources, std::shared_ptr<MediaLibrary> media_library,
                  std::shared_ptr<RTPConverterStage> webrtc_stage, Architecture platform = Architecture::Hailo15H);

    virtual std::string pipeline_name() const override;
    void start() override;
    std::string get_profile_name_by_type(ProfileType type) const override;
    ProfileType get_profile_type_by_name(const std::string &name) const override;

  protected:
    void build_pipeline() override;
};

// Detection pipeline implementation (with AI detection capabilities)
class DetectionPipeline : public BasePipeline
{
  public:
    DetectionPipeline(WebserverResourceRepository resources, std::shared_ptr<MediaLibrary> media_library,
                      std::shared_ptr<RTPConverterStage> webrtc_stage, Architecture platform = Architecture::Hailo15H);

    virtual std::string pipeline_name() const override;
    void start() override;
    std::string get_profile_name_by_type(ProfileType type) const override;
    ProfileType get_profile_type_by_name(const std::string &name) const override;

  protected:
    void build_pipeline() override;
    void update_rotation(const std::string &rotation, config_profile_t &profile_config) override;
    void register_endpoints() override;
    void unregister_endpoints() override;

  private:
    bool should_draw_overlay();
};

// CLIP pipeline implementation (textual search capabilities)
class ClipPipeline : public BasePipeline
{
  public:
    ClipPipeline(WebserverResourceRepository resources, std::shared_ptr<MediaLibrary> media_library,
                 std::shared_ptr<RTPConverterStage> webrtc_stage, Architecture platform = Architecture::Hailo15H);
    static bool is_supported(WebserverResourceRepository resources);
    static const ClipAppConfig &get_clip_config();
    virtual std::string pipeline_name() const override;
    void start() override;
    void stop() override;
    void init(ProfileType profile_type) override;
    void register_endpoints() override;
    void unregister_endpoints() override;
    std::string get_profile_name_by_type(ProfileType type) const override;
    ProfileType get_profile_type_by_name(const std::string &name) const override;

  protected:
    void build_pipeline() override;

  private:
    std::shared_ptr<ClipAppConfig> m_clip_app_config;
    std::shared_ptr<ClipVideoPipeline> m_app;
    std::vector<app::ImageData> m_images; // Thumbnail gallery functionality

    bool processEmbedding(const app::EmbeddingInfo &embedding_info);
    tl::expected<void, app::ImageError> addImage(const std::string &jpeg_path, const std::string &description,
                                                 int64_t timestamp, float score);
    void clearAllImages();
};

class ProfileManagerPipeline : public BasePipeline
{
  public:
    ProfileManagerPipeline(WebserverResourceRepository resources, std::shared_ptr<MediaLibrary> media_library,
                           std::shared_ptr<RTPConverterStage> webrtc_stage,
                           Architecture platform = Architecture::Hailo15H);

    virtual std::string pipeline_name() const override;
    ~ProfileManagerPipeline() override;
    void start() override;
    void stop() override;
    void stop(bool full_shutdown = true);
    std::string get_profile_name_by_type(ProfileType type) const override;
    ProfileType get_profile_type_by_name(const std::string &name) const override;
    void register_endpoints() override;
    void unregister_endpoints() override;

  protected:
    void build_pipeline() override;
};

// Face Landmarks pipeline implementation (multi-stage person/face detection and landmarks)
class FaceLandmarksPipeline : public BasePipeline
{
  public:
    FaceLandmarksPipeline(WebserverResourceRepository resources, std::shared_ptr<MediaLibrary> media_library,
                          std::shared_ptr<RTPConverterStage> webrtc_stage,
                          Architecture platform = Architecture::Hailo15H);
    static bool is_supported(WebserverResourceRepository resources);
    virtual std::string pipeline_name() const override;
    void start() override;
    std::string get_profile_name_by_type(ProfileType type) const override;
    ProfileType get_profile_type_by_name(const std::string &name) const override;

  protected:
    void build_pipeline() override;
    void register_endpoints() override;
    void unregister_endpoints() override;
};

} // namespace pipeline
} // namespace webserver

typedef std::shared_ptr<webserver::pipeline::BasePipeline> WebServerPipeline;
