#include "webrtc_turn.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>

#include "common/logger_macros.hpp"

namespace webserver
{
namespace resources
{
namespace turn
{

bool TurnConfig::is_valid() const
{
    return !m_host.empty();
}

TurnConfig load_turn_config_from_env()
{
    TurnConfig config;

    const char *host_env = std::getenv(WEBSERVER_ENV_TURN_HOST);
    const char *port_env = std::getenv(WEBSERVER_ENV_TURN_PORT);
    const char *username_env = std::getenv(WEBSERVER_ENV_TURN_USERNAME);
    const char *password_env = std::getenv(WEBSERVER_ENV_TURN_PASSWORD);

    if (host_env != nullptr && std::strlen(host_env) > 0)
    {
        config.m_host = host_env;
    }

    if (port_env != nullptr && std::strlen(port_env) > 0)
    {
        config.m_port = port_env;
    }
    else
    {
        config.m_port = "3478"; // Default TURN port
    }

    // Default credentials for development use
    if (username_env != nullptr && std::strlen(username_env) > 0)
    {
        config.m_username = username_env;
    }
    else
    {
        config.m_username = "dev"; // Development default
    }

    if (password_env != nullptr && std::strlen(password_env) > 0)
    {
        config.m_password = password_env;
    }
    else
    {
        config.m_password = "dev"; // Development default
    }

    return config;
}

bool configure_ice_servers(rtc::Configuration &config, const TurnConfig &turn_config)
{
    if (!turn_config.is_valid())
    {
        WEBSERVER_LOG_INFO("No TURN server configured ({} env var not set)", WEBSERVER_ENV_TURN_HOST);
        config.iceServers.clear();
        return false;
    }

    WEBSERVER_LOG_INFO("Registering TURN server: {}:{} using credentials (username: {})", turn_config.m_host,
                       turn_config.m_port, turn_config.m_username);

    config.iceServers.emplace_back(rtc::IceServer(turn_config.m_host, turn_config.m_port, turn_config.m_username,
                                                  turn_config.m_password, rtc::IceServer::RelayType::TurnUdp));

    return true;
}

nlohmann::json build_ice_servers_json(const TurnConfig &turn_config)
{
    if (!turn_config.is_valid())
    {
        return nlohmann::json::array();
    }

    std::string turn_url = "turn:" + turn_config.m_host + ":" + turn_config.m_port + "?transport=udp";
    nlohmann::json ice_server = {{"urls", nlohmann::json::array({turn_url})}};

    if (!turn_config.m_username.empty() && !turn_config.m_password.empty())
    {
        ice_server["username"] = turn_config.m_username;
        ice_server["credential"] = turn_config.m_password;
    }

    WEBSERVER_LOG_DEBUG("Including ICE server configuration in response: {}", turn_url);

    return nlohmann::json::array({ice_server});
}

} // namespace turn
} // namespace resources
} // namespace webserver
