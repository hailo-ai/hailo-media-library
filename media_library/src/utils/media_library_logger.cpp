/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "media_library_logger.hpp"
#include "common.hpp"
#include "env_vars.hpp"

#include <filesystem>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

#define HOME_DIR ("/home/root")

#define MAX_LOG_FILE_SIZE (1024 * 1024) // 1MB

#define MEDIALIB_NAME ("hailo_media_library")
#define MEDIALIB_LOGGER_FILENAME ("medialib.log")
#define MEDIALIB_MAX_NUMBER_OF_LOG_FILES (1) // There will be 2 log files - 1 spare
#define MEDIALIB_CONSOLE_LOGGER_PATTERN                                                                                \
    ("[%Y-%m-%d %X.%e] [%P] [%t] [%n] [%^%l%$] [%s:%#] [%!] %v") // Console
                                                                 // logger will
                                                                 // print:
                                                                 // [timestamp]
                                                                 // [PID] [TID]
                                                                 // [MediaLib]
                                                                 // [log level]
                                                                 // [source
                                                                 // file:line
                                                                 // number]
                                                                 // [function
                                                                 // name] msg
#define MEDIALIB_MAIN_FILE_LOGGER_PATTERN                                                                              \
    ("[%Y-%m-%d %X.%e] [%P] [%t] [%n] [%l] [%s:%#] [%!] %v") // File logger will
                                                             // print:
                                                             // [timestamp]
                                                             // [PID] [TID]
                                                             // [MediaLib] [log
                                                             // level] [source
                                                             // file:line
                                                             // number]
                                                             // [function name]
                                                             // msg
#define MEDIALIB_LOCAL_FILE_LOGGER_PATTERN                                                                             \
    ("[%Y-%m-%d %X.%e] [%t] [%n] [%l] [%s:%#] [%!] %v") // File logger will
                                                        // print: [timestamp]
                                                        // [TID] [MediaLib] [log
                                                        // level] [source
                                                        // file:line number]
                                                        // [function name] msg

#define PERIODIC_FLUSH_INTERVAL_IN_SECONDS (5)

#define PATH_SEPARATOR "/"

std::string MediaLibLoggerSetup::parse_log_path(const char *log_path)
{
    if ((nullptr == log_path) || (std::strlen(log_path) == 0))
    {
        return ".";
    }

    std::string log_path_str(log_path);
    if (log_path_str == "NONE")
    {
        return "";
    }

    return log_path_str;
}

std::string MediaLibLoggerSetup::get_log_path()
{
    std::string log_path_str = "";
    auto log_path_expected = get_env_variable(MEDIALIB_LOGGER_PATH_ENV_VAR);

    if (log_path_expected.has_value())
    {
        log_path_str = log_path_expected.value();
    }

    return parse_log_path(log_path_str.c_str());
}

std::string MediaLibLoggerSetup::get_main_log_path(std::string logger_name)
{
    std::string local_log_path = get_log_path();
    if (local_log_path.length() == 0)
    {
        return "";
    }

    const auto hailo_dir_path = std::string(HOME_DIR) + std::string(PATH_SEPARATOR) + std::string(".hailo");
    const auto full_path = hailo_dir_path + std::string(PATH_SEPARATOR) + logger_name;
    bool success;
    if (!std::filesystem::exists(hailo_dir_path))
    {
        success = std::filesystem::create_directory(hailo_dir_path);
        if (!success)
        {
            std::cerr << "Cannot create directory at path " << hailo_dir_path << std::endl;
            return "";
        }
    }
    if (!std::filesystem::exists(full_path))
    {
        success = std::filesystem::create_directory(full_path);
        if (!success)
        {
            std::cerr << "Cannot create directory at path " << full_path << std::endl;
            return "";
        }
    }

    return full_path;
}

std::shared_ptr<spdlog::sinks::sink> MediaLibLoggerSetup::create_file_sink(const std::string &dir_path,
                                                                           const std::string &filename, bool rotate)
{
    if ("" == dir_path)
    {
        return make_shared_nothrow<spdlog::sinks::null_sink_st>();
    }

    auto is_dir = std::filesystem::is_directory(dir_path);
    if (!is_dir)
    {
        std::cerr << "MediaLib warning: Cannot create log file " << filename << "! Path " << dir_path
                  << " is not valid." << std::endl;
        return make_shared_nothrow<spdlog::sinks::null_sink_st>();
    }

    if (!std::filesystem::exists(dir_path))
    {
        std::cerr << "MediaLib warning: Cannot create log file " << filename << "! Please check the directory "
                  << dir_path << " write permissions." << std::endl;
        return make_shared_nothrow<spdlog::sinks::null_sink_st>();
    }

    const auto file_path = dir_path + PATH_SEPARATOR + filename;

    if (rotate)
    {
        return make_shared_nothrow<spdlog::sinks::rotating_file_sink_mt>(file_path, MAX_LOG_FILE_SIZE,
                                                                         MEDIALIB_MAX_NUMBER_OF_LOG_FILES);
    }

    return make_shared_nothrow<spdlog::sinks::basic_file_sink_mt>(file_path);
}

MediaLibLoggerSetup::MediaLibLoggerSetup(spdlog::level::level_enum console_level, spdlog::level::level_enum file_level,
                                         spdlog::level::level_enum flush_level)
    : MediaLibLoggerSetup(console_level, file_level, flush_level, MEDIALIB_NAME, MEDIALIB_LOGGER_FILENAME, true)
{
}

MediaLibLoggerSetup::MediaLibLoggerSetup(spdlog::level::level_enum console_level, spdlog::level::level_enum file_level,
                                         spdlog::level::level_enum flush_level, std::string logger_name,
                                         std::string file_name, bool set_default_logger)
    : m_console_sink(make_shared_nothrow<spdlog::sinks::stderr_color_sink_mt>()),
      m_main_log_file_sink(create_file_sink(get_main_log_path(logger_name), file_name, true)),
      m_local_log_file_sink(create_file_sink(get_log_path(), file_name, true))
{
    if ((nullptr == m_console_sink) || (nullptr == m_main_log_file_sink) || (nullptr == m_local_log_file_sink))
    {
        std::cerr << "Allocating memory on heap for logger sinks has failed! "
                     "Please check if this host has enough memory. Writing to "
                     "log will result in a SEGFAULT!"
                  << std::endl;
        return;
    }

    m_main_log_file_sink->set_pattern(MEDIALIB_MAIN_FILE_LOGGER_PATTERN);
    m_local_log_file_sink->set_pattern(MEDIALIB_LOCAL_FILE_LOGGER_PATTERN);
    m_console_sink->set_pattern(MEDIALIB_CONSOLE_LOGGER_PATTERN);
    spdlog::sinks_init_list sink_list = {m_console_sink, m_main_log_file_sink, m_local_log_file_sink};
    m_medialib_logger = make_shared_nothrow<spdlog::logger>(logger_name, sink_list.begin(), sink_list.end());
    if (nullptr == m_medialib_logger)
    {
        std::cerr << "Allocating memory on heap for MediaLib logger has "
                     "failed! Please check if this host has enough memory. "
                     "Writing to log will result in a SEGFAULT!"
                  << std::endl;
        return;
    }

    set_levels(console_level, file_level, flush_level);
    if (set_default_logger)
        spdlog::set_default_logger(m_medialib_logger);
}

void MediaLibLoggerSetup::set_levels(spdlog::level::level_enum console_level, spdlog::level::level_enum file_level,
                                     spdlog::level::level_enum flush_level)
{
    m_console_sink->set_level(console_level);
    m_main_log_file_sink->set_level(file_level);
    m_local_log_file_sink->set_level(file_level);

    bool flush_every_print = is_env_variable_on(MEDIALIB_LOGGER_FLUSH_EVERY_PRINT_ENV_VAR);
    if (flush_every_print)
    {
        m_medialib_logger->flush_on(spdlog::level::debug);
        std::cerr << "MediaLib warning: Flushing log file on every print. May "
                     "reduce MediaLib performance!"
                  << std::endl;
    }
    else
    {
        m_medialib_logger->flush_on(flush_level);
    }

    auto min_level = std::min({console_level, file_level, flush_level});
    m_medialib_logger->set_level(min_level);
    spdlog::flush_every(std::chrono::seconds(PERIODIC_FLUSH_INTERVAL_IN_SECONDS));
}

spdlog::level::level_enum get_level(const char *log_level_c_str, spdlog::level::level_enum default_level)
{
    std::string log_level;
    if (log_level_c_str == nullptr)
        log_level = "";
    else
        log_level = std::string(log_level_c_str);

    if (log_level == "trace")
        return spdlog::level::level_enum::trace;
    else if (log_level == "debug")
        return spdlog::level::level_enum::debug;
    else if (log_level == "info")
        return spdlog::level::level_enum::info;
    else if (log_level == "warn")
        return spdlog::level::level_enum::warn;
    else if (log_level == "error")
        return spdlog::level::level_enum::err;
    else if (log_level == "critical")
        return spdlog::level::level_enum::critical;
    else if (log_level == "off")
        return spdlog::level::level_enum::off;
    else
        return default_level;
}

COMPAT__INITIALIZER(libmedialib_initialize_logger) // this is a macro that calls the function
                                                   // below on library load (before program
                                                   // starts)
{
    std::string log_level_file_str = "";
    auto log_level_file_expected = get_env_variable(MEDIALIB_LOGGER_LEVEL_ENV_VAR);

    if (log_level_file_expected.has_value())
    {
        log_level_file_str = log_level_file_expected.value();
    }

    std::string log_level_console_str = "";
    auto log_level_console_expected = get_env_variable(MEDIALIB_LOGGER_CONSOLE_ENV_VAR);

    if (log_level_console_expected.has_value())
    {
        log_level_console_str = log_level_console_expected.value();
    }

    auto spdlog_file_level = get_level(log_level_file_str.c_str(), spdlog::level::level_enum::info);
    auto spdlog_console_level = get_level(log_level_console_str.c_str(), spdlog::level::level_enum::err);

    MediaLibLoggerSetup(spdlog_console_level, spdlog_file_level, spdlog_file_level);
}
