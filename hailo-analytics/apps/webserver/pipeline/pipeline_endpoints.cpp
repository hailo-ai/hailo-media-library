#include "pipeline.hpp"

using namespace webserver::pipeline;
using namespace webserver::resources;

const std::string PIPELINE_ENDPOINT_FRAMERATE = "/framerate";
const std::string PIPELINE_ENDPOINT_FLIP = "/flip";
const std::string PIPELINE_ENDPOINT_ROTATION = "/rotation";
const std::string PIPELINE_ENDPOINT_DEWARP = "/dewarp";
const std::string PIPELINE_ENDPOINT_GRAYSCALE = "/grayscale";
const std::string PIPELINE_ENDPOINT_FREEZE = "/freeze";
const std::string PIPELINE_ENDPOINT_DIGITAL_ZOOM = "/digital_zoom";
const std::string PIPELINE_ENDPOINT_RESOLUTION = "/resolution";
const std::string PIPELINE_ENDPOINT_DENOISE = "/denoise";
const std::string PIPELINE_ENDPOINT_CURRENT_PROFILE_NAME = "/current_profile_name";
const std::string PIPELINE_ENDPOINT_AUTOMATIC_ALGORITHMS = "/automatic_algorithms";
const std::string PIPELINE_ENDPOINT_RESET_STREAM = "/reset_stream";

const std::vector<std::string> PIPELINE_ALL_ENDPOINTS = {PIPELINE_ENDPOINT_FRAMERATE,
                                                         PIPELINE_ENDPOINT_FLIP,
                                                         PIPELINE_ENDPOINT_ROTATION,
                                                         PIPELINE_ENDPOINT_DEWARP,
                                                         PIPELINE_ENDPOINT_GRAYSCALE,
                                                         PIPELINE_ENDPOINT_FREEZE,
                                                         PIPELINE_ENDPOINT_DIGITAL_ZOOM,
                                                         PIPELINE_ENDPOINT_RESOLUTION,
                                                         PIPELINE_ENDPOINT_DENOISE,
                                                         PIPELINE_ENDPOINT_CURRENT_PROFILE_NAME,
                                                         PIPELINE_ENDPOINT_AUTOMATIC_ALGORITHMS,
                                                         PIPELINE_ENDPOINT_RESET_STREAM};

void BasePipeline::register_framerate_endpoint()
{
    m_resources.m_srv.Get(
        PIPELINE_ENDPOINT_FRAMERATE, std::function<nlohmann::json()>([this]() {
            WEBSERVER_LOG_INFO("GET {} called", PIPELINE_ENDPOINT_FRAMERATE);
            auto expected_profile = m_app_resources->media_library.get_current_profile();
            if (!expected_profile.has_value())
            {
                WEBSERVER_LOG_ERROR("Failed to get current profile");
                throw std::runtime_error("Failed to get current profile");
            }
            config_profile_t current_profile = expected_profile.value();
            uint32_t fps = current_profile.application_settings.application_input_streams.resolutions[0].framerate;
            nlohmann::json j;
            j["framerate"] = fps;
            WEBSERVER_LOG_INFO("GET {} completed", PIPELINE_ENDPOINT_FRAMERATE);
            return j;
        }));

    m_resources.m_srv.Put(PIPELINE_ENDPOINT_FRAMERATE, [this](const nlohmann::json &j_body) {
        //{ "framerate": 30 }
        WEBSERVER_LOG_INFO("PUT /framerate called");
        if (!j_body.contains("framerate"))
        {
            WEBSERVER_LOG_ERROR("Framerate not found in request body");
            throw std::runtime_error("Framerate not found in request body");
        }
        auto framerate = j_body["framerate"].get<int>();
        m_resources.m_event_bus->notify(EventType::CHANGE_FRAMERATE,
                                        std::make_shared<ProfileFPSState>(ProfileFPSState(framerate)));
        WEBSERVER_LOG_INFO("PUT /framerate completed");
        return nlohmann::json();
    });
}

void BasePipeline::register_flip_endpoint()
{
    m_resources.m_srv.Get(PIPELINE_ENDPOINT_FLIP, std::function<nlohmann::json()>([this]() {
                              WEBSERVER_LOG_INFO("GET {} called", PIPELINE_ENDPOINT_FLIP);
                              auto expected_profile = m_app_resources->media_library.get_current_profile();
                              if (!expected_profile.has_value())
                              {
                                  WEBSERVER_LOG_ERROR("Failed to get current profile");
                                  throw std::runtime_error("Failed to get current profile");
                              }
                              config_profile_t current_profile = expected_profile.value();
                              std::string flip =
                                  flip_direction_to_string(current_profile.application_settings.flip.enabled
                                                               ? current_profile.application_settings.flip.direction
                                                               : flip_direction_t::FLIP_DIRECTION_NONE);
                              nlohmann::json j;
                              j["flip"] = flip;
                              WEBSERVER_LOG_INFO("GET {} completed", PIPELINE_ENDPOINT_FLIP);
                              return j;
                          }));

    m_resources.m_srv.Put(PIPELINE_ENDPOINT_FLIP, [this](const nlohmann::json &j_body) {
        //{ "flip": "FLIP_DIRECTION_NONE/FLIP_DIRECTION_HORIZONTAL/FLIP_DIRECTION_VERTICAL/FLIP_DIRECTION_BOTH" }
        WEBSERVER_LOG_INFO("PUT /flip called");
        if (!j_body.contains("flip"))
        {
            WEBSERVER_LOG_ERROR("Flip not found in request body");
            throw std::runtime_error("Flip not found in request body");
        }
        auto flip = j_body["flip"].get<std::string>();
        m_resources.m_event_bus->notify(EventType::CHANGE_FLIP,
                                        std::make_shared<ProfileFlipState>(ProfileFlipState(flip)));
        WEBSERVER_LOG_INFO("PUT /flip completed");
        return nlohmann::json();
    });
}

void BasePipeline::register_rotation_endpoint()
{
    m_resources.m_srv.Get(PIPELINE_ENDPOINT_ROTATION, std::function<nlohmann::json()>([this]() {
                              WEBSERVER_LOG_INFO("GET {} called", PIPELINE_ENDPOINT_ROTATION);
                              auto expected_profile = m_app_resources->media_library.get_current_profile();
                              if (!expected_profile.has_value())
                              {
                                  WEBSERVER_LOG_ERROR("Failed to get current profile");
                                  throw std::runtime_error("Failed to get current profile");
                              }
                              config_profile_t current_profile = expected_profile.value();
                              rotation_angle_t angle = current_profile.application_settings.rotation.enabled
                                                           ? current_profile.application_settings.rotation.angle
                                                           : rotation_angle_t::ROTATION_ANGLE_0;
                              std::string rotation = rotation_angle_to_string(angle);
                              nlohmann::json j;
                              j["rotation"] = rotation;
                              WEBSERVER_LOG_INFO("GET {} completed", PIPELINE_ENDPOINT_ROTATION);
                              return j;
                          }));

    m_resources.m_srv.Put(PIPELINE_ENDPOINT_ROTATION, [this](const nlohmann::json &j_body) {
        //{ "rotation": "ROTATION_ANGLE_0/ROTATION_ANGLE_90/ROTATION_ANGLE_180/ROTATION_ANGLE_270" }
        WEBSERVER_LOG_INFO("PUT /rotation called");
        if (!j_body.contains("rotation"))
        {
            WEBSERVER_LOG_ERROR("Rotation not found in request body");
            throw std::runtime_error("Rotation not found in request body");
        }
        auto rotation = j_body["rotation"].get<std::string>();
        m_resources.m_event_bus->notify(EventType::CHANGE_ROTATION,
                                        std::make_shared<ProfileRotationState>(ProfileRotationState(rotation)));
        WEBSERVER_LOG_INFO("PUT /rotation completed");
        return nlohmann::json();
    });
}

void BasePipeline::register_dewarp_endpoint()
{
    m_resources.m_srv.Get(PIPELINE_ENDPOINT_DEWARP, std::function<nlohmann::json()>([this]() {
                              WEBSERVER_LOG_INFO("GET {} called", PIPELINE_ENDPOINT_DEWARP);
                              auto expected_profile = m_app_resources->media_library.get_current_profile();
                              if (!expected_profile.has_value())
                              {
                                  WEBSERVER_LOG_ERROR("Failed to get current profile");
                                  throw std::runtime_error("Failed to get current profile");
                              }
                              config_profile_t current_profile = expected_profile.value();
                              bool dewarp = current_profile.iq_settings.dewarp.enabled;
                              nlohmann::json j;
                              j["dewarp"] = dewarp;
                              WEBSERVER_LOG_INFO("GET {} completed", PIPELINE_ENDPOINT_DEWARP);
                              return j;
                          }));

    m_resources.m_srv.Put(PIPELINE_ENDPOINT_DEWARP, [this](const nlohmann::json &j_body) {
        //{ "dewarp": "true/false" }
        WEBSERVER_LOG_INFO("PUT /dewarp called");
        if (!j_body.contains("dewarp"))
        {
            WEBSERVER_LOG_ERROR("Dewarp not found in request body");
            throw std::runtime_error("Dewarp not found in request body");
        }
        auto dewarp = j_body["dewarp"].get<bool>();
        m_resources.m_event_bus->notify(EventType::CHANGE_DEWARP,
                                        std::make_shared<ProfileDewarpState>(ProfileDewarpState(dewarp)));
        WEBSERVER_LOG_INFO("PUT /dewarp completed");
        return nlohmann::json();
    });
}

void BasePipeline::register_grayscale_endpoint()
{
    m_resources.m_srv.Get(PIPELINE_ENDPOINT_GRAYSCALE, std::function<nlohmann::json()>([this]() {
                              WEBSERVER_LOG_INFO("GET {} called", PIPELINE_ENDPOINT_GRAYSCALE);
                              auto expected_profile = m_app_resources->media_library.get_current_profile();
                              if (!expected_profile.has_value())
                              {
                                  WEBSERVER_LOG_ERROR("Failed to get current profile");
                                  throw std::runtime_error("Failed to get current profile");
                              }
                              config_profile_t current_profile = expected_profile.value();
                              bool grayscale = current_profile.iq_settings.grayscale.enabled;
                              nlohmann::json j;
                              j["grayscale"] = grayscale;
                              WEBSERVER_LOG_INFO("GET {} completed", PIPELINE_ENDPOINT_GRAYSCALE);
                              return j;
                          }));

    m_resources.m_srv.Put(PIPELINE_ENDPOINT_GRAYSCALE, [this](const nlohmann::json &j_body) {
        //{ "grayscale": "true/false" }
        WEBSERVER_LOG_INFO("PUT /grayscale called");
        if (!j_body.contains("grayscale"))
        {
            WEBSERVER_LOG_ERROR("Grayscale not found in request body");
            throw std::runtime_error("Grayscale not found in request body");
        }
        auto grayscale = j_body["grayscale"].get<bool>();
        m_resources.m_event_bus->notify(EventType::CHANGE_GRAYSCALE,
                                        std::make_shared<ProfileGrayscaleState>(ProfileGrayscaleState(grayscale)));
        WEBSERVER_LOG_INFO("PUT /grayscale completed");
        return nlohmann::json();
    });
}

void BasePipeline::register_freeze_endpoint()
{
    m_resources.m_srv.Get(PIPELINE_ENDPOINT_FREEZE, std::function<nlohmann::json()>([this]() {
                              WEBSERVER_LOG_INFO("GET {} called", PIPELINE_ENDPOINT_FREEZE);
                              bool freeze = m_app_resources->freeze_stage->is_freeze();
                              nlohmann::json j;
                              j["freeze"] = freeze;
                              WEBSERVER_LOG_INFO("GET {} completed", PIPELINE_ENDPOINT_FREEZE);
                              return j;
                          }));

    m_resources.m_srv.Put(PIPELINE_ENDPOINT_FREEZE, [this](const nlohmann::json &j_body) {
        //{ "freeze": "true/false" }
        WEBSERVER_LOG_INFO("PUT /freeze called");
        if (!j_body.contains("freeze"))
        {
            WEBSERVER_LOG_ERROR("Freeze not found in request body");
            throw std::runtime_error("Freeze not found in request body");
        }
        auto freeze = j_body["freeze"].get<bool>();
        m_resources.m_event_bus->notify(EventType::CHANGE_FREEZE,
                                        std::make_shared<ProfileFreezeState>(ProfileFreezeState(freeze)));
        WEBSERVER_LOG_INFO("PUT /freeze completed");
        return nlohmann::json();
    });
}

void BasePipeline::register_digital_zoom_endpoint()
{
    m_resources.m_srv.Get(
        PIPELINE_ENDPOINT_DIGITAL_ZOOM, std::function<nlohmann::json()>([this]() {
            WEBSERVER_LOG_INFO("GET {} called", PIPELINE_ENDPOINT_DIGITAL_ZOOM);
            auto expected_profile = m_app_resources->media_library.get_current_profile();
            if (!expected_profile.has_value())
            {
                WEBSERVER_LOG_ERROR("Failed to get current profile");
                throw std::runtime_error("Failed to get current profile");
            }
            config_profile_t current_profile = expected_profile.value();
            uint32_t width = current_profile.application_settings.application_input_streams.resolutions[0]
                                 .dimensions.destination_width;
            uint32_t height = current_profile.application_settings.application_input_streams.resolutions[0]
                                  .dimensions.destination_height;
            nlohmann::json j;
            j["digital_zoom"]["enabled"] = current_profile.application_settings.digital_zoom.enabled;
            j["digital_zoom"]["mode"] =
                digital_zoom_mode_string_map.at(current_profile.application_settings.digital_zoom.mode);
            j["digital_zoom"]["magnification"] = current_profile.application_settings.digital_zoom.magnification;
            j["digital_zoom"]["digital_zoom_roi"]["x"] =
                absolut_to_relative(current_profile.application_settings.digital_zoom.roi.x, width);
            j["digital_zoom"]["digital_zoom_roi"]["y"] =
                absolut_to_relative(current_profile.application_settings.digital_zoom.roi.y, height);
            j["digital_zoom"]["digital_zoom_roi"]["width"] =
                absolut_to_relative(current_profile.application_settings.digital_zoom.roi.width, width);
            j["digital_zoom"]["digital_zoom_roi"]["height"] =
                absolut_to_relative(current_profile.application_settings.digital_zoom.roi.height, height);
            WEBSERVER_LOG_INFO("GET {} completed", PIPELINE_ENDPOINT_DIGITAL_ZOOM);
            return j;
        }));

    m_resources.m_srv.Put(PIPELINE_ENDPOINT_DIGITAL_ZOOM, [this](const nlohmann::json &j_body) {
        //{"mode":"DIGITAL_ZOOM_MODE_MAGNIFICATION", "magnification":1, "x":0,"y":0,"width":100,"height":100}
        WEBSERVER_LOG_INFO("PUT /digital_zoom_roi called");
        if (!j_body.contains("digital_zoom"))
        {
            WEBSERVER_LOG_ERROR("Digital zoom mode not found in request body");
            throw std::runtime_error("Digital zoom mode not found in request body");
        }
        auto mode = string_to_digital_zoom(j_body["digital_zoom"]["mode"].get<std::string>());
        if (j_body["digital_zoom"].contains("mode") && mode == digital_zoom_mode_t::DIGITAL_ZOOM_MODE_MAGNIFICATION &&
            !j_body["digital_zoom"].contains("magnification"))
        {
            WEBSERVER_LOG_ERROR("Digital zoom not found in request body");
            throw std::runtime_error("Digital zoom not found in request body");
        }
        else if (j_body["digital_zoom"].contains("mode") && mode == digital_zoom_mode_t::DIGITAL_ZOOM_MODE_ROI &&
                 (!j_body["digital_zoom"]["digital_zoom_roi"].contains("x") ||
                  !j_body["digital_zoom"]["digital_zoom_roi"].contains("y") ||
                  !j_body["digital_zoom"]["digital_zoom_roi"].contains("width") ||
                  !j_body["digital_zoom"]["digital_zoom_roi"].contains("height")))
        {
            WEBSERVER_LOG_ERROR("Digital zoom roi not found in request body");
            throw std::runtime_error("Digital zoom roi not found in request body");
        }
        auto magnification = j_body["digital_zoom"]["magnification"].get<int>();
        auto enable = j_body["digital_zoom"]["enabled"].get<bool>();
        if (mode == digital_zoom_mode_t::DIGITAL_ZOOM_MODE_MAGNIFICATION)
        {
            m_resources.m_event_bus->notify(
                EventType::CHANGE_DIGITAL_ZOOM,
                std::make_shared<ProfileDigitalZoomState>(ProfileDigitalZoomState(enable, magnification)));
            return nlohmann::json();
        }
        auto x = j_body["digital_zoom"]["digital_zoom_roi"]["x"].get<double>();
        auto y = j_body["digital_zoom"]["digital_zoom_roi"]["y"].get<double>();
        auto width = j_body["digital_zoom"]["digital_zoom_roi"]["width"].get<double>();
        auto height = j_body["digital_zoom"]["digital_zoom_roi"]["height"].get<double>();
        m_resources.m_event_bus->notify(EventType::CHANGE_DIGITAL_ZOOM_ROI,
                                        std::make_shared<ProfileDigitalZoomRoiState>(
                                            ProfileDigitalZoomRoiState(enable, magnification, x, y, width, height)));
        WEBSERVER_LOG_INFO("PUT /digital_zoom_roi completed");
        return nlohmann::json();
    });
}

void BasePipeline::register_resolution_endpoint()
{
    m_resources.m_srv.Get(
        PIPELINE_ENDPOINT_RESOLUTION, std::function<nlohmann::json()>([this]() {
            WEBSERVER_LOG_INFO("GET {} called", PIPELINE_ENDPOINT_RESOLUTION);
            auto expected_profile = m_app_resources->media_library.get_current_profile();
            if (!expected_profile.has_value())
            {
                WEBSERVER_LOG_ERROR("Failed to get current profile");
                throw std::runtime_error("Failed to get current profile");
            }
            config_profile_t current_profile = expected_profile.value();
            WEBSERVER_LOG_DEBUG("Current profile resolution: {}x{}",
                                current_profile.application_settings.application_input_streams.resolutions[0]
                                    .dimensions.destination_width,
                                current_profile.application_settings.application_input_streams.resolutions[0]
                                    .dimensions.destination_height);
            std::string resolution =
                get_resolution_string(current_profile.application_settings.application_input_streams.resolutions[0]
                                          .dimensions.destination_width,
                                      current_profile.application_settings.application_input_streams.resolutions[0]
                                          .dimensions.destination_height);
            nlohmann::json j;
            j["resolution"] = resolution;
            WEBSERVER_LOG_INFO("GET {} completed", PIPELINE_ENDPOINT_RESOLUTION);
            return j;
        }));

    // add endpoint for change resolution and then make the pipeline send the change event
    m_resources.m_srv.Put(PIPELINE_ENDPOINT_RESOLUTION, [this](const nlohmann::json &j_body) {
        WEBSERVER_LOG_INFO("PUT /resolution called");
        if (!j_body.contains("resolution"))
        {
            WEBSERVER_LOG_ERROR("Resolution not found in request body");
            throw std::runtime_error("Resolution not found in request body");
        }
        auto resolution = j_body["resolution"].get<std::string>();
        m_resources.m_event_bus->notify(EventType::CHANGE_RESOLUTION,
                                        std::make_shared<ProfileResolutionState>(ProfileResolutionState(resolution)));
        WEBSERVER_LOG_INFO("PUT /resolution completed");
        return nlohmann::json();
    });
}

void BasePipeline::register_denoise_endpoint()
{
    m_resources.m_srv.Get(
        PIPELINE_ENDPOINT_DENOISE, std::function<nlohmann::json()>([this]() {
            WEBSERVER_LOG_INFO("GET {} called", PIPELINE_ENDPOINT_DENOISE);
            auto expected_profile = m_app_resources->media_library.get_current_profile();
            if (!expected_profile.has_value())
            {
                WEBSERVER_LOG_ERROR("Failed to get current profile");
                throw std::runtime_error("Failed to get current profile");
            }
            config_profile_t current_profile = expected_profile.value();
            nlohmann::json j;
            j["enabled"] = current_profile.iq_settings.denoise.enabled;
            j["sensor"] = current_profile.iq_settings.denoise.sensor;
            j["loobback_count"] = current_profile.iq_settings.denoise.loopback_count;
            j["method"] = denoise_string_map.at(current_profile.iq_settings.denoise.denoising_quality);
            j["network"]["network_path"] = current_profile.iq_settings.denoise.network_config.network_path;
            j["network"]["y_channel"] = current_profile.iq_settings.denoise.network_config.y_channel;
            j["network"]["uv_channel"] = current_profile.iq_settings.denoise.network_config.uv_channel;
            j["network"]["feedback_y_channel"] = current_profile.iq_settings.denoise.network_config.feedback_y_channel;
            j["network"]["feedback_uv_channel"] =
                current_profile.iq_settings.denoise.network_config.feedback_uv_channel;
            j["network"]["output_y_channel"] = current_profile.iq_settings.denoise.network_config.output_y_channel;
            j["network"]["output_uv_channel"] = current_profile.iq_settings.denoise.network_config.output_uv_channel;

            WEBSERVER_LOG_INFO("GET {} completed", PIPELINE_ENDPOINT_DENOISE);
            return j;
        }));
}

void BasePipeline::register_current_profile_name_endpoint()
{
    m_resources.m_srv.Get(PIPELINE_ENDPOINT_CURRENT_PROFILE_NAME, std::function<nlohmann::json()>([this]() {
                              WEBSERVER_LOG_INFO("GET {} called", PIPELINE_ENDPOINT_CURRENT_PROFILE_NAME);
                              auto expected_profile = m_app_resources->media_library.get_current_profile();
                              if (!expected_profile.has_value())
                              {
                                  WEBSERVER_LOG_ERROR("Failed to get current profile");
                                  throw std::runtime_error("Failed to get current profile");
                              }
                              config_profile_t current_profile = expected_profile.value();
                              nlohmann::json j;
                              j["profile_name"] = current_profile.name;
                              WEBSERVER_LOG_INFO("GET {} completed", PIPELINE_ENDPOINT_CURRENT_PROFILE_NAME);
                              return j;
                          }));
}

void BasePipeline::register_automatic_algorithms_endpoint()
{
    m_resources.m_srv.Get(PIPELINE_ENDPOINT_AUTOMATIC_ALGORITHMS, std::function<nlohmann::json()>([this]() {
                              WEBSERVER_LOG_INFO("GET {} called", PIPELINE_ENDPOINT_AUTOMATIC_ALGORITHMS);
                              auto expected_profile = m_app_resources->media_library.get_current_profile_str();
                              if (!expected_profile.has_value())
                              {
                                  WEBSERVER_LOG_ERROR("Failed to get current profile");
                                  throw std::runtime_error("Failed to get current profile");
                              }
                              nlohmann::json full_profile = nlohmann::json::parse(expected_profile.value());
                              nlohmann::json j;
                              j["automatic_algorithms"] = full_profile["iq_settings"]["automatic_algorithms"];
                              WEBSERVER_LOG_INFO("GET {} completed", PIPELINE_ENDPOINT_AUTOMATIC_ALGORITHMS);
                              return j;
                          }));
    m_resources.m_srv.Put(PIPELINE_ENDPOINT_AUTOMATIC_ALGORITHMS, [this](const nlohmann::json &j_body) {
        WEBSERVER_LOG_INFO("PUT {} called", PIPELINE_ENDPOINT_AUTOMATIC_ALGORITHMS);
        if (!j_body.contains("automatic_algorithms"))
        {
            WEBSERVER_LOG_ERROR("automatic_algorithms not found in request body");
            throw std::runtime_error("automatic_algorithms not found in request body");
        }
        auto automatic_algorithms = j_body;
        std::string automatic_algorithms_str = automatic_algorithms.dump();
        WEBSERVER_LOG_DEBUG("automatic_algorithms json: {}", automatic_algorithms_str);
        m_app_resources->media_library.set_automatic_algorithm_configuration(automatic_algorithms_str);
        WEBSERVER_LOG_INFO("PUT {} completed", PIPELINE_ENDPOINT_AUTOMATIC_ALGORITHMS);
        return nlohmann::json();
    });
}

void BasePipeline::register_reset_stream_endpoint()
{
    m_resources.m_srv.Put(PIPELINE_ENDPOINT_RESET_STREAM, [this](const nlohmann::json &req) {
        WEBSERVER_LOG_INFO("POST {} called", PIPELINE_ENDPOINT_RESET_STREAM);
        stop();
        sleep(1);
        start();
        WEBSERVER_LOG_INFO("POST {} completed", PIPELINE_ENDPOINT_RESET_STREAM);
        return nlohmann::json();
    });
}

void BasePipeline::register_endpoints()
{
    register_framerate_endpoint();
    register_flip_endpoint();
    register_rotation_endpoint();
    register_dewarp_endpoint();
    register_grayscale_endpoint();
    register_freeze_endpoint();
    register_digital_zoom_endpoint();
    register_resolution_endpoint();
    register_denoise_endpoint();
    register_current_profile_name_endpoint();
    register_automatic_algorithms_endpoint();
    register_reset_stream_endpoint();
}

void BasePipeline::unregister_endpoints()
{
    for (const auto &endpoint : PIPELINE_ALL_ENDPOINTS)
    {
        m_resources.m_srv.Unregister(endpoint);
    }
}
