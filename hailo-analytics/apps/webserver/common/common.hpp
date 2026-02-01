#pragma once
#include <iostream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "media_library/media_library_api_types.hpp"

#define V4L2_DEVICE_NAME "/dev/video0"
#define MEDIALIB_DEWARP_DSP_OPTIMIZATION_ENV_VAR ("MEDIALIB_DEWARP_DSP_OPTIMIZATION")

namespace webserver::common
{

enum class Resolution
{
    _HD,
    _FHD,
    _QHD,
    _5MP,
    _4K,
    _SD,
    _4MP
};

NLOHMANN_JSON_SERIALIZE_ENUM(Resolution, {
                                             {Resolution::_HD, "HD"},
                                             {Resolution::_FHD, "FHD"},
                                             {Resolution::_QHD, "QHD"},
                                             {Resolution::_5MP, "5MP"},
                                             {Resolution::_4K, "4K"},
                                             {Resolution::_SD, "SD"},
                                             {Resolution::_4MP, "4MP"},
                                         })

extern const std::unordered_map<Resolution, std::pair<uint32_t, uint32_t>> resolution_map;
} // namespace webserver::common

webserver::common::Resolution string_to_resolution(const std::string &resolution_str);

std::string get_resolution_string(uint32_t width, uint32_t height);

extern const std::unordered_map<std::string, flip_direction_t> flip_string_map;
std::string flip_direction_to_string(flip_direction_t flip_direction);

extern const std::unordered_map<std::string, rotation_angle_t> rotation_string_map;
std::string rotation_angle_to_string(rotation_angle_t rotation_angle);
bool isPortrait(rotation_angle_t a);

extern const std::unordered_map<digital_zoom_mode_t, std::string> digital_zoom_mode_string_map;
digital_zoom_mode_t string_to_digital_zoom(std::string mode);

extern const std::unordered_map<denoise_method_t, std::string> denoise_string_map;

template <typename T>
inline bool json_extract_value(const nlohmann::json &json, const std::string &key, T &out,
                               std::string *return_msg = nullptr)
{
    if (json.find(key) == json.end())
    {
        if (return_msg)
        {
            *return_msg = "Missing " + key + " in JSON";
        }
        return false;
    }

    try
    {
        out = json[key].get<T>();
    }
    catch (nlohmann::json::exception &e)
    {
        if (return_msg)
        {
            *return_msg = "Failed to extract " + key + " from JSON: " + e.what();
        }
        return false;
    }
    return true;
}

static inline bool is_env_variable_on(const std::string &env_var_name, const std::string &required_value = "1")
{
    auto env_var = std::getenv(env_var_name.c_str());
    return ((nullptr != env_var) && (required_value == env_var));
}

enum class Architecture
{
    Hailo15H,
    Hailo15L,
    UNKNOWN
};

NLOHMANN_JSON_SERIALIZE_ENUM(Architecture, {{Architecture::Hailo15H, "Hailo15H"},
                                            {Architecture::Hailo15L, "Hailo15L"},
                                            {Architecture::UNKNOWN, "UNKNOWN"}})

#define ARCHITECTURE_STRING(arch) (nlohmann::json(arch).dump())

Architecture get_hailo_architecture();

enum class pipeline_t
{
    Basic,
    Detection,
    CLIP,
    ProfileManager,
    FaceLandmarks
};

NLOHMANN_JSON_SERIALIZE_ENUM(pipeline_t, {{pipeline_t::Basic, "Off"},
                                          {pipeline_t::Detection, "Detection"},
                                          {pipeline_t::CLIP, "Clip text search"},
                                          {pipeline_t::ProfileManager, "Profile Manager"},
                                          {pipeline_t::FaceLandmarks, "Face Landmarks"}})

enum class ProfileType
{
    Daylight,
    Lowlight,
    HighDynamicRange,
    LowlightBayer,
    DenoiseHdr,
};

NLOHMANN_JSON_SERIALIZE_ENUM(ProfileType, {{ProfileType::Daylight, "Daylight"},
                                           {ProfileType::Lowlight, "AI-ISP Gen1"},
                                           {ProfileType::HighDynamicRange, "High Dynamic Range"},
                                           {ProfileType::LowlightBayer, "AI-ISP Gen2"},
                                           {ProfileType::DenoiseHdr, "Denoise HDR"}})
