#pragma once

// general includes
#include <iostream>
#include <memory>
#include <filesystem>

// medialibrary includes
#include "media_library/frontend.hpp"
#include "media_library/signal_utils.hpp"

// infra includes
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/analytics/reference_camera_app_constructor.hpp"
#include "hailo_analytics/pipeline/sinks/udp_stage.hpp"
#include "hailo_analytics/pipeline/cropping/aggregator_stage.hpp"
#include "hailo_analytics/pipeline/cropping/sync_aggregator_stage.hpp"
#include "hailo_analytics/pipeline/overlay/overlay_stage.hpp"
#include "hailo_analytics/pipeline/cropping/bbox_crop_stage.hpp"
#include "hailo_analytics/pipeline/cropping/tiling_stage.hpp"
#include "hailo_analytics/pipeline/ai/ai_stage.hpp"
#include "hailo_analytics/pipeline/ai/postprocess_stage.hpp"
#include "hailo_analytics/pipeline/ai/lightweight_tracker_stage.hpp"
#include "hailo_analytics/pipeline/routing/tracker_traffic_ctrl_stage.hpp"
#include "hailo_analytics/pipeline/routing/tee_stage.hpp"
#include "hailo_analytics/pipeline/sinks/rtp_converter_stage.hpp"

// custom infra include
#include "pipeline/thumb_storage_stage.hpp"
#include "pipeline/faiss_storage_stage.hpp"
#include "pipeline/cache_stage.hpp"
#include "pipeline/clip_image_preprocess.hpp"
#include "pipeline/video_storage_stage.hpp"
#include "pipeline/full_frame_bbox_injector_stage.hpp"
#include "database_manager.hpp"

// Extensions includes
#include "service/query_service/query_service_ext.hpp"
#include "service/query_service/clip_text_encoder.hpp"
#include "streaming/webrtc_streamer_ext.hpp"
#include "service/player_service_ext.hpp"
#include "service/storage_monitor_service_ext.hpp"
#include "service/storage_cleanup_service_ext.hpp"
#include "service/storage_cleanup_strategy.hpp"
#include "service/app_control_service_ext.hpp"

// others
#include "clip_app_config_parser.hpp"
#include "common_utils.hpp"
#include <iostream>
#include <memory>
#include <filesystem>

// App defines
#include "clip_pipeline_ai_defines.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

// Using declarations for hailo_analytics types
using hailo_analytics::analytics::app_constructor::CamAppReturnCode;
using hailo_analytics::analytics::app_constructor::MediaStageComponents;
using hailo_analytics::analytics::app_constructor::PipelinePtr;
using hailo_analytics::pipeline::PipelineBuilder;
using hailo_analytics::pipeline::StagePoolMode;
using hailo_analytics::pipeline::StageType;
using hailo_analytics::pipeline::ai::HailortAsyncStage;
using hailo_analytics::pipeline::ai::HailortAsyncStageBuild;
using hailo_analytics::pipeline::ai::LightweightTrackerStage;
using hailo_analytics::pipeline::ai::LightweightTrackerStageBuild;
using hailo_analytics::pipeline::ai::PostprocessStage;
using hailo_analytics::pipeline::ai::PostprocessStageBuild;
using hailo_analytics::pipeline::cropping::AggregatorStage;
using hailo_analytics::pipeline::cropping::AggregatorStageBuild;
using hailo_analytics::pipeline::cropping::BBoxCropStage;
using hailo_analytics::pipeline::cropping::BBoxCropStageBuild;
using hailo_analytics::pipeline::cropping::SyncAggregatorStage;
using hailo_analytics::pipeline::cropping::SyncAggregatorStageBuild;
using hailo_analytics::pipeline::cropping::TilingCropStage;
using hailo_analytics::pipeline::cropping::TilingCropStageBuild;
using hailo_analytics::pipeline::overlay::OverlayStage;
using hailo_analytics::pipeline::overlay::OverlayStageBuild;
using hailo_analytics::pipeline::routing::TeeStage;
using hailo_analytics::pipeline::routing::TeeStageBuild;
using hailo_analytics::pipeline::routing::TrackerTrafficCtrlStage;
using hailo_analytics::pipeline::routing::TrackerTrafficCtrlStageBuild;
using hailo_analytics::pipeline::sinks::EncodingType;
using hailo_analytics::pipeline::sinks::RTPConverterStage;
using hailo_analytics::pipeline::sinks::RTPConverterStageBuild;

// If enabled make sure the network bandwidth can support it since it will be streaming via both UDP and WebRTC
// (to web browser), web browser streaming may lag.
#define ENABLE_4K_UDP_OUTPUT 0

struct ClipAppCustomData : public hailo_analytics::analytics::app_constructor::UserDataBase
{

    ClipAppConfig::ImageEncoders m_clip_image_encoders;
    ClipAppConfig::PipelineConfig m_pipeline_config;
    ClipAppConfig::HailortDeviceConfig m_hailort_device_config;
    ClipAppConfig::StorageConfiguration m_storage_config;
    std::vector<ClipAppConfig::TextEncoder> m_clip_text_encoder_support_list;
    ClipAppConfig::FaissConfig m_faiss_test_config;
    std::shared_ptr<hailo_analytics::pipeline::Stage> m_main_sink_output_stage;
    std::shared_ptr<hailo_analytics::pipeline::sinks::RTPConverterStage::RTPReceiver> m_query_playback_rtp_receiver;
    bool m_skip_detections_overlays_drawing;

    ClipAppCustomData(const ClipAppConfig::ImageEncoders &encoders,
                      const ClipAppConfig::PipelineConfig &pipeline_config,
                      const ClipAppConfig::HailortDeviceConfig &hailort_device_config,
                      const ClipAppConfig::StorageConfiguration &storage_config,
                      const std::vector<ClipAppConfig::TextEncoder> &clip_text_encoder_support_list,
                      const ClipAppConfig::FaissConfig &faiss_test_config,
                      std::shared_ptr<hailo_analytics::pipeline::Stage> main_sink_output_stage = nullptr,
                      std::shared_ptr<hailo_analytics::pipeline::sinks::RTPConverterStage::RTPReceiver>
                          query_playback_rtp_receiver = nullptr,
                      bool skip_detections_overlays_drawing = false);

    const char *type_name() const override;
};

class ClipVideoPipeline : public hailo_analytics::analytics::app_constructor::CameraAppConstructor
{
  private:
    std::shared_ptr<ClipAppCustomData> m_app_custom_data;

  public:
    std::map<output_stream_id_t, std::shared_ptr<hailo_analytics::pipeline::sinks::UdpStage>> m_udp_outputs;

    ~ClipVideoPipeline();

    static bool is_supported(const ClipAppConfig::StorageConfiguration &storage_config);

  protected:
    hailo_analytics::analytics::app_constructor::CamAppReturnCode register_app_extensions(
        std::shared_ptr<hailo_analytics::analytics::app_constructor::UserDataBase> user_data);

    std::string default_media_config() const override;

    std::string main_stream_encoder_id(const MediaStageComponents &components) const override;

    std::string main_stream_frontend_output_id(const MediaStageComponents &components) const override;

    tl::expected<PipelinePtr, CamAppReturnCode> build_pipeline(const MediaStageComponents &components) override;

    std::string get_udp_stage_name_contain(std::string &contains);

    void show_component_info(const MediaStageComponents &components);

    void faiss_index_misc_config(const std::shared_ptr<ClipAppCustomData> &app_custom_data);
};
