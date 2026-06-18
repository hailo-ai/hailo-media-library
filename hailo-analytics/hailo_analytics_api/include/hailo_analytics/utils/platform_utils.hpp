#pragma once

#include <cstddef>

namespace hailo_analytics::utils
{

/**
 * @brief Enum representing the Hailo architecture variants
 */
enum class Architecture
{
    Hailo15H,
    Hailo15L,
    UNKNOWN
};

/**
 * @brief Detects the current Hailo architecture by reading from /sys/devices/soc0/machine
 *
 * @return Architecture The detected platform architecture
 *
 * @note Returns Architecture::UNKNOWN if the machine file cannot be read or
 *       the platform is not recognized
 */
Architecture get_hailo_architecture();

/**
 * @brief Probe SensorRegistry for all sensors at indices 0..num_sensors-1
 *        and check that every slot is populated.
 *
 * @param num_sensors Number of sensor slots the app expects.
 * @return true if all slots are populated, false otherwise.
 */
bool validate_all_sensors_are_present(size_t num_sensors);

} // namespace hailo_analytics::utils
