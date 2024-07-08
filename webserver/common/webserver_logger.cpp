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
#include "media_library/media_library_logger.hpp"
#include "media_library/common.hpp"

#include <filesystem>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

#define LOGGER_NAME ("hailo_webserver")
#define LOGGER_FILENAME ("webserver.log")
#define WEBSERVER_LOGGER_LEVEL_ENV_VAR ("WEBSERVER_LOG_LEVEL")
#define WEBSERVER_LOGGER_CONSOLE_ENV_VAR ("WEBSERVER_CONSOLE_LOG_LEVEL")

std::shared_ptr<spdlog::logger> _webserver_logger;

// this is a macro that calls the function
// below on library load (before program
// starts)
COMPAT__INITIALIZER(libmedialib_initialize_logger)
{
    // Init logger
    const auto log_level_file_c_str =
        std::getenv(WEBSERVER_LOGGER_LEVEL_ENV_VAR);
    const auto log_level_console_c_str =
        std::getenv(WEBSERVER_LOGGER_CONSOLE_ENV_VAR);

    auto spdlog_file_level =
        get_level(log_level_file_c_str, spdlog::level::level_enum::info);
    auto spdlog_console_level =
        get_level(log_level_console_c_str, spdlog::level::level_enum::warn);

    auto setup = MediaLibLoggerSetup(spdlog_console_level, spdlog_file_level,
                                     spdlog_file_level, LOGGER_NAME, LOGGER_FILENAME, false);

    _webserver_logger = setup.get_logger();
}