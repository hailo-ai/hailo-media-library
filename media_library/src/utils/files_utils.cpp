#include "files_utils.hpp"
#include <fstream>
#include <sstream>
#include <string>

namespace files_utils
{

std::optional<int> read_int_from_file(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return std::nullopt;
    }

    int value;
    file >> value;
    if (file.fail())
    {
        return std::nullopt;
    }

    return value;
}

std::optional<std::string> read_string_from_file(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    if (file.fail())
    {
        return std::nullopt;
    }

    return buffer.str();
}

} // namespace files_utils
