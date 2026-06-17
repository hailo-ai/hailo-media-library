#include "hailo_analytics/utils/platform_utils.hpp"

#include <ctype.h>
#include <algorithm>
#include <fstream> // IWYU pragma: keep
#include <iostream>
#include <string>
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "media_library/cloexec_fstream.hpp"
#include "media_library/sensor_registry.hpp"

namespace hailo_analytics::utils
{

namespace
{
const std::string MACHINE_FILE_PATH = "/sys/devices/soc0/machine";
const std::string HAILO_15_IDENTIFIER = "Hailo-15";
const std::string HAILO_15L_IDENTIFIER = "Hailo-15L";

std::string to_lower(const std::string &str)
{
    std::string lower_str = str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);
    return lower_str;
}
} // anonymous namespace

Architecture get_hailo_architecture()
{
    cloexec::ifstream file(MACHINE_FILE_PATH);
    if (!file.is_open())
    {
        std::cerr << "Failed to open machine file at " << MACHINE_FILE_PATH << std::endl;
        return Architecture::UNKNOWN;
    }

    std::string line;
    std::getline(file, line);
    file.close();

    std::string lower_line = to_lower(line);

    // Check for 15L first, since "Hailo-15L" contains "Hailo-15"
    if (lower_line.find(to_lower(HAILO_15L_IDENTIFIER)) != std::string::npos)
    {
        return Architecture::Hailo15L;
    }
    else if (lower_line.find(to_lower(HAILO_15_IDENTIFIER)) != std::string::npos)
    {
        return Architecture::Hailo15H;
    }

    return Architecture::UNKNOWN;
}

bool validate_all_sensors_are_present(size_t num_sensors)
{
    auto &registry = SensorRegistry::get_instance();
    for (size_t i = 0; i < num_sensors; ++i)
    {
        if (!registry.detect_sensor_type(i).has_value())
        {
            HAILO_ANALYTICS_LOG_WARN("Sensor {}: absent", i);
            return false;
        }
    }
    return true;
}

} // namespace hailo_analytics::utils
