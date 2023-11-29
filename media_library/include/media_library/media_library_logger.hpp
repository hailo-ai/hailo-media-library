/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
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

#include "logger_macros.hpp"

/* Minimum log level availble at compile time */
#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL (SPDLOG_LEVEL_DEBUG)
#endif

#define SPDLOG_NO_EXCEPTIONS
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <ctype.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdint.h>
#include <string.h>

#define MEDIALIB_LOGGER_LEVEL_ENV_VAR ("MEDIALIB_LOG_LEVEL")
#define MEDIALIB_LOGGER_CONSOLE_ENV_VAR ("MEDIALIB_CONSOLE_LOG_LEVEL")

class MediaLibLoggerSetup
/**
 * @brief This class is responsible for setting up the logger.
 * It will create the logger sinks and set their levels.
 * It will also set the logger level and pattern.
 * The MediaLibLoggerSetup is not accessible after the constructing, log calls
 * are done directly through the logger macros, e.g LOGGER__INFO, LOGGER__ERROR,
 * etc. It will also set the logger pattern.
 */
{
public:
  MediaLibLoggerSetup(spdlog::level::level_enum console_level,
                      spdlog::level::level_enum file_level,
                      spdlog::level::level_enum flush_level);
  ~MediaLibLoggerSetup() = default;
  MediaLibLoggerSetup(MediaLibLoggerSetup const &) = delete;
  void operator=(MediaLibLoggerSetup const &) = delete;

  std::string get_log_path(const std::string &path_env_var);
  bool should_flush_every_print(const std::string &flush_every_print_env_var);
  std::string get_main_log_path();
  std::shared_ptr<spdlog::sinks::sink>
  create_file_sink(const std::string &dir_path, const std::string &filename,
                   bool rotate);

private:
  std::string parse_log_path(const char *log_path);
  void set_levels(spdlog::level::level_enum console_level,
                  spdlog::level::level_enum file_level,
                  spdlog::level::level_enum flush_level);

  std::shared_ptr<spdlog::sinks::sink> m_console_sink;

  // The main log will be written to a centralized directory (home directory)
  // The local log will be written to the local directory or to the path the
  // user has chosen (via $MEDIALIB_LOGGER_PATH)
  std::shared_ptr<spdlog::sinks::sink> m_main_log_file_sink;
  std::shared_ptr<spdlog::sinks::sink> m_local_log_file_sink;
  std::shared_ptr<spdlog::logger> m_medialib_logger;
};
