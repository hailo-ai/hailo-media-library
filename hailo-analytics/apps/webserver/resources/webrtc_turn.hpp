#pragma once

#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>
#include <string>

namespace webserver
{
namespace resources
{
namespace turn
{

// Environment variable names for TURN server configuration
constexpr const char *WEBSERVER_ENV_TURN_HOST = "WEBSERVER_TURN_HOST";
constexpr const char *WEBSERVER_ENV_TURN_PORT = "WEBSERVER_TURN_PORT";
constexpr const char *WEBSERVER_ENV_TURN_USERNAME = "WEBSERVER_TURN_USERNAME";
constexpr const char *WEBSERVER_ENV_TURN_PASSWORD = "WEBSERVER_TURN_PASSWORD";

/**
 * @brief Configuration for TURN/ICE servers
 */
struct TurnConfig
{
    std::string m_host;
    std::string m_port;
    std::string m_username;
    std::string m_password;

    /**
     * @brief Check if TURN configuration is valid
     */
    bool is_valid() const;
};

/**
 * @brief Load TURN configuration from environment variables
 *
 * Reads WEBSERVER_TURN_HOST (required), WEBSERVER_TURN_PORT (default: 3478),
 * WEBSERVER_TURN_USERNAME (default: "dev"), and WEBSERVER_TURN_PASSWORD (default: "dev")
 *
 * Default credentials are provided for development convenience only.
 *
 * @return TurnConfig structure with the loaded configuration
 */
TurnConfig load_turn_config_from_env();

/**
 * @brief Configure ICE servers in the WebRTC configuration
 *
 * @param config rtc::Configuration to populate with ICE servers
 * @param turn_config TURN configuration to use
 * @return true if TURN server was configured, false otherwise
 */
bool configure_ice_servers(rtc::Configuration &config, const TurnConfig &turn_config);

/**
 * @brief Build ICE servers JSON array for client-side WebRTC configuration
 *
 * @param turn_config TURN configuration to use
 * @return JSON array containing ICE server configuration, or empty array if no TURN configured
 */
nlohmann::json build_ice_servers_json(const TurnConfig &turn_config);

} // namespace turn
} // namespace resources
} // namespace webserver
