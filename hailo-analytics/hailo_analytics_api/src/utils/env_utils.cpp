#include "hailo_analytics/utils/env_utils.hpp"
#include <cstdlib>

namespace hailo_analytics::utils
{

bool is_env_variable_on(const std::string &env_var_name, const std::string &required_value)
{
    auto env_var = std::getenv(env_var_name.c_str());
    return ((nullptr != env_var) && (required_value == env_var));
}

std::string get_env_variable(const std::string &env_var_name, const std::string &default_value)
{
    auto env_var = std::getenv(env_var_name.c_str());
    return (nullptr != env_var) ? std::string(env_var) : default_value;
}

} // namespace hailo_analytics::utils
