#include "sensor_registry.hpp"
#include "media_library_logger.hpp"

#include "sensor_capabilities.hpp"

#include <filesystem>
#include <optional>
#include <fstream>
#include <regex>

#define MODULE_NAME LoggerType::Isp

SensorRegistry::SensorRegistry()
{
    initialize_resolutions();
    initialize_sensors();
}

// Singleton instance
SensorRegistry &SensorRegistry::get_instance()
{
    static SensorRegistry instance;
    return instance;
}

std::optional<SensorCapabilities> SensorRegistry::get_sensor_capabilities(SensorType sensor) const
{
    auto it = m_sensor_capabilities.find(sensor);
    if (it != m_sensor_capabilities.end())
    {
        return it->second;
    }
    return std::nullopt;
}

std::optional<SensorRegistry::SensorDeviceInfo> SensorRegistry::get_sensor_device_info(size_t sensor_index) const
{
    for (const auto &entry : std::filesystem::directory_iterator("/sys/class/video4linux/"))
    {
        if (entry.path().filename().string().find("v4l-subdev") != std::string::npos)
        {
            std::ifstream name_file(entry.path() / "name");
            std::string name;
            std::getline(name_file, name);

            for (const auto &[sensor_type, capabilities] : m_sensor_capabilities)
            {
                if (name.starts_with(capabilities.sub_dev_prefix))
                {
                    // Parse format: "imx678 0-001a" or "imx678 2-0040"
                    std::regex bus_regex(capabilities.sub_dev_prefix + "\\s+(\\d+)-(\\w+)");
                    std::smatch matches;

                    if (std::regex_search(name, matches, bus_regex))
                    {
                        int bus = std::stoi(matches[1].str());
                        std::string address = matches[2].str();

                        // Bus 0 matches sensor 0, bus != 0 matches sensor 1
                        if ((sensor_index == 0 && bus == 0) || (sensor_index == 1 && bus != 0))
                        {
                            std::string subdevice_path = "/dev/" + entry.path().filename().string();
                            return SensorDeviceInfo{sensor_type, bus, address, subdevice_path};
                        }
                    }
                }
            }
        }
    }

    return std::nullopt;
}

std::optional<SensorType> SensorRegistry::detect_sensor_type(size_t sensor_index) const
{
    auto device_info = get_sensor_device_info(sensor_index);
    if (device_info)
    {
        return device_info->sensor_type;
    }

    LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to find sensor type for index {}", sensor_index);
    return std::nullopt;
}

std::optional<std::pair<int, std::string>> SensorRegistry::get_i2c_bus_and_address(size_t sensor_index)
{
    auto device_info = get_sensor_device_info(sensor_index);
    if (device_info)
    {
        return std::pair<int, std::string>{device_info->bus, device_info->address};
    }

    return std::nullopt;
}

std::optional<std::string> SensorRegistry::get_imx_subdevice_path(size_t sensor_index) const
{
    auto device_info = get_sensor_device_info(sensor_index);
    if (device_info)
    {
        return device_info->subdevice_path;
    }

    return std::nullopt;
}

std::optional<Resolution> SensorRegistry::detect_resolution(const output_resolution_t &resolution) const
{
    for (const auto &[res, info] : m_resolution_info)
    {
        if (info.width == resolution.dimensions.destination_width &&
            info.height == resolution.dimensions.destination_height)
        {
            return res;
        }
    }

    return std::nullopt;
}

std::optional<ResolutionInfo> SensorRegistry::get_resolution_info(Resolution res) const
{
    auto it = m_resolution_info.find(res);
    if (it != m_resolution_info.end())
    {
        return it->second;
    }

    return std::nullopt;
}

std::optional<SensorModeInfo> SensorRegistry::get_sensor_mode_info(SensorType sensor, const SensorModeKey &key) const
{
    auto capabilities = get_sensor_capabilities(sensor);
    if (capabilities)
    {
        if (!is_supported(capabilities.value(), key.resolution))
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Resolution not supported for sensor {}", capabilities->sensor_name);
            return std::nullopt;
        }
        auto it = capabilities->mode_mappings.find(key);
        if (it != capabilities->mode_mappings.end())
        {
            return it->second;
        }
    }
    return std::nullopt;
}

std::optional<SensorModeInfo> SensorRegistry::get_sensor_mode_info_hdr(const output_resolution_t &input_resolution,
                                                                       hdr_dol_t hdr_mode) const
{
    // Detect sensor and resolution
    auto sensor_type = detect_sensor_type();
    if (!sensor_type)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to detect sensor type");
        return std::nullopt;
    }
    auto resolution = detect_resolution(input_resolution);
    if (!resolution)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Unsupported resolution: {}x{}",
                              input_resolution.dimensions.destination_width,
                              input_resolution.dimensions.destination_height);
        return std::nullopt;
    }

    return get_sensor_mode_info(sensor_type.value(), SensorModeKey(resolution.value(), hdr_mode));
}

std::optional<SensorModeInfo> SensorRegistry::get_sensor_mode_info_sdr(
    const output_resolution_t &input_resolution) const
{
    // Detect sensor and resolution
    auto sensor_type = detect_sensor_type();
    if (!sensor_type)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to detect sensor type");
        return std::nullopt;
    }
    auto resolution = detect_resolution(input_resolution);
    if (!resolution)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Unsupported resolution: {}x{}",
                              input_resolution.dimensions.destination_width,
                              input_resolution.dimensions.destination_height);
        return std::nullopt;
    }

    return get_sensor_mode_info(sensor_type.value(), SensorModeKey(resolution.value()));
}

bool SensorRegistry::is_supported(SensorCapabilities capabilities, Resolution resolution) const
{
    return (capabilities.supported_resolutions.find(resolution) != capabilities.supported_resolutions.end());
}

std::optional<int> SensorRegistry::get_pixel_format()
{
    auto sensor = detect_sensor_type();
    if (!sensor.has_value())
    {
        return std::nullopt;
    }

    auto capabilities = get_sensor_capabilities(sensor.value());
    if (capabilities.has_value())
    {
        return capabilities.value().pixel_format;
    }

    return std::nullopt;
}

std::optional<std::string> SensorRegistry::get_video_device_path(size_t sensor_index)
{
    if (sensor_index >= sensor_config::sensor_index_to_video_device.size())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Unsupported sensor index: {}", sensor_index);
        return std::nullopt;
    }

    return sensor_config::sensor_index_to_video_device[sensor_index];
}

std::optional<std::string> SensorRegistry::get_raw_capture_path(size_t sensor_index)
{
    if (sensor_index >= sensor_config::sensor_index_to_raw_capture.size())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Unsupported sensor index: {}", sensor_index);
        return std::nullopt;
    }
    return sensor_config::sensor_index_to_raw_capture[sensor_index];
}

void SensorRegistry::initialize_resolutions()
{
    m_resolution_info = sensor_config::all_resolution_info;
}

std::optional<std::string> SensorRegistry::get_sensor_name(SensorType sensor) const
{
    auto capabilities_opt = get_sensor_capabilities(sensor);
    if (capabilities_opt.has_value())
    {
        return capabilities_opt.value().sensor_name;
    }
    return std::nullopt;
}

void SensorRegistry::initialize_sensors()
{
    m_sensor_capabilities = sensor_config::all_sensor_capabilities;
}
