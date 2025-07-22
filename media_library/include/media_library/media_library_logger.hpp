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
#pragma once

/* Minimum log level availble at compile time */
#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL (SPDLOG_LEVEL_TRACE)
#endif
#define SPDLOG_NO_EXCEPTIONS

#include "logger_macros.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <ctype.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdint.h>
#include <string.h>

#define DEFAULT_ROTATE (true)
#define DEFAULT_MAX_LOG_FILE_SIZE (1024 * 1024) // 1MB
#define DEFAULT_LOGGER_PATTERN ("[%Y-%m-%d %X.%e] [%P] [%t] [%n] [%^%l%$] [%s:%#] [%!] %v")
/* Logger format:
   [timestamp]     - Date and time with microseconds
   [PID]           - Process ID
   [TID]           - Thread ID
   [name]          - Name of the logger instance
   [log level]     - Log severity level (e.g., info, error)
   [source:line]   - Source file and line number
   [function]      - Function name
   [message]       - Log message content */

enum class LoggerType
{
    Default,
    Api,
    Resize,
    Dewarp,
    PrivacyMask,
    Encoder,
    BufferPool,
    Dis,
    Eis,
    Dsp,
    Isp,
    Denoise,
    Osd,
    Config,
    LdcMesh,
    MotionDetection,
    Snapshot,
    ThrottlingMonitor,
    Hdr,
    NamedPipe,
    AnalyticsDB
};

class LoggerManager
{
  public:
    static std::unordered_map<LoggerType, std::string> logger_names;
    static std::unordered_map<LoggerType, std::shared_ptr<spdlog::logger>> loggers;

    static std::shared_ptr<spdlog::logger> get_logger(LoggerType name)
    {
        auto it = loggers.find(name);
        assert(it != loggers.end());
        return it->second;
    }

    LoggerManager() = delete;
};

spdlog::level::level_enum get_level(const char *log_level_c_str, spdlog::level::level_enum default_level);

namespace media_lib_logger_setup
{
/**
 * @brief Sets up all loggers, according to default media library settings.
 * It will create the loggers' sinks and set their levels.
 * It will also set the loggers' levels and patterns.
 * Log calls are done directly through the logger macros, e.g LOGGER__INFO, LOGGER__ERROR,
 * LOGGER__MODULE__INFO, LOGGER__MODULE__ERROR, etc.
 */
void media_lib_logger_setup();
// Create and return a single logger
std::shared_ptr<spdlog::logger> create_logger(std::string logger_str, spdlog::level::level_enum file_level,
                                              spdlog::level::level_enum console_level, const char *file_name,
                                              const std::string &pattern = DEFAULT_LOGGER_PATTERN,
                                              bool rotate = DEFAULT_ROTATE,
                                              std::size_t max_file_size = DEFAULT_MAX_LOG_FILE_SIZE);
}; // namespace media_lib_logger_setup
