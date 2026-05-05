#include "common.hpp"

// resolution maps
namespace webserver::common
{
const std::unordered_map<Resolution, std::pair<uint32_t, uint32_t>> resolution_map = {
    {Resolution::_HD, {1280, 720}},   {Resolution::_FHD, {1920, 1080}}, {Resolution::_QHD, {2560, 1440}},
    {Resolution::_5MP, {2592, 1944}}, {Resolution::_4K, {3840, 2160}},  {Resolution::_SD, {640, 480}},
    {Resolution::_4MP, {2688, 1520}}};
}

webserver::common::Resolution string_to_resolution(const std::string &resolution_str)
{
    for (const auto &[res, dims] : webserver::common::resolution_map)
    {
        if (nlohmann::json(res).get<std::string>() == resolution_str)
        {
            return res;
        }
    }
    throw std::invalid_argument("Invalid resolution string");
}

std::string get_resolution_string(uint32_t width, uint32_t height)
{
    auto target = std::make_pair(width, height);
    auto rotated_target = std::make_pair(height, width);
    for (auto const &[res, dims] : webserver::common::resolution_map)
    {
        if (dims == target || dims == rotated_target)
        {
            return nlohmann::json(res).get<std::string>();
        }
    }
    return "Unknown Resolution";
}

// flip
const std::unordered_map<std::string, flip_direction_t> flip_string_map = {
    {"FLIP_DIRECTION_NONE", FLIP_DIRECTION_NONE},
    {"FLIP_DIRECTION_HORIZONTAL", FLIP_DIRECTION_HORIZONTAL},
    {"FLIP_DIRECTION_VERTICAL", FLIP_DIRECTION_VERTICAL},
    {"FLIP_DIRECTION_BOTH", FLIP_DIRECTION_BOTH}};

std::string flip_direction_to_string(flip_direction_t flip)
{
    for (auto const &[key, val] : flip_string_map)
    {
        if (val == flip)
            return key;
    }
    throw std::invalid_argument("Invalid flip direction");
}

// rotation
const std::unordered_map<std::string, rotation_angle_t> rotation_string_map = {
    {"ROTATION_ANGLE_0", ROTATION_ANGLE_0},
    {"ROTATION_ANGLE_90", ROTATION_ANGLE_90},
    {"ROTATION_ANGLE_180", ROTATION_ANGLE_180},
    {"ROTATION_ANGLE_270", ROTATION_ANGLE_270}};

std::string rotation_angle_to_string(rotation_angle_t angle)
{
    for (auto const &[key, val] : rotation_string_map)
    {
        if (val == angle)
            return key;
    }
    throw std::invalid_argument("Invalid rotation angle");
}

bool isPortrait(rotation_angle_t a)
{
    return a == rotation_angle_t::ROTATION_ANGLE_90 || a == rotation_angle_t::ROTATION_ANGLE_270;
};

// digital zoom & denoise
const std::unordered_map<digital_zoom_mode_t, std::string> digital_zoom_mode_string_map = {
    {DIGITAL_ZOOM_MODE_ROI, "DIGITAL_ZOOM_MODE_ROI"},
    {DIGITAL_ZOOM_MODE_MAGNIFICATION, "DIGITAL_ZOOM_MODE_MAGNIFICATION"}};

digital_zoom_mode_t string_to_digital_zoom(std::string mode)
{
    for (auto const &[key, val] : digital_zoom_mode_string_map)
    {
        if (val == mode)
            return key;
    }
    throw std::invalid_argument("Invalid digital zoom mode");
}
const std::unordered_map<denoise_method_t, std::string> denoise_string_map = {
    {DENOISE_METHOD_NONE, "DENOISE_METHOD_NONE"},
    {DENOISE_METHOD_VD1, "HIGH_QUALITY"},
    {DENOISE_METHOD_VD2, "BALANCED"},
    {DENOISE_METHOD_VD3, "HIGH_PERFORMANCE"}};

const std::string MACHINE_FILE_PATH = "/sys/devices/soc0/machine";
const std::string HAILO_15_IDENTIFIER = "Hailo-15";
const std::string HAILO_15L_IDENTIFIER = "Hailo-15L";

Architecture get_hailo_architecture()
{
    std::ifstream file(MACHINE_FILE_PATH);
    if (!file.is_open())
    {
        std::cerr << "Failed to open machine file" << std::endl;
        return Architecture::UNKNOWN;
    }

    std::string line;
    std::getline(file, line);
    file.close();
    auto to_lower = [](const std::string &str) {
        std::string lower_str = str;
        std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);
        return lower_str;
    };

    std::string lower_line = to_lower(line);
    if (lower_line.find(to_lower(HAILO_15L_IDENTIFIER)) != std::string::npos)
    {
        return Architecture::Hailo15L;
    }
    else if (lower_line.find(to_lower(HAILO_15_IDENTIFIER)) != std::string::npos)
    {
        return Architecture::Hailo15H;
    }

    return Architecture::UNKNOWN;
}

std::string profile_type_to_display_name(ProfileType type, bool is_hdm)
{
    if (type == ProfileType::LowlightBayer && is_hdm)
    {
        return "AI-ISP Gen3";
    }
    return nlohmann::json(type).get<std::string>();
}

ProfileType display_name_to_profile_type(const std::string &name)
{
    // "AI-ISP Gen3" maps to LowlightBayer (same as Gen2, distinguished by denoise config)
    if (name == "AI-ISP Gen3")
    {
        return ProfileType::LowlightBayer;
    }
    return nlohmann::json(name).get<ProfileType>();
}

bool is_env_variable_on(const std::string &env_var_name, const std::string &required_value)
{
    auto env_var = std::getenv(env_var_name.c_str());
    if (nullptr == env_var)
        return false;
    std::string val(env_var);
    return (val == required_value || val == "true" || val == "TRUE" || val == "True");
}
