#include "common.hpp"
#include <cstring>
#include <stdio.h>

#ifdef MEDIALIB_LOCAL_SERVER
void isp_utils::set_default_configuration() {}
void isp_utils::set_denoise_configuration() {}
void isp_utils::set_backlight_configuration() {}
#endif

void webserver::common::update_3a_config(bool enabled)
{

#ifdef MEDIALIB_LOCAL_SERVER
    return;
#else
    // Read JSON file
    std::ifstream file(TRIPLE_A_CONFIG_PATH);
    if (!file.is_open())
    {
        std::cout << "Failed to open JSON file." << std::endl;
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
        std::cout << "Failed to open output file." << std::endl;
        return;
    }

    outFile << config.dump(); // 4 is the indentation level for pretty printing
    outFile.close();
#endif
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
}

void webserver::common::to_json(nlohmann::json &json, const webserver::common::auto_exposure_t &params)
{
    json = nlohmann::json{{"enabled", params.enabled},
                          {"gain", params.gain},
                          {"integration_time", params.integration_time}};
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