#include "files_utils.hpp"

#include <unistd.h>
#include <fstream>
#include <string>
#include <filesystem>
#include <chrono>
#include <system_error>

#include "media_library_logger.hpp"

#define MODULE_NAME LoggerType::Default

namespace fs = std::filesystem;

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

media_library_return write_string_to_file_atomic(const std::string &path, const std::string &content)
{
    std::error_code ec;

    // Create parent directory if needed
    fs::path file_path(path);
    if (file_path.has_parent_path())
    {
        fs::create_directories(file_path.parent_path(), ec);
        if (ec)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create parent directory: {}", ec.message());
            return MEDIA_LIBRARY_ERROR;
        }
    }

    // Write to temporary file
    std::string temp_path = path + ".tmp";
    std::ofstream temp_file(temp_path, std::ios::binary);
    if (!temp_file.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open temporary file for writing: {}", temp_path);
        return MEDIA_LIBRARY_ERROR;
    }

    temp_file << content;
    temp_file.close();

    if (temp_file.fail())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to write to temporary file: {}", temp_path);
        fs::remove(temp_path, ec);
        return MEDIA_LIBRARY_ERROR;
    }

    // Atomically rename temporary file to target file
    fs::rename(temp_path, path, ec);
    if (ec)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to rename temporary file to target: {}", ec.message());
        fs::remove(temp_path, ec);
        return MEDIA_LIBRARY_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

} // namespace files_utils
