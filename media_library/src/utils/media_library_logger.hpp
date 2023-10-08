#pragma once

#include "logger_macros.hpp"

/* Minimum log level availble at compile time */
#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL (SPDLOG_LEVEL_DEBUG)
#endif

#define SPDLOG_NO_EXCEPTIONS
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/null_sink.h>

#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <memory>
#include <iomanip>
#include <sstream>
#include <iostream>


#define MEDIALIB_LOGGER_LEVEL_ENV_VAR ("MEDIALIB_LOG_LEVEL")
#define MEDIALIB_LOGGER_CONSOLE_ENV_VAR ("MEDIALIB_CONSOLE_LOG_LEVEL")

class MediaLibLoggerSetup
/**
 * @brief This class is responsible for setting up the logger.
 * It will create the logger sinks and set their levels.
 * It will also set the logger level and pattern.
 * The MediaLibLoggerSetup is not accessible after the constructing, log calls are done
 * directly through the logger macros, e.g LOGGER__INFO, LOGGER__ERROR, etc.
 * It will also set the logger pattern.
 */
{
public:
    MediaLibLoggerSetup(spdlog::level::level_enum console_level, spdlog::level::level_enum file_level, spdlog::level::level_enum flush_level);
    ~MediaLibLoggerSetup() = default;
    MediaLibLoggerSetup(MediaLibLoggerSetup const &) = delete;
    void operator=(MediaLibLoggerSetup const &) = delete;

    std::string get_log_path(const std::string &path_env_var);
    bool should_flush_every_print(const std::string &flush_every_print_env_var);
    std::string get_main_log_path();
    std::shared_ptr<spdlog::sinks::sink> create_file_sink(const std::string &dir_path, const std::string &filename, bool rotate);

private:
    std::string parse_log_path(const char *log_path);
    void set_levels(spdlog::level::level_enum console_level, spdlog::level::level_enum file_level, spdlog::level::level_enum flush_level);

    std::shared_ptr<spdlog::sinks::sink> m_console_sink;

    // The main log will be written to a centralized directory (home directory)
    // The local log will be written to the local directory or to the path the user has chosen (via $MEDIALIB_LOGGER_PATH)
    std::shared_ptr<spdlog::sinks::sink> m_main_log_file_sink;
    std::shared_ptr<spdlog::sinks::sink> m_local_log_file_sink;
    std::shared_ptr<spdlog::logger> m_medialib_logger;
};
