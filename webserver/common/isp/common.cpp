#include "common.hpp"
#include <cstring>
#include <stdio.h>
#include "common/logger_macros.hpp"

#ifdef MEDIALIB_LOCAL_SERVER
void isp_utils::set_default_configuration() {}
void isp_utils::set_denoise_configuration() {}
void isp_utils::set_backlight_configuration() {}
void isp_utils::set_hdr_configuration(bool is_4k) {}
#endif

using namespace webserver::common;
using namespace isp_utils::ctrl;

std::unordered_map<v4l2_ctrl_id, v4l2ControlHelper::min_max_isp_params> v4l2ControlHelper::m_min_max_isp_params = {
    {v4l2_ctrl_id::V4L2_CTRL_SHARPNESS_DOWN, {0, 65535, v4l2_ctrl_id::V4L2_CTRL_SHARPNESS_DOWN}},
    {v4l2_ctrl_id::V4L2_CTRL_SHARPNESS_UP, {0, 30000, v4l2_ctrl_id::V4L2_CTRL_SHARPNESS_UP}},
    {v4l2_ctrl_id::V4L2_CTRL_BRIGHTNESS, {-128, 127, v4l2_ctrl_id::V4L2_CTRL_BRIGHTNESS}},
    {v4l2_ctrl_id::V4L2_CTRL_CONTRAST, {30, 199, v4l2_ctrl_id::V4L2_CTRL_CONTRAST}},
    {v4l2_ctrl_id::V4L2_CTRL_SATURATION, {0, 199, v4l2_ctrl_id::V4L2_CTRL_SATURATION}},
    {v4l2_ctrl_id::V4L2_CTRL_WDR_CONTRAST, {-1023, 1023, v4l2_ctrl_id::V4L2_CTRL_WDR_CONTRAST}},
    {v4l2_ctrl_id::V4L2_CTRL_AE_GAIN, {0, 3890 * 1024, v4l2_ctrl_id::V4L2_CTRL_AE_GAIN}},
    {v4l2_ctrl_id::V4L2_CTRL_AE_INTEGRATION_TIME, {8, 33000, v4l2_ctrl_id::V4L2_CTRL_AE_INTEGRATION_TIME}},
    {v4l2_ctrl_id::V4L2_CTRL_AE_WDR_VALUES, {0, 255, v4l2_ctrl_id::V4L2_CTRL_AE_WDR_VALUES}},
};

void webserver::common::update_3a_config(bool enabled)
{
#ifdef MEDIALIB_LOCAL_SERVER
    return;
#else
    // Read JSON file
    std::ifstream file(TRIPLE_A_CONFIG_PATH);
    if (!file.is_open())
    {
        WEBSERVER_LOG_ERROR("Failed to open JSON file");
        return;
    }

    nlohmann::json config;
    file >> config;
    file.close();

    // Change values to enable=value on objects with "classname": "Aeev1" and "classname": "ACproc"
    auto root = config["root"];

    for (auto &obj : config["root"])
    {
        if (obj["classname"] == "Aeev1" || obj["classname"] == "ACproc" || obj["classname"] == "AWdrv4")
        {
            obj["enable"] = enabled;
            obj["disable"] = false;
        }
    }

    // Write updated JSON back to file
    std::ofstream outFile(TRIPLE_A_CONFIG_PATH);
    if (!outFile.is_open())
    {
        WEBSERVER_LOG_ERROR("Failed to open output file");
        return;
    }

    outFile << config.dump();
    outFile.close();
#endif
}

void webserver::common::update_3a_config(nlohmann::json config)
{
#ifdef MEDIALIB_LOCAL_SERVER
    return;
#else
    // Write updated JSON back to file
    std::ofstream outFile(TRIPLE_A_CONFIG_PATH);
    if (!outFile.is_open())
    {
        WEBSERVER_LOG_ERROR("Failed to open output file");
        return;
    }

    outFile << config.dump();
    outFile.close();
#endif
}

nlohmann::json webserver::common::get_3a_config()
{
    nlohmann::json config;
#ifndef MEDIALIB_LOCAL_SERVER
    // Read JSON file
    std::ifstream file(TRIPLE_A_CONFIG_PATH);
    if (!file.is_open())
    {
        WEBSERVER_LOG_ERROR("Failed to open JSON file");
        return NULL;
    }

    file >> config;
    file.close();
#endif
    return config;
}

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