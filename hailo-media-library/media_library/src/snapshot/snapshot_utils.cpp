#include "snapshot.hpp"
#include "media_library_logger.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include "env_vars.hpp"
#include "logger_macros.hpp"

#define MODULE_NAME LoggerType::Snapshot

std::string SnapshotManager::format_to_extension(HailoFormat format)
{
    switch (format)
    {
    case HAILO_FORMAT_NV12:
        return ".nv12";
    case HAILO_FORMAT_GRAY16:
        return ".raw16";
    case HAILO_FORMAT_GRAY12:
        return ".raw12";
    case HAILO_FORMAT_GRAY8:
        return ".gray8";
    case HAILO_FORMAT_RGB:
        return ".rgb";
    case HAILO_FORMAT_ARGB:
        return ".argb";
    case HAILO_FORMAT_A420:
        return ".a420";
    default:
        return ".raw";
    }
}

std::string SnapshotManager::generate_timestamp_directory()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    // Add milliseconds to the timestamp to ensure uniqueness
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream timestamp_stream;
    timestamp_stream << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d_%H-%M-%S");
    timestamp_stream << "_" << std::setfill('0') << std::setw(3) << ms.count();

    std::filesystem::path base_path = MEDIA_LIBRARY_PATH;
    const char *env_path = std::getenv(MEDIALIB_SNAPSHOT_PATH_ENV_VAR);
    if (env_path != nullptr && env_path[0] != '\0')
    {
        base_path = env_path;
    }

    std::string directory_path = (base_path / timestamp_stream.str()).string();

    std::error_code ec;
    std::filesystem::create_directories(directory_path, ec);
    if (ec)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create snapshot directory {}: {}", directory_path, ec.message());
        return "";
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot directory created: {}", directory_path);

    return directory_path;
}
