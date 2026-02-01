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
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h> // https://github.com/gabime/spdlog/issues/1638#issuecomment-668483705
#include "media_library_logger.hpp"

// Makes sure during compilation time that all strings in LOGGER__X macros are
// not in printf format, but in fmtlib format.
constexpr bool string_not_printf_format(char const *str)
{
    int i = 0;

    while (str[i] != '\0')
    {
        if (str[i] == '%' && ((str[i + 1] >= 'a' && str[i + 1] <= 'z') || (str[i + 1] >= 'A' && str[i + 1] <= 'Z')))
        {
            return false;
        }
        i++;
    }

    return true;
}

#define EXPAND(x) x
#define ASSERT_NOT_PRINTF_FORMAT(fmt, ...)                                                                             \
    static_assert(string_not_printf_format(fmt), "Error - Log string is in printf format and not in fmtlib format!")

#define LOGGER_TO_SPDLOG(module, level, ...)                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        EXPAND(ASSERT_NOT_PRINTF_FORMAT(__VA_ARGS__));                                                                 \
        SPDLOG_LOGGER_CALL(LoggerManager::get_logger(module), level, __VA_ARGS__);                                     \
    } while (0) // NOLINT: clang complains about this code never executing

#define LOGGER__MODULE__TRACE(module, ...) LOGGER_TO_SPDLOG(module, spdlog::level::trace, __VA_ARGS__)
#define LOGGER__TRACE(...) LOGGER__MODULE__TRACE(LoggerType::Default, __VA_ARGS__)
#define LOGGER__MODULE__DEBUG(module, ...) LOGGER_TO_SPDLOG(module, spdlog::level::debug, __VA_ARGS__)
#define LOGGER__DEBUG(...) LOGGER__MODULE__DEBUG(LoggerType::Default, __VA_ARGS__)
#define LOGGER__MODULE__INFO(module, ...) LOGGER_TO_SPDLOG(module, spdlog::level::info, __VA_ARGS__)
#define LOGGER__INFO(...) LOGGER__MODULE__INFO(LoggerType::Default, __VA_ARGS__)
#define LOGGER__MODULE__WARN(module, ...) LOGGER_TO_SPDLOG(module, spdlog::level::warn, __VA_ARGS__)
#define LOGGER__WARN(...) LOGGER__MODULE__WARN(LoggerType::Default, __VA_ARGS__)
#define LOGGER__WARNING LOGGER__WARN
#define LOGGER__MODULE__WARNING LOGGER__MODULE__WARN
#define LOGGER__MODULE__ERROR(module, ...) LOGGER_TO_SPDLOG(module, spdlog::level::err, __VA_ARGS__)
#define LOGGER__ERROR(...) LOGGER__MODULE__ERROR(LoggerType::Default, __VA_ARGS__)
#define LOGGER__MODULE__CRITICAL(module, ...) LOGGER_TO_SPDLOG(module, spdlog::level::critical, __VA_ARGS__)
#define LOGGER__CRITICAL(...) LOGGER__MODULE__CRITICAL(LoggerType::Default, __VA_ARGS__)
