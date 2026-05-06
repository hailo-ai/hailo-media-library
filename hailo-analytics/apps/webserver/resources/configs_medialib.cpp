#include "configs.hpp"
#include "common/common.hpp"
#include "media_library/config_parser.hpp"
#include "media_library/encoder_config_types.hpp"
#include "media_library/media_library_types.hpp"
#include "pipeline/pipeline.hpp"

#include <algorithm>
#include <array>
#include <optional>

using namespace webserver::resources;

std::string ConfigResourceMedialib::name()
{
    return "medialib config";
}

nlohmann::json ConfigResourceMedialib::get_current_profile()
{
    return m_profile;
}

nlohmann::json ConfigResourceMedialib::get_current_medialib_config()
{
    return m_medialib_config;
}

ConfigResourceMedialib::ConfigResourceMedialib(std::shared_ptr<EventBus> event_bus, std::string config_path)
    : ConfigResourceBase(event_bus)
{
    // Load default medialib config
    auto medialib_config = load_config_from_file(config_path);
    if (!medialib_config.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to load default medialib config: {}", medialib_config.error());
        throw std::runtime_error("Failed to load default medialib config: " + medialib_config.error());
    }
    m_medialib_config = medialib_config.value();
    // Load default profile config
    m_default_profile_name = m_medialib_config["default_profile"];
    // Determine HDM mode from the lowlight bayer profile
    m_is_hdm_mode = check_lowlight_bayer_is_hdm();
    // Initialize throttling state to uninitialized
    m_current_throttling_state = media_library_throttling_state_t::THROTTLING_STATE_UNINITIALIZED;

    subscribe_callback({EventType::PROFILE_UPDATE, EventType::PIPELINE_READY}, EventPriority::EVENT_PRIORITY_VERY_HIGH,
                       [this](ResourceStateChangeNotification notification) {
                           WEBSERVER_LOG_INFO("Received PROFILE_UPDATE notification");
                           auto state = notification.getResourceStateFromBase<ProfileState>();
                           ProfileStateData data = state->value;
                           std::unique_lock<std::shared_mutex> lock(m_config_mutex);
                           m_current_profile = data.profile_config;
                           m_supported_profiles = data.supported_profiles;
                           m_current_profile_name = data.active_profile_name;
                           m_current_profile_type = data.active;
                           auto conf_succsess = extract_profile_data(m_current_profile_name);
                           if (!conf_succsess.has_value())
                           {
                               WEBSERVER_LOG_ERROR("Failed to extract profile data: {}", conf_succsess.error());
                               throw std::runtime_error("Failed to extract profile data: " + conf_succsess.error());
                           }
                           WEBSERVER_LOG_INFO("Current profile updated to: {}",
                                              static_cast<int>(m_current_profile_type));
                       });

    // Subscribe to THROTTLING_STATE_UPDATE to keep cached state current
    subscribe_callback(EventType::THROTTLING_STATE_UPDATE, EventPriority::EVENT_PRIORITY_HIGH,
                       [this](ResourceStateChangeNotification notification) {
                           WEBSERVER_LOG_INFO("Received THROTTLING_STATE_UPDATE notification");
                           auto state = notification.getResourceStateFromBase<MediaLibraryThrottlingState>();
                           m_current_throttling_state = state->value;
                           WEBSERVER_LOG_INFO("Throttling state updated to: {}",
                                              static_cast<nlohmann::json>(m_current_throttling_state).dump());
                       });

    subscribe_callback(EventType::ENCODER_CHANGE, EventPriority::EVENT_PRIORITY_HIGH,
                       [this](ResourceStateChangeNotification notification) {
                           WEBSERVER_LOG_INFO("Received ENCODER_CHANGE notification");
                           auto state = notification.getResourceStateFromBase<EncoderState>();
                           m_encoder_name = state->value;
                           WEBSERVER_LOG_INFO("Encoder name changed to: {}", m_encoder_name);
                       });
}

void ConfigResourceMedialib::reset_config()
{
    std::unique_lock<std::shared_mutex> lock(m_config_mutex);
    auto conf_succsess = extract_profile_data(m_current_profile_name);
    if (!conf_succsess.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to extract profile data: {}", conf_succsess.error());
        throw std::runtime_error("Failed to extract profile data: " + conf_succsess.error());
    }
}

tl::expected<void, std::string> ConfigResourceMedialib::extract_profile_data(const std::string &profile_name)
{
    auto profile_json = get_profile(profile_name);
    if (!profile_json.has_value())
    {
        return tl::make_unexpected("Failed to load profile: " + profile_json.error());
    }

    auto profile_with_gyro = enable_gyro_if_exist(profile_json.value());
    if (!profile_with_gyro.has_value())
    {
        return tl::make_unexpected("Failed to enable gyro: " + profile_with_gyro.error());
    }
    m_profile = profile_with_gyro.value();

    // Load frontend config
    auto frontend_default_config = extract_frontend_config();
    if (!frontend_default_config.has_value())
    {
        return tl::make_unexpected("Failed to load default frontend config: " + frontend_default_config.error());
    }
    m_frontend_default_config = frontend_default_config.value();

    // Load encoder and osd config
    auto encoder_default_config = extract_encoder_config();
    if (!encoder_default_config.has_value())
    {
        return tl::make_unexpected("Failed to load default encoder config: " + encoder_default_config.error());
    }
    m_encoder_osd_default_config = encoder_default_config.value();
    return {};
}

tl::expected<nlohmann::json, std::string> ConfigResourceMedialib::extract_encoder_config()
{
    nlohmann::json encoder_config;
    try
    {
        if (m_encoder_name.empty()) // use first encoder as default
        {
            m_encoder_name = m_current_profile.encoded_output_streams.begin()->first;
        }

        // NOTE: waiting for encoder api from mosko
        //  encoder_config_t encoder_config = m_current_profile.m_encoders[STREAM_4K];
        auto encoder_config_struct = m_current_profile.encoded_output_streams[m_encoder_name];
        // updated from the real struct)
        ConfigParser config_parser = ConfigParser(CONFIG_SCHEMA_ENCODER_AND_BLENDING);
        std::string encoder_config_str =
            config_parser.config_struct_to_string<config_encoded_output_stream_t>(encoder_config_struct);
        encoder_config = nlohmann::json::parse(encoder_config_str);
    }
    catch (const std::exception &e)
    {
        WEBSERVER_LOG_ERROR("Failed to extract encoder config: {}", e.what());
        return tl::make_unexpected(std::string(e.what()));
    }
    return encoder_config;
}

tl::expected<nlohmann::json, std::string> ConfigResourceMedialib::extract_frontend_config()
{
    nlohmann::json frontend_config;
    try
    {
        auto frontend_config_struct = m_current_profile.to_frontend_config();
        ConfigParser config_parser = ConfigParser(CONFIG_SCHEMA_FRONTEND);
        std::string frontend_config_str =
            config_parser.config_struct_to_string<frontend_config_t>(frontend_config_struct);
        frontend_config = nlohmann::json::parse(frontend_config_str);
    }
    catch (const std::exception &e)
    {
        WEBSERVER_LOG_ERROR("Failed to extract frontend config: {}", e.what());
        return tl::make_unexpected(std::string(e.what()));
    }
    return frontend_config;
}

tl::expected<nlohmann::json, std::string> ConfigResourceMedialib::get_profile(const nlohmann::json &profile_name)
{
    for (auto profile : m_medialib_config["profiles"])
    {
        if (profile["name"] == profile_name)
        {
            return load_config_from_file(profile["config_file"]);
        }
    }
    return tl::make_unexpected("Profile not found");
}

tl::expected<nlohmann::json, std::string> ConfigResourceMedialib::enable_gyro_if_exist(nlohmann::json profile)
{
    try
    {
        // Check if stabilizer_settings exists in the profile
        if (!profile.contains("stabilizer_settings"))
        {
            WEBSERVER_LOG_INFO("Stabilizer settings not found in profile, skipping gyro initialization");
            return profile;
        }

        nlohmann::json stabilizer_settings;

        // Check if stabilizer_settings is a file path (new format) or inline object (old format)
        if (profile["stabilizer_settings"].is_string())
        {
            // New format: stabilizer_settings contains a file path
            std::string stabilizer_config_path = profile["stabilizer_settings"];
            auto loaded_config = load_config_from_file(stabilizer_config_path);
            if (!loaded_config.has_value())
            {
                WEBSERVER_LOG_ERROR("Failed to load stabilizer settings from file: {}", loaded_config.error());
                return tl::make_unexpected("Failed to load stabilizer settings: " + loaded_config.error());
            }
            stabilizer_settings = loaded_config.value();
        }
        else
        {
            // Old format: stabilizer_settings contains inline config
            stabilizer_settings = profile["stabilizer_settings"];
        }

        // Check if gyro configuration exists in the loaded stabilizer settings
        if (!stabilizer_settings.contains("gyro") || !stabilizer_settings["gyro"].contains("sensor_name") ||
            !stabilizer_settings["gyro"].contains("sensor_frequency") || !stabilizer_settings["gyro"].contains("scale"))
        {
            WEBSERVER_LOG_INFO("Gyro settings not found in stabilizer settings, skipping gyro initialization");
            return profile;
        }

        auto sensor_name = stabilizer_settings["gyro"]["sensor_name"];
        auto sensor_frequency = stabilizer_settings["gyro"]["sensor_frequency"];
        auto gyro_scale = stabilizer_settings["gyro"]["scale"];
        auto gyro_dev = std::make_unique<GyroDevice>(sensor_name, sensor_frequency, gyro_scale);
        if (gyro_dev->exists() == GYRO_STATUS_SUCCESS)
        {
            // For new format, we need to update the file and reload it
            if (profile["stabilizer_settings"].is_string())
            {
                stabilizer_settings["gyro"]["enabled"] = true;
                // Note: In a production system, you might want to save this back to the file
                // For now, we'll update the in-memory copy
                gyro_exist = true;
            }
            else
            {
                // Old format: update inline
                profile["stabilizer_settings"]["gyro"]["enabled"] = true;
                gyro_exist = true;
            }
        }
        gyro_dev = nullptr;
        return profile;
    }
    catch (const std::exception &e)
    {
        WEBSERVER_LOG_ERROR("Failed to enable gyro: {}", e.what());
        return tl::make_unexpected(std::string(e.what()));
    }
}

static bool has_hdm_denoise_fields(const nlohmann::json &iq_settings)
{
    if (!iq_settings.contains("denoise"))
    {
        return false;
    }
    const auto &denoise = iq_settings.at("denoise");
    if (!denoise.value("bayer", false) || !denoise.contains("network"))
    {
        return false;
    }
    static const std::array<std::string, 4> HDM_FIELDS = {"input_fusion_feedback", "output_fusion_feedback",
                                                          "input_gamma_feedback", "output_gamma_feedback"};
    const auto &network = denoise.at("network");
    return std::all_of(HDM_FIELDS.begin(), HDM_FIELDS.end(), [&network](const std::string &field) {
        return network.contains(field) && !network[field].get<std::string>().empty();
    });
}

static std::optional<nlohmann::json> resolve_iq_settings(const nlohmann::json &profile_json)
{
    // iq_settings may be a file path (string) or an already-flattened inline object
    if (profile_json.contains("iq_settings") && profile_json["iq_settings"].is_string())
    {
        std::string iq_path = profile_json["iq_settings"];
        std::ifstream iq_file(iq_path);
        if (!iq_file.is_open())
        {
            WEBSERVER_LOG_ERROR("Failed to open IQ settings file: {}", iq_path);
            return std::nullopt;
        }
        nlohmann::json iq_settings;
        iq_file >> iq_settings;
        return iq_settings;
    }
    if (profile_json.contains("iq_settings_content"))
    {
        return std::optional<nlohmann::json>{profile_json["iq_settings_content"]};
    }
    return std::nullopt;
}

bool ConfigResourceMedialib::check_lowlight_bayer_is_hdm() const
{
    static constexpr std::string_view LOWLIGHT_BAYER_PREFIX = "Lowlight_Bayer";
    for (const auto &profile : m_medialib_config["profiles"])
    {
        std::string name = profile["name"];
        if (!name.starts_with(LOWLIGHT_BAYER_PREFIX))
        {
            continue;
        }

        try
        {
            std::string config_file = profile["config_file"];
            std::ifstream file(config_file);
            if (!file.is_open())
            {
                WEBSERVER_LOG_ERROR("Failed to open lowlight bayer config file: {}", config_file);
                continue;
            }
            nlohmann::json profile_json;
            file >> profile_json;
            auto iq_settings = resolve_iq_settings(profile_json);
            if (!iq_settings.has_value())
            {
                continue;
            }
            if (has_hdm_denoise_fields(iq_settings.value()))
            {
                return true;
            }
        }
        catch (const std::exception &e)
        {
            WEBSERVER_LOG_ERROR("Failed to parse lowlight bayer config: {}", e.what());
            continue;
        }
    }
    return false;
}

tl::expected<nlohmann::json, std::string> ConfigResourceMedialib::load_config_from_file(const std::string &file_path)
{
    std::ifstream configFile(file_path);
    if (!configFile.is_open())
    {
        std::string error_msg = "Failed to open config file: " + file_path;
        WEBSERVER_LOG_ERROR("{}", error_msg);
        return tl::make_unexpected(error_msg);
    }

    nlohmann::json configJson;
    try
    {
        configFile >> configJson;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        std::string error_msg = "JSON parse error in file " + file_path + ": " + e.what();
        WEBSERVER_LOG_ERROR("{}", error_msg);
        configFile.close();
        return tl::make_unexpected(error_msg);
    }
    catch (const std::exception &e)
    {
        std::string error_msg = "Error reading config file " + file_path + ": " + e.what();
        WEBSERVER_LOG_ERROR("{}", error_msg);
        configFile.close();
        return tl::make_unexpected(error_msg);
    }

    configFile.close();
    return configJson;
}

void ConfigResourceMedialib::update_profile()
{
    WEBSERVER_LOG_INFO("Updating profile");
    on_resource_change(EventType::PROFILE_UPDATE_REQUEST, std::make_shared<EmptyState>());
}

nlohmann::json ConfigResourceMedialib::build_profile_response() const
{
    nlohmann::json profile_json;
    profile_json["profile"]["active"] = profile_type_to_display_name(m_current_profile_type, m_is_hdm_mode);
    nlohmann::json supported_list = nlohmann::json::array();
    for (const auto &profile : m_supported_profiles)
    {
        supported_list.push_back(profile_type_to_display_name(profile, m_is_hdm_mode));
    }
    profile_json["profile"]["supported"] = supported_list;
    return profile_json;
}

void ConfigResourceMedialib::http_register(HTTPServer &srv)
{
    srv.Post("/reset_all", [this](const nlohmann::json &req) {
        WEBSERVER_LOG_INFO("POST /reset_all called");
        try
        {
            on_resource_change(EventType::RESET_CONFIG, std::make_shared<EmptyState>());
        }
        catch (const std::exception &e)
        {
            WEBSERVER_LOG_ERROR("Failed to reset all: {}", e.what());
        }
        WEBSERVER_LOG_INFO("POST /reset_all completed");
    });

    srv.Put("/profile", [this](const nlohmann::json &j_body) {
        //{ "profile_name": "profile_name" }
        WEBSERVER_LOG_INFO("PUT /profile called");
        if (!j_body.contains("profile") || !j_body["profile"].contains("active"))
        {
            WEBSERVER_LOG_ERROR("Profile name not found in request body");
            throw std::runtime_error("Profile name not found in request body");
        }
        auto profile_name = display_name_to_profile_type(j_body["profile"]["active"].get<std::string>());
        {
            std::shared_lock<std::shared_mutex> lock(m_config_mutex);
            if (profile_name == m_current_profile_type)
            {
                WEBSERVER_LOG_INFO("Profile {} is already active", j_body["profile"]["active"]);
                auto response = build_profile_response();
                WEBSERVER_LOG_INFO("PUT /profile completed");
                return response;
            }
        }

        on_resource_change(EventType::SWITCH_PROFILE, std::make_shared<ProfileTypeState>(profile_name));
        {
            std::unique_lock<std::shared_mutex> lock(m_config_mutex);
            m_current_profile_type = profile_name;
        }

        nlohmann::json profile_json;
        {
            std::shared_lock<std::shared_mutex> lock(m_config_mutex);
            profile_json = build_profile_response();
        }
        WEBSERVER_LOG_INFO("PUT /profile completed");
        return profile_json;
    });

    srv.Get("/profile", std::function<nlohmann::json()>([this]() {
                WEBSERVER_LOG_INFO("GET /profile called");
                nlohmann::json profile_json;
                {
                    std::shared_lock<std::shared_mutex> lock(m_config_mutex);
                    profile_json = build_profile_response();
                }
                WEBSERVER_LOG_INFO("GET /profile completed");
                return profile_json;
            }));

    srv.Put("/digital_image_stabilization", [this](const nlohmann::json &j_body) {
        WEBSERVER_LOG_INFO("PUT /digital_image_stabilization called");
        if (!j_body.contains("digital_image_stabilization"))
        {
            WEBSERVER_LOG_ERROR("Digital Image stabilization not found in request body");
            throw std::runtime_error("Digital Image stabilization not found in request body");
        }
        auto image_stabilization = j_body["digital_image_stabilization"]["active"].get<bool>();
        on_resource_change(EventType::CHANGE_DIS,
                           std::make_shared<ProfileDisState>(ProfileDisState(image_stabilization)));
        WEBSERVER_LOG_INFO("PUT /digital_image_stabilization completed");
        return nlohmann::json();
    });

    srv.Put("/electronic_image_stabilization", [this](const nlohmann::json &j_body) {
        WEBSERVER_LOG_INFO("PUT /electronic_image_stabilization called");
        if (!j_body.contains("electronic_image_stabilization"))
        {
            WEBSERVER_LOG_ERROR("Image stabilization not found in request body");
            throw std::runtime_error("Image stabilization not found in request body");
        }
        {
            std::shared_lock<std::shared_mutex> lock(m_config_mutex);
            if (!gyro_exist)
            {
                WEBSERVER_LOG_ERROR("Gyro not exist, cannot set electronic image stabilization");
                throw std::runtime_error("Gyro not exist, cannot set electronic image stabilization");
            }
        }
        auto image_stabilization = j_body["electronic_image_stabilization"]["active"].get<bool>();
        on_resource_change(EventType::CHANGE_EIS,
                           std::make_shared<ProfileEisState>(ProfileEisState(image_stabilization)));

        WEBSERVER_LOG_INFO("PUT /electronic_image_stabilization completed");
        return nlohmann::json();
    });

    srv.Get("/digital_image_stabilization", std::function<nlohmann::json()>([this]() {
                WEBSERVER_LOG_INFO("GET /image_stabilization called");
                update_profile();
                nlohmann::json j;
                {
                    std::shared_lock<std::shared_mutex> lock(m_config_mutex);
                    j["digital_image_stabilization"]["active"] = m_current_profile.stabilizer_settings.dis.enabled;
                }
                WEBSERVER_LOG_INFO("GET /digital_image_stabilization completed");
                return j;
            }));

    srv.Get("/electronic_image_stabilization", std::function<nlohmann::json()>([this]() {
                WEBSERVER_LOG_INFO("GET /image_stabilization called");
                update_profile();
                nlohmann::json j;
                {
                    std::shared_lock<std::shared_mutex> lock(m_config_mutex);
                    j["electronic_image_stabilization"]["gyro_exist"] = gyro_exist;
                    j["electronic_image_stabilization"]["active"] =
                        gyro_exist && m_current_profile.stabilizer_settings.eis.enabled;
                }
                WEBSERVER_LOG_INFO("GET /electronic_image_stabilization completed");
                return j;
            }));

    srv.Get("/architecture", std::function<nlohmann::json()>([this]() {
                WEBSERVER_LOG_INFO("GET /architecture called");
                nlohmann::json j;
                j["architecture"] = get_hailo_architecture();
                WEBSERVER_LOG_INFO("GET /architecture completed");
                return j;
            }));

    srv.Get("/throttling_state", std::function<nlohmann::json()>([this]() {
                WEBSERVER_LOG_INFO("GET /throttling_state called");
                nlohmann::json j;
                j["throttling_state"] = m_current_throttling_state;
                WEBSERVER_LOG_INFO("GET /throttling_state completed");
                return j;
            }));
}
