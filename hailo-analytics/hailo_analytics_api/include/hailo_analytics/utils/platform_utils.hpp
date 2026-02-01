#pragma once

#include <string>

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

} // namespace hailo_analytics::utils
