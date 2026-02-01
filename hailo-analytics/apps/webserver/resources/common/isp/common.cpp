#include "common.hpp"
#include <cstring>
#include <stdio.h>
#include <filesystem>
#include <mutex>

using namespace webserver::common;
static constexpr const char *TRIPLE_A_CONFIG_PATH = "/usr/bin/3aconfig.json";
static constexpr const char *TRIPLE_A_CONFIG_PATH_SIM_LIMK = "/usr/bin/isp_3aconfig_0";

void webserver::common::from_json(const nlohmann::json &json, webserver::common::stream_isp_params_t &params)
{
    json.at("saturation").get_to(params.saturation);
    json.at("brightness").get_to(params.brightness);
    json.at("contrast").get_to(params.contrast);
    json.at("sharpness_up").get_to(params.sharpness_up);
    json.at("sharpness_down").get_to(params.sharpness_down);
}

void webserver::common::to_json(nlohmann::json &json, const webserver::common::stream_isp_params_t &params)
{
    json = nlohmann::json{{"saturation", params.saturation},
                          {"brightness", params.brightness},
                          {"contrast", params.contrast},
                          {"sharpness_up", params.sharpness_up},
                          {"sharpness_down", params.sharpness_down}};
}

void webserver::common::from_json(const nlohmann::json &json, webserver::common::stream_params_t &params)
{
    json.at("saturation").get_to(params.saturation);
    json.at("brightness").get_to(params.brightness);
    json.at("contrast").get_to(params.contrast);
    json.at("sharpness").get_to(params.sharpness);
}

void webserver::common::to_json(nlohmann::json &json, const webserver::common::stream_params_t &params)
{
    json = nlohmann::json{{"saturation", params.saturation},
                          {"brightness", params.brightness},
                          {"contrast", params.contrast},
                          {"sharpness", params.sharpness}};
}

void webserver::common::from_json(const nlohmann::json &json, webserver::common::auto_exposure_t &params)
{
    json.at("enabled").get_to(params.enabled);
    json.at("gain").get_to(params.gain);
    json.at("integration_time").get_to(params.integration_time);
    json.at("backlight").get_to(params.backlight);
}

void webserver::common::to_json(nlohmann::json &json, const webserver::common::auto_exposure_t &params)
{
    json = nlohmann::json{{"enabled", params.enabled},
                          {"gain", params.gain},
                          {"integration_time", params.integration_time},
                          {"backlight", params.backlight}};
}

void webserver::common::from_json(const nlohmann::json &json, webserver::common::wide_dynamic_range_t &params)
{
    json.at("value").get_to(params.value);
}

void webserver::common::to_json(nlohmann::json &json, const webserver::common::wide_dynamic_range_t &params)
{
    json = nlohmann::json{{"value", params.value}};
}

void webserver::common::from_json(const nlohmann::json &json, webserver::common::auto_white_balance_t &params)
{
    json.at("value").get_to(params.value);
}

void webserver::common::to_json(nlohmann::json &json, const webserver::common::auto_white_balance_t &params)
{
    json = nlohmann::json{{"value", params.value}};
}

void webserver::common::from_json(const nlohmann::json &json, webserver::common::tuning_t &params)
{
    json.at("value").get_to(params.value);
}

void webserver::common::to_json(nlohmann::json &json, const webserver::common::tuning_t &params)
{
    json = nlohmann::json{{"value", params.value}};
}

void webserver::common::from_json(const nlohmann::json &json, ae_ranges_t &params)
{
    throw std::runtime_error("ae ranges cant be defined by frontend");
}
void webserver::common::to_json(nlohmann::json &json, const ae_ranges_t &params)
{
    json = nlohmann::json{
        {"ae_gain", {{"max", params.ae_gain.max}, {"min", params.ae_gain.min}}},
        {"ae_integration_time", {{"max", params.ae_integration_time.max}, {"min", params.ae_integration_time.min}}}};
}

stream_isp_params_t::stream_isp_params_t(int32_t saturation, int32_t brightness, int32_t contrast,
                                         uint16_t sharpness_down, uint16_t sharpness_up)
    : saturation(saturation), brightness(brightness), contrast(contrast), sharpness_down(sharpness_down),
      sharpness_up(sharpness_up)
{
}

stream_isp_params_t stream_isp_params_t::from_stream_params(const stream_params_t &params)
{
    int32_t v_brightness = v4l2_ctrl::calculate_value_from_precentage<int32_t>(
        params.brightness, v4l2_ctrl::Video0Ctrl::BRIGHTNESS, brightness);
    int32_t v_contrast =
        v4l2_ctrl::calculate_value_from_precentage<int32_t>(params.contrast, v4l2_ctrl::Video0Ctrl::CONTRAST, contrast);
    int32_t v_saturation = v4l2_ctrl::calculate_value_from_precentage<int32_t>(
        params.saturation, v4l2_ctrl::Video0Ctrl::SATURATION, saturation);
    int32_t v_sharpness_down = v4l2_ctrl::calculate_value_from_precentage<int32_t>(
        params.sharpness, v4l2_ctrl::Video0Ctrl::SHARPNESS_DOWN, sharpness_down);
    int32_t v_sharpness_up = v4l2_ctrl::calculate_value_from_precentage<int32_t>(
        params.sharpness, v4l2_ctrl::Video0Ctrl::SHARPNESS_UP, sharpness_up);

    return stream_isp_params_t(v_saturation, v_brightness, v_contrast, v_sharpness_down, v_sharpness_up);
}

stream_params_t stream_isp_params_t::to_stream_params(const stream_isp_params_t &params)
{
    uint16_t p_brightness = v4l2_ctrl::calculate_precentage_from_value<int32_t>(
        params.brightness, v4l2_ctrl::Video0Ctrl::BRIGHTNESS, brightness);
    uint16_t p_contrast =
        v4l2_ctrl::calculate_precentage_from_value<int32_t>(params.contrast, v4l2_ctrl::Video0Ctrl::CONTRAST, contrast);
    uint16_t p_saturation = v4l2_ctrl::calculate_precentage_from_value<int32_t>(
        params.saturation, v4l2_ctrl::Video0Ctrl::SATURATION, saturation);
    uint16_t p_sharpness = v4l2_ctrl::calculate_precentage_from_value<uint16_t>(
        params.sharpness_down, v4l2_ctrl::Video0Ctrl::SHARPNESS_DOWN, sharpness_down);

    return stream_params_t{p_saturation, p_brightness, p_contrast, p_sharpness};
}

backlight_filter_t::backlight_filter_t(uint16_t max_level, uint16_t min_level)
    : max_level(max_level), min_level(min_level)
{
}

backlight_filter_t backlight_filter_t::from_precentage(uint16_t precentage)
{
    const float current_backlight = 1 - (max_level + min_level) / max_combined_level;
    const float target_backlight = precentage / 100.0f;
    const float scale_factor = (1 - target_backlight) / (1 - current_backlight);

    int new_max_level = int(max_level * scale_factor);
    int new_min_level = int(min_level * scale_factor);

    int new_sum = new_max_level + new_min_level;
    if (new_max_level > ABSOLUTE_MAX_LEVEL)
    {
        new_max_level = ABSOLUTE_MAX_LEVEL;
        new_min_level = new_sum - new_max_level;
    }
    return backlight_filter_t(new_max_level, new_min_level);
}

uint16_t backlight_filter_t::to_precentage(const backlight_filter_t &filter)
{
    return (1 - (filter.max_level + filter.min_level) / max_combined_level) * 100;
}

static constexpr const char *SENSOR_PATH = "/sys/class/video4linux/";
static constexpr const char *SENSOR_PREFIX = "v4l-subdev";
static constexpr const char *SENSOR_IMX678_NAME = "imx678";
static constexpr const char *SENSOR_IMX675_NAME = "imx675";
static constexpr const char *SENSOR_IMX715_NAME = "imx715";
static constexpr const char *SENSOR_IMX334_NAME = "imx334";
static constexpr const char *SENSOR_IMX664_NAME = "imx664";

SensorModel webserver::common::get_sensor_type()
{
    for (const auto &entry : std::filesystem::directory_iterator(SENSOR_PATH))
    {
        if (entry.path().filename().string().find(SENSOR_PREFIX) != std::string::npos)
        {
            std::ifstream name_file(entry.path() / "name");
            std::string name;
            name_file >> name;
            if (name.find(SENSOR_IMX678_NAME) == 0)
            {
                return SensorModel::SENSOR_IMX678;
            }
            if (name.find(SENSOR_IMX675_NAME) == 0)
            {
                return SensorModel::SENSOR_IMX675;
            }
            if (name.find(SENSOR_IMX715_NAME) == 0)
            {
                return SensorModel::SENSOR_IMX715;
            }
            if (name.find(SENSOR_IMX334_NAME) == 0)
            {
                return SensorModel::SENSOR_IMX334;
            }
            if (name.find(SENSOR_IMX664_NAME) == 0)
            {
                return SensorModel::SENSOR_IMX664;
            }
        }
    }
    WEBSERVER_LOG_ERROR("No supported sensor found in {}", SENSOR_PATH);
    return SensorModel::SENSOR_UNKNOWN;
}

namespace webserver::common
{
const std::unordered_map<SensorModel, std::vector<Resolution>> sensor_resolutions_for_user = {
    {SensorModel::SENSOR_IMX678, {Resolution::_HD, Resolution::_FHD, Resolution::_QHD, Resolution::_4K}},
    {SensorModel::SENSOR_IMX675, {Resolution::_HD, Resolution::_FHD, Resolution::_QHD, Resolution::_5MP}},
    {SensorModel::SENSOR_IMX715, {Resolution::_HD, Resolution::_FHD, Resolution::_QHD, Resolution::_4K}},
    {SensorModel::SENSOR_IMX334, {Resolution::_HD, Resolution::_FHD, Resolution::_QHD, Resolution::_4K}},
    {SensorModel::SENSOR_IMX664, {Resolution::_HD, Resolution::_FHD, Resolution::_QHD, Resolution::_4MP}},
    {SensorModel::SENSOR_UNKNOWN,
     {Resolution::_HD, Resolution::_FHD, Resolution::_QHD, Resolution::_5MP, Resolution::_4K, Resolution::_SD}}};
}
