#include <nlohmann/json.hpp>
#include <memory>
#include <stdexcept>
#include <string>

#include "configs.hpp"
#include "common/logger_macros.hpp"
#include "resources/common/event_bus.hpp"
#include "resources/common/resources.hpp"

webserver::resources::ConfigResourceBase::ConfigResourceBase(std::shared_ptr<EventBus> event_bus) : Resource(event_bus)
{
}

std::string webserver::resources::ConfigResourceBase::name()
{
    return "config";
}

webserver::resources::ResourceType webserver::resources::ConfigResourceBase::get_type()
{
    return ResourceType::RESOURCE_CONFIG_MANAGER;
}

nlohmann::json webserver::resources::ConfigResourceBase::get_frontend_default_config()
{
    if (m_frontend_default_config.empty())
    {
        WEBSERVER_LOG_ERROR("Failed to get default frontend config");
        throw std::runtime_error("Failed to get default frontend config");
    }
    return m_frontend_default_config;
}

nlohmann::json webserver::resources::ConfigResourceBase::get_encoder_default_config()
{
    if (m_encoder_osd_default_config["encoding"]["encoding"].empty())
    {
        WEBSERVER_LOG_ERROR("Failed to get default encoder config");
        throw std::runtime_error("Failed to get default encoder config");
    }
    return m_encoder_osd_default_config["encoding"]["encoding"];
}

nlohmann::json webserver::resources::ConfigResourceBase::get_osd_default_config()
{
    if (m_encoder_osd_default_config["osd"]["osd"].empty())
    {
        WEBSERVER_LOG_ERROR("Failed to get default osd config");
        throw std::runtime_error("Failed to get default osd config");
    }
    return m_encoder_osd_default_config["osd"]["osd"];
}

nlohmann::json webserver::resources::ConfigResourceBase::get_osd_and_encoder_default_config()
{
    if (m_encoder_osd_default_config.empty())
    {
        WEBSERVER_LOG_ERROR("Failed to get default config");
        throw std::runtime_error("Failed to get default config");
    }
    return m_encoder_osd_default_config;
}

nlohmann::json webserver::resources::ConfigResourceBase::get_hdr_default_config()
{
    if (m_frontend_default_config["hdr"].empty())
    {
        WEBSERVER_LOG_ERROR("Failed to get default hdr config");
        throw std::runtime_error("Failed to get default hdr config");
    }
    return m_frontend_default_config["hdr"];
}

nlohmann::json webserver::resources::ConfigResourceBase::get_denoise_default_config()
{
    if (m_frontend_default_config["denoise"].empty())
    {
        WEBSERVER_LOG_ERROR("Failed to get default denoise config");
        throw std::runtime_error("Failed to get default denoise config");
    }
    return m_frontend_default_config["denoise"];
}

nlohmann::json webserver::resources::ConfigResourceBase::get_isp_default_config()
{
    if (m_frontend_default_config["iq_settings"].empty())
    {
        WEBSERVER_LOG_ERROR("Failed to get default isp config");
        throw std::runtime_error("Failed to get default isp config");
    }
    return m_frontend_default_config["isp_config_files"];
}
