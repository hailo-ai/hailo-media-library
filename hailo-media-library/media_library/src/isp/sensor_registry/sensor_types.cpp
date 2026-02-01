#include "sensor_types.hpp"

SensorModeKey::SensorModeKey(Resolution res) : resolution(res), hdr_mode(std::nullopt)
{
}

SensorModeKey::SensorModeKey(Resolution res, std::optional<hdr_dol_t> hdr) : resolution(res), hdr_mode(hdr)
{
}

std::size_t SensorModeKey::Hash::operator()(const SensorModeKey &key) const
{
    int res = static_cast<int>(key.resolution);
    int hdr = key.hdr_mode ? static_cast<int>(*key.hdr_mode) : 0;
    return std::hash<std::string>{}(std::to_string(res) + "_" + std::to_string(hdr));
}

bool SensorModeKey::operator==(const SensorModeKey &other) const
{
    return (this->resolution == other.resolution && this->hdr_mode == other.hdr_mode);
}
