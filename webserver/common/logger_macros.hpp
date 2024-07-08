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

#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL (SPDLOG_LEVEL_DEBUG)
#endif

#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

extern std::shared_ptr<spdlog::logger> _webserver_logger;

// Makes sure during compilation time that all strings in LOGGER__X macros are
// not in printf format, but in fmtlib format.
constexpr bool string_not_printf_format(char const *str)
{
    int i = 0;

    while (str[i] != '\0')
    {
        if (str[i] == '%' && ((str[i + 1] >= 'a' && str[i + 1] <= 'z') ||
                              (str[i + 1] >= 'A' && str[i + 1] <= 'Z')))
        {
            return false;
        }
        i++;
    }

    return true;
}

#define EXPAND(x) x
#define ASSERT_NOT_PRINTF_FORMAT(fmt, ...) \
    static_assert(                         \
        string_not_printf_format(fmt),     \
        "Error - Log string is in printf format and not in fmtlib format!")

#define LOGGER_TO_SPDLOG(level, ...)                   \
    do                                                 \
    {                                                  \
        EXPAND(ASSERT_NOT_PRINTF_FORMAT(__VA_ARGS__)); \
        level(_webserver_logger, __VA_ARGS__);         \
    } while (0) // NOLINT: clang complains about this code never executing

#define WEBSERVER_LOG_TRACE(...) LOGGER_TO_SPDLOG(SPDLOG_LOGGER_TRACE, __VA_ARGS__)
#define WEBSERVER_LOG_DEBUG(...) LOGGER_TO_SPDLOG(SPDLOG_LOGGER_DEBUG, __VA_ARGS__)
#define WEBSERVER_LOG_INFO(...) LOGGER_TO_SPDLOG(SPDLOG_LOGGER_INFO, __VA_ARGS__)
#define WEBSERVER_LOG_WARN(...) LOGGER_TO_SPDLOG(SPDLOG_LOGGER_WARN, __VA_ARGS__)
#define WEBSERVER_LOG_WARNING WEBSERVER_LOG_WARN
#define WEBSERVER_LOG_ERROR(...) LOGGER_TO_SPDLOG(SPDLOG_LOGGER_ERROR, __VA_ARGS__)
#define WEBSERVER_LOG_CRITICAL(...) LOGGER_TO_SPDLOG(SPDLOG_LOGGER_CRITICAL, __VA_ARGS__)
