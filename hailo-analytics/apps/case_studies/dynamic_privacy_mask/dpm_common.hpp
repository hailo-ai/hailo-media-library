#pragma once

// general includes
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <tl/expected.hpp>
#include <cxxopts/cxxopts.hpp>

// medialibrary includes
#include "media_library/signal_utils.hpp"

// infra includes
#include "hailo_analytics/analytics/dpm_analytics.hpp"
#include "hailo_analytics/analytics/vision.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

// defines
#define VISION_PIPELINE "vision_pipeline"
#define DPM_AI_PIPELINE "dpm_ai_pipeline"
#define APP_NAME "dynamic_privacy_mask_app"
#define HOST_IP "10.0.0.2"
#define VISION_SINK "sink0"
#define AI_SINK "dpm_sink2"
#define NO_PROFILE_SELECTED ""
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/case_studies/dynamic_privacy_mask_medialib_config.json"

// Import shared DPM constants
using hailo_analytics::analytics::dpm_analytics::DEFAULT_DPM_SEG_HEF;
using hailo_analytics::analytics::dpm_analytics::DEFAULT_MAX_DETECTIONS_15H;
using hailo_analytics::analytics::dpm_analytics::DEFAULT_MAX_DETECTIONS_15L;

#define SEGMENTED_LABELS_DEFAULT "person,vehicle"

inline bool is_hailo15l()
{
    std::ifstream file("/sys/devices/soc0/machine");
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
    std::shared_ptr<MediaLibrary> media_library;
    hailo_analytics::pipeline::PipelinePtr pipeline;
    bool print_fps = false;
    std::string medialib_config_path;
    std::string profile_name;
    std::string host_ip = HOST_IP;
    std::string udp_port;
    std::string zmq_port;
    std::string hef_path = std::string(DEFAULT_DPM_SEG_HEF);
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

inline std::string read_string_from_file(const char *file_path)
{
    std::ifstream file_to_read;
    file_to_read.open(file_path);
    if (!file_to_read.is_open())
        throw std::runtime_error(std::string("config path (") + file_path + ") is not valid");
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)), std::istreambuf_iterator<char>());
    file_to_read.close();
    std::cout << "Read config from file: " << file_path << std::endl;
    return file_string;
}

inline void configure_media_library(std::shared_ptr<AppResources> app_resources,
                                    const std::string &medialib_config_string)
{
    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library" << std::endl;
        throw std::runtime_error("Failed to create media library");
    }
    app_resources->media_library = media_lib_expected.value();
    if (app_resources->media_library->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        throw std::runtime_error("Failed to initialize media library");
    }
    if (app_resources->profile_name != NO_PROFILE_SELECTED)
    {
        app_resources->media_library->set_profile(app_resources->profile_name);
    }
}

struct stream_resolutions_t
{
    int vision_width = 0;
    int vision_height = 0;
    int ai_width = 0;
    int ai_height = 0;
};

inline stream_resolutions_t get_stream_resolutions(const std::vector<frontend_output_stream_t> &output_streams)
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
        throw std::runtime_error("Failed to get vision input resolution from frontend");
    }

    if (resolutions.ai_width == 0 || resolutions.ai_height == 0)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get AI pipeline resolution from frontend");
        throw std::runtime_error("Failed to get AI pipeline resolution from frontend");
    }

    return resolutions;
}

inline hailo_analytics::pipeline::PipelinePtr create_vision_pipeline(
    std::shared_ptr<AppResources> app_resources, const std::vector<frontend_output_stream_t> &output_streams)
{
    int base_port = app_resources->udp_port.empty() ? 5000 : std::stoi(app_resources->udp_port);
    auto vision_config = hailo_analytics::analytics::vision::base_vision_config(output_streams, base_port);
    // Erase the AI sink to prevent vision pipeline from automatically generating
    // and connecting frontend outputs to encoders for it
    vision_config.outputs.erase(AI_SINK);
    vision_config.outputs[VISION_SINK].udp_config.host = app_resources->host_ip;

    auto vision_pipeline_status = hailo_analytics::analytics::vision::generate_vision_pipeline(
        app_resources->media_library, VISION_PIPELINE, vision_config);
    if (!vision_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create vision pipeline");
        throw std::runtime_error("Failed to create vision pipeline");
    }
    return vision_pipeline_status.value();
}

inline hailo_analytics::pipeline::PipelinePtr create_dpm_ai_pipeline(std::shared_ptr<AppResources> app_resources,
                                                                     const stream_resolutions_t &resolutions)
{
    auto dpm_ai_config = hailo_analytics::analytics::dpm_analytics::build_dpm_config(
        resolutions.ai_width, resolutions.ai_height, app_resources->max_detections_per_frame,
        app_resources->segment_labels, app_resources->hef_path);

    if (!app_resources->zmq_port.empty() && dpm_ai_config.metadata_sender_config.has_value())
    {
        dpm_ai_config.metadata_sender_config->zeromq_config.pub_address = "tcp://*:" + app_resources->zmq_port;
    }

    auto dpm_ai_pipeline_status =
        hailo_analytics::analytics::dpm_analytics::generate_full_dpm_analytics_pipeline(DPM_AI_PIPELINE, dpm_ai_config);
    if (!dpm_ai_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create DPM analytics pipeline");
        throw std::runtime_error("Failed to create DPM analytics pipeline");
    }
    return dpm_ai_pipeline_status.value();
}

inline void assemble_dpm_pipeline(std::shared_ptr<AppResources> app_resources,
                                  hailo_analytics::pipeline::PipelinePtr vision_pipeline)
{
    auto output_streams = app_resources->media_library->get_frontend_output_streams();
    if (!output_streams.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get stream ids");
        throw std::runtime_error("Failed to get stream ids");
    }
    auto resolutions = get_stream_resolutions(output_streams.value());
    auto dpm_ai_pipeline = create_dpm_ai_pipeline(app_resources, resolutions);

    hailo_analytics::pipeline::PipelineBuilder pip_builder;
    pip_builder.add_stage(vision_pipeline, hailo_analytics::pipeline::StageType::SOURCE);
    pip_builder.add_stage(dpm_ai_pipeline);

    // Connect frontend AI sink to the DPM analytics pipeline
    pip_builder.connect_frontend(VISION_PIPELINE, AI_SINK, DPM_AI_PIPELINE);

    app_resources->pipeline = pip_builder.build(APP_NAME, true);
}
