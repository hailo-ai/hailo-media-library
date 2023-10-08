#pragma once
#include <spdlog/fmt/ostr.h>

// Makes sure during compilation time that all strings in LOGGER__X macros are not in printf format, but in fmtlib format.
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
#define ASSERT_NOT_PRINTF_FORMAT(fmt, ...) static_assert(string_not_printf_format(fmt), "Error - Log string is in printf format and not in fmtlib format!")

#define LOGGER_TO_SPDLOG(level, ...)                   \
    do                                                 \
    {                                                  \
        EXPAND(ASSERT_NOT_PRINTF_FORMAT(__VA_ARGS__)); \
        level(__VA_ARGS__);                            \
    } while (0) // NOLINT: clang complains about this code never executing

#define LOGGER__TRACE(...) LOGGER_TO_SPDLOG(SPDLOG_TRACE, __VA_ARGS__)
#define LOGGER__DEBUG(...) LOGGER_TO_SPDLOG(SPDLOG_DEBUG, __VA_ARGS__)
#define LOGGER__INFO(...) LOGGER_TO_SPDLOG(SPDLOG_INFO, __VA_ARGS__)
#define LOGGER__WARN(...) LOGGER_TO_SPDLOG(SPDLOG_WARN, __VA_ARGS__)
#define LOGGER__WARNING LOGGER__WARN
#define LOGGER__ERROR(...) LOGGER_TO_SPDLOG(SPDLOG_ERROR, __VA_ARGS__)
#define LOGGER__CRITICAL(...) LOGGER_TO_SPDLOG(SPDLOG_CRITICAL, __VA_ARGS__)
