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
#include "spdlog/cfg/env.h"
#include "spdlog/cfg/helpers-inl.h"

#include <filesystem>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

#define MEDIALIB_NAME ("hailo_media_library")
#define MEDIALIB_LOGGER_FILENAME ("medialib.log")
// if rotation is enabled but set to 0, the rotation first fully remove current file before starting to use it again
#define MEDIALIB_MAX_NUMBER_OF_LOG_FILES (1)
#define DEFAULT_FILE_LEVEL (spdlog::level::level_enum::debug)
#define DEFAULT_CONSOLE_LEVEL (spdlog::level::level_enum::err)

#define MEDIALIB_LOGGER_PATTERN ("[%Y-%m-%d %X.%e] [%P] [%t] [hailo_media_library] [%n] [%^%l%$] [%s:%#] [%!] %v")
/* Logger format:
   [timestamp]           - Date and time with microseconds
   [PID]                 - Process ID
   [TID]                 - Thread ID
   [hailo_media_library]
   [module]              - Name of logger module (hailo_media_library if not provided)
   [log level]           - Log severity level (e.g., info, error)
   [source:line]         - Source file and line number
   [function]            - Function name
   [message]             - Log message content */

#define PATH_SEPARATOR "/"

std::unordered_map<LoggerType, std::string> LoggerManager::logger_names = {
    {LoggerType::Default, MEDIALIB_NAME},
    {LoggerType::Api, "api"},
    {LoggerType::Resize, "resize"},
    {LoggerType::Dewarp, "dewarp"},
    {LoggerType::PrivacyMask, "privacy_mask"},
    {LoggerType::Encoder, "encoder"},
    {LoggerType::BufferPool, "buffer_pool"},
    {LoggerType::Dis, "dis"},
    {LoggerType::Eis, "eis"},
    {LoggerType::Dsp, "dsp"},
    {LoggerType::Isp, "isp"},
    {LoggerType::Denoise, "denoise"},
    {LoggerType::Osd, "osd"},
    {LoggerType::Config, "config"},
    {LoggerType::LdcMesh, "ldc_mesh"},
    {LoggerType::ThrottlingMonitor, "throttling_monitor"},
    {LoggerType::Snapshot, "snapshot"},
    {LoggerType::MotionDetection, "motion_detection"},
    {LoggerType::Hdr, "hdr"},
    {LoggerType::NamedPipe, "named_pipe"},
    {LoggerType::AnalyticsDB, "analytics_db"},
    {LoggerType::GstFrontendBin, "gst_frontend_bin"},
    {LoggerType::GstEncoderBin, "gst_encoder_bin"},
};

std::unordered_map<LoggerType, std::shared_ptr<spdlog::logger>> LoggerManager::loggers = {};

namespace media_lib_logger_setup
{

static std::unordered_map<std::string, spdlog::level::level_enum> get_levels_from_env(
    const char *var, const std::string &default_level_name, spdlog::level::level_enum default_level)
{
    std::unordered_map<std::string, spdlog::level::level_enum> levels = {{default_level_name, default_level}};
    auto env_val = get_env_variable<std::string>(var);

    if (!env_val.has_value())
    {
        return levels;
    }

    std::string logger_string_config = env_val.value();
    if (logger_string_config.empty())
    {
        return levels;
    }

    auto key_vals = spdlog::cfg::helpers::extract_key_vals_(logger_string_config);

    for (auto &name_level : key_vals)
    {
        auto &logger_name = name_level.first;
        auto level_name = spdlog::cfg::helpers::to_lower_(name_level.second);
        auto level = spdlog::level::from_str(level_name);
        // ignore unrecognized level names
        if (level == spdlog::level::off && level_name != "off")
        {
            continue;
        }
        if (logger_name.empty()) // no logger name indicates default level
        {
            levels[default_level_name] = level;
        }
        else
        {
            levels[logger_name] = level;
        }
    }

    return levels;
}

static std::string get_log_dir_path()
{
    std::string log_path = "";
    auto log_path_expected = get_env_variable<std::string>(MEDIALIB_LOGGER_PATH_ENV_VAR);

    if (log_path_expected.has_value())
    {
        log_path = log_path_expected.value();
    }

    if (log_path == "")
    {
        return ".";
    }

    if (log_path == "NONE")
    {
        return "";
    }

    return log_path;
}

static std::shared_ptr<spdlog::sinks::sink> create_file_sink(const std::string &dir_path, const std::string &filename,
                                                             bool rotate, std::size_t max_file_size,
                                                             spdlog::level::level_enum file_level)
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
    std::shared_ptr<spdlog::sinks::sink> file_sink;

    if (rotate)
    {
        file_sink = make_shared_nothrow<spdlog::sinks::rotating_file_sink_mt>(file_path, max_file_size,
                                                                              MEDIALIB_MAX_NUMBER_OF_LOG_FILES);
    }
    else
    {
        file_sink = make_shared_nothrow<spdlog::sinks::basic_file_sink_mt>(file_path);
    }

    if (nullptr == file_sink)
    {
        return nullptr;
    }

    file_sink->set_level(file_level);

    return file_sink;
}

static std::shared_ptr<spdlog::sinks::sink> create_console_sink(spdlog::level::level_enum console_level)
{
    auto console_sink = make_shared_nothrow<spdlog::sinks::stderr_color_sink_mt>();
    if (nullptr == console_sink)
    {
        return nullptr;
    }

    console_sink->set_level(console_level);
    return console_sink;
}

void media_lib_logger_setup()
{
    std::string default_level_name = LoggerManager::logger_names[LoggerType::Default];
    auto file_levels = get_levels_from_env(MEDIALIB_LOGGER_LEVEL_ENV_VAR, default_level_name, DEFAULT_FILE_LEVEL);
    auto console_levels =
        get_levels_from_env(MEDIALIB_LOGGER_CONSOLE_ENV_VAR, default_level_name, DEFAULT_CONSOLE_LEVEL);

    bool rotate = DEFAULT_ROTATE;
    auto rotate_env_val = get_env_variable<bool>(MEDIALIB_LOGGER_ROTATE_ENV_VAR);
    if (rotate_env_val.has_value())
    {
        rotate = rotate_env_val.value();
    }

    std::size_t max_file_size = DEFAULT_MAX_LOG_FILE_SIZE;
    auto file_size_env_val = get_env_variable<std::size_t>(MEDIALIB_LOGGER_FILE_SIZE_ENV_VAR);
    if (file_size_env_val.has_value())
    {
        max_file_size = file_size_env_val.value();
    }

    for (auto &logger_name : LoggerManager::logger_names)
    {
        auto logger_type = logger_name.first;
        auto logger_str = logger_name.second;

        // get level for file sinks
        spdlog::level::level_enum file_level = file_levels[default_level_name];
        if (file_levels.count(logger_str) != 0)
        {
            file_level = file_levels[logger_str];
        }

        // get level for console sink
        spdlog::level::level_enum console_level = console_levels[default_level_name];
        if (console_levels.count(logger_str) != 0)
        {
            console_level = console_levels[logger_str];
        }

        auto logger = create_logger(logger_str, file_level, console_level, MEDIALIB_LOGGER_FILENAME,
                                    MEDIALIB_LOGGER_PATTERN, rotate, max_file_size);
        if (logger == nullptr)
        {
            throw std::runtime_error("Failed to create logger");
        }

        LoggerManager::loggers[logger_type] = std::move(logger);
    }
}

static bool has_null_shared_ptr(const std::vector<std::shared_ptr<spdlog::sinks::sink>> &vec)
{
    // std::any_of returns true if the predicate (the lambda) is true for any element
    return std::any_of(vec.begin(), vec.end(), [](const std::shared_ptr<spdlog::sinks::sink> &ptr) {
        return ptr == nullptr; // The predicate checks if the shared_ptr is null
    });
}

std::shared_ptr<spdlog::logger> create_logger(std::string logger_str, spdlog::level::level_enum file_level,
                                              spdlog::level::level_enum console_level, const char *file_name,
                                              const std::string &pattern, bool rotate, std::size_t max_file_size)
{
    std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks;
    sinks.push_back(create_file_sink(get_log_dir_path(), file_name, rotate, max_file_size, file_level));
    sinks.push_back(create_console_sink(console_level));
    if (file_level < spdlog::level::info)
    {
        std::string file_name_for_reduced_sink = std::string("info-") + std::string(file_name);
        sinks.push_back(create_file_sink(get_log_dir_path(), file_name_for_reduced_sink, rotate, max_file_size,
                                         spdlog::level::info));
    }

    if (has_null_shared_ptr(sinks))
    {
        std::cerr << "Allocating memory on heap for logger sinks has failed! "
                     "Please check if this host has enough memory. Writing to "
                     "log will result in a SEGFAULT!"
                  << std::endl;
        return nullptr;
    }

    // create logger
    auto logger = make_shared_nothrow<spdlog::logger>(logger_str, sinks.begin(), sinks.end());
    if (nullptr == logger)
    {
        std::cerr << "Allocating memory on heap for MediaLib logger has "
                     "failed! Please check if this host has enough memory. "
                     "Writing to log will result in a SEGFAULT!"
                  << std::endl;
        return nullptr;
    }
    auto min_level = std::min(file_level, console_level);
    logger->flush_on(min_level);
    logger->set_level(min_level);
    logger->set_pattern(pattern);
    return logger;
}
} // namespace media_lib_logger_setup

spdlog::level::level_enum get_level(const char *log_level_c_str, spdlog::level::level_enum default_level)
{
    if (log_level_c_str == nullptr)
    {
        return default_level;
    }

    std::string level_str(log_level_c_str);

    auto level_name = spdlog::cfg::helpers::to_lower_(level_str);
    auto level = spdlog::level::from_str(level_name);
    // use default for unrecognized level names
    if (level == spdlog::level::off && level_name != "off")
    {
        return default_level;
    }
    return level;
}

// this is a macro that calls the function below on library load (before program starts)
COMPAT__INITIALIZER(libmedialib_initialize_logger)
{
    media_lib_logger_setup::media_lib_logger_setup();
}
