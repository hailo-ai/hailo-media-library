#pragma once

#include <string>
#include <stdexcept>

namespace hailo_analytics::utils
{

inline std::string port_from_stream_id(const std::string &id, int base_port = 5000)
{
    auto sink_pos = id.rfind("sink");
    if (sink_pos == std::string::npos)
    {
        throw std::invalid_argument("Stream ID '" + id + "' does not contain 'sink'");
    }
    size_t num_start = sink_pos + 4; // length of "sink"
    if (num_start >= id.size())
    {
        throw std::invalid_argument("Stream ID '" + id + "' has no number after 'sink'");
    }
    std::string num_str = id.substr(num_start);
    int stream_num = std::stoi(num_str);
    return std::to_string(base_port + stream_num * 2);
}

} // namespace hailo_analytics::utils
