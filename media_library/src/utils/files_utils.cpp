#include "files_utils.hpp"
#include "media_library_logger.hpp"
#include <fstream>
#include <sstream>
#include <string>
#define MODULE_NAME LoggerType::Default

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
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open file: {}", path);
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    if (file.fail())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to read file: {}", path);
        return std::nullopt;
    }

    return buffer.str();
}

SharedFd make_shared_fd(int fd)
{
    return SharedFd(new int(fd), [](int *fd_ptr) {
        if (fd_ptr)
        {
            if (*fd_ptr >= 0)
            {
                close(*fd_ptr);
            }
            delete fd_ptr;
        }
    });
}

} // namespace files_utils
