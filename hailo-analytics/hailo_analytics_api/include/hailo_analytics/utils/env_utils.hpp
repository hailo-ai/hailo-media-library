#pragma once

#include <string>

namespace hailo_analytics::utils
{

/**
 * @brief Check if an environment variable is set to a specific value.
 *
 * @param env_var_name Name of the environment variable
 * @param required_value Expected value (default: "1")
 * @return true if the variable is set to the required value
 */
bool is_env_variable_on(const std::string &env_var_name, const std::string &required_value = "1");

/**
 * @brief Get environment variable value with a default fallback.
 *
 * @param env_var_name Name of the environment variable
 * @param default_value Default value if variable is not set
 * @return The environment variable value or default
 */
std::string get_env_variable(const std::string &env_var_name, const std::string &default_value);

} // namespace hailo_analytics::utils
