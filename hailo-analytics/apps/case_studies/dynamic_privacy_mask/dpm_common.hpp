#pragma once

#include <algorithm>
#include <fstream>
#include "media_library/cloexec_fstream.hpp"
#include <iostream>
#include <sstream>
#include <tl/expected.hpp>
#include <cxxopts/cxxopts.hpp>

#include "media_library/signal_utils.hpp"
#include "media_library_factory.hpp"

// infra includes
#include "hailo_analytics/analytics/ai_models_config.hpp"
#include "hailo_analytics/analytics/dpm_analytics.hpp"
#include "hailo_analytics/analytics/vision.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/muxing/bundle_streams_stage.hpp"
#include "hailo_analytics/pipeline/muxing/split_streams_stage.hpp"
#include "hailo_analytics/pipeline/codecs/encoder_stage.hpp"
#include "hailo_analytics/pipeline/sinks/app_sink_stage.hpp"
#include "hailo_analytics/pipeline/sinks/udp_stage.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "hailo_analytics/utils/profile_utils.hpp"
#include "hailo_analytics/utils/stream_utils.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"

#define VISION_PIPELINE "vision_pipeline"
#define DPM_AI_PIPELINE "dpm_ai_pipeline"
#define APP_NAME "dynamic_privacy_mask_app"
#define HOST_IP hailo_analytics::analytics::vision::get_default_host_ip()
#define VISION_SINK "sink0"
#define FHD_SINK "dpm_sink1"
#define AI_SINK "dpm_sink2"
#define NO_PROFILE_SELECTED ""
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/case_studies/dynamic_privacy_mask_medialib_config.json"

constexpr std::string_view BUNDLE_STAGE = "bundle";
constexpr std::string_view SPLIT_STAGE = "split";
constexpr std::string_view AI_TERMINAL_SINK = "ai_terminal_sink";

static constexpr size_t UDP_SINK_QUEUE_SIZE = 5;

static constexpr size_t BUNDLE_QUEUE_SIZE = 6; // jitter buffer

namespace ai_models = hailo_analytics::analytics::ai_models;

using hailo_analytics::analytics::dpm_analytics::DEFAULT_MAX_DETECTIONS_15H;
using hailo_analytics::analytics::dpm_analytics::DEFAULT_MAX_DETECTIONS_15L;

#define SEGMENTED_LABELS_DEFAULT "person,vehicle"

inline bool is_hailo15l()
{
    cloexec::ifstream file("/sys/devices/soc0/machine");
    if (!file.is_open())
        return false;
    std::string line;
    std::getline(file, line);
    std::transform(line.begin(), line.end(), line.begin(), ::tolower);
    return line.find("hailo-15l") != std::string::npos;
}

inline int default_max_detections()
{
    return is_hailo15l() ? DEFAULT_MAX_DETECTIONS_15L : DEFAULT_MAX_DETECTIONS_15H;
}

struct AppResources
{
    MediaLibraryInterfacePtr media_library;
    hailo_analytics::pipeline::PipelinePtr pipeline;
    bool print_fps = true;
    bool use_medialib_service = false;
    std::string medialib_config_path;
    std::string profile_name;
    std::string host_ip = HOST_IP;
    std::string udp_port;
    std::string zmq_port;
    std::string hef_path = ai_models::resolve_hef(ai_models::LINKNET_DPM_128.hef_relative);
    std::vector<std::string> segment_labels;
    int max_detections_per_frame = default_max_detections();
};

inline std::vector<std::string> parse_segment_labels(const std::string &labels_str)
{
    std::vector<std::string> labels;
    if (labels_str.empty())
        return labels;

    std::stringstream ss(labels_str);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        if (!token.empty())
        {
            labels.push_back(token);
        }
    }
    return labels;
}

inline hailo_analytics::pipeline::AppStatus configure_media_library(std::shared_ptr<AppResources> app_resources)
{
    auto media_library =
        apps::utils::create_media_library(app_resources->use_medialib_service, app_resources->medialib_config_path);
    if (!media_library.has_value())
    {
        return hailo_analytics::pipeline::AppStatus::MEDIA_LIBRARY_ERROR;
    }
    app_resources->media_library = media_library.value();
    if (app_resources->profile_name != NO_PROFILE_SELECTED)
    {
        hailo_analytics::utils::set_initial_profile(app_resources->media_library, app_resources->profile_name);
    }
    return hailo_analytics::pipeline::AppStatus::SUCCESS;
}

struct stream_resolutions_t
{
    int vision_width = 0;
    int vision_height = 0;
    int ai_width = 0;
    int ai_height = 0;
};

inline tl::expected<stream_resolutions_t, hailo_analytics::pipeline::AppStatus> get_stream_resolutions(
    const std::vector<frontend_output_stream_t> &output_streams)
{
    stream_resolutions_t resolutions;
    for (const auto &stream : output_streams)
    {
        if (stream.id == VISION_SINK)
        {
            resolutions.vision_width = stream.width;
            resolutions.vision_height = stream.height;
        }
        else if (stream.id == AI_SINK)
        {
            resolutions.ai_width = stream.width;
            resolutions.ai_height = stream.height;
        }
    }

    if (resolutions.vision_width == 0 || resolutions.vision_height == 0)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get vision input resolution from frontend");
        return tl::make_unexpected(hailo_analytics::pipeline::AppStatus::MEDIA_LIBRARY_ERROR);
    }

    if (resolutions.ai_width == 0 || resolutions.ai_height == 0)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get AI pipeline resolution from frontend");
        return tl::make_unexpected(hailo_analytics::pipeline::AppStatus::MEDIA_LIBRARY_ERROR);
    }

    return resolutions;
}

inline tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
create_frontend_pipeline(std::shared_ptr<AppResources> app_resources)
{
    auto frontend =
        std::make_shared<hailo_analytics::pipeline::sources::FrontendStage>(VISION_PIPELINE, 1, false, false);
    if (frontend->configure(app_resources->media_library) != hailo_analytics::pipeline::AppStatus::SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to configure frontend stage");
        return tl::make_unexpected(hailo_analytics::pipeline::AppStatus::CONFIGURATION_ERROR);
    }
    auto pipeline = hailo_analytics::pipeline::PipelineBuilder()
                        .add_stage(frontend, hailo_analytics::pipeline::StageType::SOURCE)
                        .build(VISION_PIPELINE, true);
    pipeline->set_in_stage(frontend);
    pipeline->set_out_stage(frontend);
    return pipeline;
}

inline tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
create_dpm_ai_pipeline(std::shared_ptr<AppResources> app_resources, const stream_resolutions_t &resolutions)
{
    auto dpm_ai_config = hailo_analytics::analytics::dpm_analytics::build_dpm_config(
        resolutions.ai_width, resolutions.ai_height, app_resources->max_detections_per_frame,
        app_resources->segment_labels, app_resources->hef_path);

    dpm_ai_config.enable_detections_db_writer = false;

    // Both stages load a HEF. Leaving either outside hailort_server makes it contend with
    // media_library_service for the NN core instead of sharing it.
    const bool use_hailort_service = app_resources->use_medialib_service;
    dpm_ai_config.tiling_config.detection_config.ai_config.use_hailort_service = use_hailort_service;
    dpm_ai_config.dpm_config.segmentation_config.ai_config.use_hailort_service = use_hailort_service;

    if (!app_resources->zmq_port.empty() && dpm_ai_config.metadata_sender_config.has_value())
    {
        dpm_ai_config.metadata_sender_config->zeromq_config.pub_address = "tcp://*:" + app_resources->zmq_port;
    }

    auto dpm_ai_pipeline_status =
        hailo_analytics::analytics::dpm_analytics::generate_full_dpm_analytics_pipeline(DPM_AI_PIPELINE, dpm_ai_config);
    if (!dpm_ai_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create DPM analytics pipeline");
        return tl::make_unexpected(hailo_analytics::pipeline::AppStatus::PIPELINE_ERROR);
    }
    return dpm_ai_pipeline_status.value();
}

inline tl::expected<std::shared_ptr<hailo_analytics::pipeline::sinks::UdpStage>, hailo_analytics::pipeline::AppStatus>
create_udp_sink_for_stream(std::shared_ptr<AppResources> app_resources, const std::string &stream_id)
{
    int base_port = app_resources->udp_port.empty() ? 5000 : std::stoi(app_resources->udp_port);
    std::string port = hailo_analytics::utils::port_from_stream_id(stream_id, base_port);

    auto udp = hailo_analytics::pipeline::sinks::UdpStageBuild::create()
                   .set_stage_name(std::string("udp_") + stream_id)
                   .set_queue_size_opt(UDP_SINK_QUEUE_SIZE)
                   .set_leaky_opt(true)
                   .set_printfps_opt(app_resources->print_fps)
                   .set_trace_opt(true)
                   .buildptr();
    if (udp->configure(app_resources->host_ip, port, hailo_analytics::pipeline::sinks::EncodingType::H264) !=
        hailo_analytics::pipeline::AppStatus::SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to configure UDP sink for stream {}", stream_id);
        return tl::make_unexpected(hailo_analytics::pipeline::AppStatus::CONFIGURATION_ERROR);
    }
    return udp;
}

inline hailo_analytics::pipeline::AppStatus assemble_dpm_pipeline(
    std::shared_ptr<AppResources> app_resources, hailo_analytics::pipeline::PipelinePtr vision_pipeline)
{
    auto output_streams = app_resources->media_library->get_frontend_output_streams();
    if (!output_streams.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get stream ids");
        return hailo_analytics::pipeline::AppStatus::MEDIA_LIBRARY_ERROR;
    }
    auto resolutions = get_stream_resolutions(output_streams.value());
    if (!resolutions.has_value())
    {
        return resolutions.error();
    }
    auto dpm_ai_pipeline = create_dpm_ai_pipeline(app_resources, resolutions.value());
    if (!dpm_ai_pipeline.has_value())
    {
        return dpm_ai_pipeline.error();
    }

    auto bundle = hailo_analytics::pipeline::muxing::BundleStreamsStageBuild::create()
                      .set_stage_name(std::string(BUNDLE_STAGE))
                      .set_carrier_stream_id(std::string(AI_SINK))
                      .set_passenger_stream_ids({std::string(VISION_SINK), std::string(FHD_SINK)})
                      .set_queue_size_opt(BUNDLE_QUEUE_SIZE)
                      .buildptr();

    auto split = hailo_analytics::pipeline::muxing::SplitStreamsStageBuild::create()
                     .set_stage_name(std::string(SPLIT_STAGE))
                     .set_carrier_stream_id(std::string(AI_SINK))
                     .set_propagate_roi_opt(true)
                     .set_queue_size_opt(1)
                     .buildptr();

    auto vision_encoder = hailo_analytics::pipeline::codecs::EncoderStageBuild::create()
                              .set_stage_name("encoder_4k")
                              .set_attach_analytics_metadata(true)
                              .buildptr();
    if (vision_encoder->configure(app_resources->media_library, std::string(VISION_SINK)) !=
        hailo_analytics::pipeline::AppStatus::SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to configure encoder stage for stream {}", VISION_SINK);
        return hailo_analytics::pipeline::AppStatus::CONFIGURATION_ERROR;
    }

    auto vision_udp = create_udp_sink_for_stream(app_resources, std::string(VISION_SINK));
    if (!vision_udp.has_value())
    {
        return vision_udp.error();
    }

    auto fhd_encoder = hailo_analytics::pipeline::codecs::EncoderStageBuild::create()
                           .set_stage_name("encoder_fhd")
                           .set_attach_analytics_metadata(false)
                           .buildptr();
    if (fhd_encoder->configure(app_resources->media_library, std::string(FHD_SINK)) !=
        hailo_analytics::pipeline::AppStatus::SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to configure encoder stage for stream {}", FHD_SINK);
        return hailo_analytics::pipeline::AppStatus::CONFIGURATION_ERROR;
    }

    auto fhd_udp = create_udp_sink_for_stream(app_resources, std::string(FHD_SINK));
    if (!fhd_udp.has_value())
    {
        return fhd_udp.error();
    }

    auto ai_terminal_sink = hailo_analytics::pipeline::sinks::AppSinkStageBuild::create()
                                .set_stage_name(std::string(AI_TERMINAL_SINK))
                                .set_queue_size_opt(1)
                                .set_leaky_opt(true)
                                .set_process_func([](hailo_analytics::pipeline::BufferPtr) {})
                                .buildptr();

    // Frontend ─[4K, FHD, AI]→ bundle → DPM_AI → split ─[4K ]→ encoder_4k  → udp_4k
    //                                                  ─[FHD]→ encoder_fhd → udp_fhd
    //                                                  ─[AI ]→ ai_terminal_sink
    app_resources->pipeline =
        hailo_analytics::pipeline::PipelineBuilder()
            .add_stage(vision_pipeline, hailo_analytics::pipeline::StageType::SOURCE)
            .add_stage(std::string(BUNDLE_STAGE), bundle)
            .add_stage(dpm_ai_pipeline.value())
            .add_stage(std::string(SPLIT_STAGE), split)
            .add_stage("encoder_4k", vision_encoder)
            .add_stage("udp_4k", vision_udp.value(), hailo_analytics::pipeline::StageType::SINK)
            .add_stage("encoder_fhd", fhd_encoder)
            .add_stage("udp_fhd", fhd_udp.value(), hailo_analytics::pipeline::StageType::SINK)
            .add_stage(std::string(AI_TERMINAL_SINK), ai_terminal_sink, hailo_analytics::pipeline::StageType::SINK)
            .connect_frontend(VISION_PIPELINE, std::string(VISION_SINK), std::string(BUNDLE_STAGE))
            .connect_frontend(VISION_PIPELINE, std::string(FHD_SINK), std::string(BUNDLE_STAGE))
            .connect_frontend(VISION_PIPELINE, std::string(AI_SINK), std::string(BUNDLE_STAGE))
            .connect(std::string(BUNDLE_STAGE), DPM_AI_PIPELINE)
            .connect(DPM_AI_PIPELINE, std::string(SPLIT_STAGE))
            .connect(std::string(SPLIT_STAGE), std::string(VISION_SINK), "encoder_4k")
            .connect(std::string(SPLIT_STAGE), std::string(FHD_SINK), "encoder_fhd")
            .connect(std::string(SPLIT_STAGE), std::string(AI_SINK), std::string(AI_TERMINAL_SINK))
            .connect("encoder_4k", "udp_4k")
            .connect("encoder_fhd", "udp_fhd")
            .build(APP_NAME, true);

    return hailo_analytics::pipeline::AppStatus::SUCCESS;
}
