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
    return m_frontend_default_config;
}

nlohmann::json webserver::resources::ConfigResource::get_encoder_default_config()
{
    return m_encoder_osd_default_config["encoding"];
}

nlohmann::json webserver::resources::ConfigResource::get_osd_default_config()
{
    return m_encoder_osd_default_config["osd"];
}

nlohmann::json webserver::resources::ConfigResource::get_hdr_default_config()
{
    return m_frontend_default_config["hdr"];
}