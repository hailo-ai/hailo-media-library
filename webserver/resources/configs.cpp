#include "resources.hpp"

#define DEFAULT_CONFIGS_PATH "/home/root/apps/webserver/resources/configs/"
#define DEFAULT_FRONTEND_CONFIG_PATH "/home/root/apps/webserver/resources/configs/frontend_config.json"
#define DEFAULT_ENCODER_OSD_CONFIG_PATH "/home/root/apps/webserver/resources/configs/encoder_config.json"

webserver::resources::ConfigResource::ConfigResource() : Resource()
{
    // Load default frontend config
    std::ifstream defaultFrontendConfigFile(DEFAULT_FRONTEND_CONFIG_PATH);
    if (defaultFrontendConfigFile.is_open())
    {
        nlohmann::json defaultFrontendConfigJson;
        defaultFrontendConfigFile >> defaultFrontendConfigJson;
        m_frontend_default_config = defaultFrontendConfigJson;
        auto sensor_name = m_frontend_default_config["gyro"]["sensor_name"];
        auto sensor_frequency = m_frontend_default_config["gyro"]["sensor_frequency"];
        auto gyro_scale = m_frontend_default_config["gyro"]["scale"];
        auto gyro_dev = std::make_unique<GyroDevice>(sensor_name, sensor_frequency, gyro_scale);
        if (gyro_dev->exists() == GYRO_STATUS_SUCCESS)
        {
            m_frontend_default_config["gyro"]["enabled"] = true;
        }
        gyro_dev = nullptr;
        defaultFrontendConfigFile.close();
    }
    else
    {
        throw std::runtime_error("Failed to open default frontend config file");
    }

    // Load default encoder config
    std::ifstream defaultEncoderConfigFile(DEFAULT_ENCODER_OSD_CONFIG_PATH);
    if (defaultEncoderConfigFile.is_open())
    {
        nlohmann::json defaultEncoderConfigJson;
        defaultEncoderConfigFile >> defaultEncoderConfigJson;
        m_encoder_osd_default_config = defaultEncoderConfigJson;
        defaultEncoderConfigFile.close();
    }
    else
    {
        throw std::runtime_error("Failed to open default encoder config file");
    }
}

nlohmann::json webserver::resources::ConfigResource::get_frontend_default_config()
{
    if (m_frontend_default_config.empty())
    {
        throw std::runtime_error("Failed to get default frontend config");
    }
    return m_frontend_default_config;
}

nlohmann::json webserver::resources::ConfigResource::get_encoder_default_config()
{
    if (m_encoder_osd_default_config["encoding"].empty())
    {
        throw std::runtime_error("Failed to get default encoder config");
    }
    return m_encoder_osd_default_config["encoding"];
}

nlohmann::json webserver::resources::ConfigResource::get_osd_default_config()
{
    if (m_encoder_osd_default_config["osd"].empty())
    {
        throw std::runtime_error("Failed to get default osd config");
    }
    return m_encoder_osd_default_config["osd"];
}

nlohmann::json webserver::resources::ConfigResource::get_hdr_default_config()
{
    if (m_frontend_default_config["hdr"].empty())
    {
        throw std::runtime_error("Failed to get default hdr config");
    }
    return m_frontend_default_config["hdr"];
}

nlohmann::json webserver::resources::ConfigResource::get_denoise_default_config()
{
    if (m_frontend_default_config["denoise"].empty())
    {
        throw std::runtime_error("Failed to get default denoise config");
    }
    return m_frontend_default_config["denoise"];
}
