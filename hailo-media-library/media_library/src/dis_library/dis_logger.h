/*
 * Copyright (c) 2017-2026 Hailo Technologies Ltd. All rights reserved.
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

#ifndef DIS_SKIP_MEDIA_LIBRARY_LOGGER
#include "media_library/media_library_logger.hpp"

// Define logger macros when media library logger is available
#define DIS_LOG_ERROR(...) LOGGER__MODULE__ERROR(LoggerType::Dis, __VA_ARGS__)
#define DIS_LOG_WARN(...) LOGGER__MODULE__WARN(LoggerType::Dis, __VA_ARGS__)
#define DIS_LOG_INFO(...) LOGGER__MODULE__INFO(LoggerType::Dis, __VA_ARGS__)
#define DIS_LOG_DEBUG(...) LOGGER__MODULE__DEBUG(LoggerType::Dis, __VA_ARGS__)

#else
#include <fmt/core.h>
#include <iostream>

// Define fmt-style macros when media library logger is not available
#define DIS_LOG_ERROR(...)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cerr << "[ERROR][DIS] " << fmt::format(__VA_ARGS__) << std::endl;                                         \
    } while (false)

#define DIS_LOG_WARN(...)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cout << "[WARN][DIS] " << fmt::format(__VA_ARGS__) << std::endl;                                          \
    } while (false)

#define DIS_LOG_INFO(...)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cout << "[INFO][DIS] " << fmt::format(__VA_ARGS__) << std::endl;                                          \
    } while (false)
#define DIS_LOG_DEBUG(...)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cout << "[DEBUG][DIS] " << fmt::format(__VA_ARGS__) << std::endl;                                         \
    } while (false)
#endif // DIS_SKIP_MEDIA_LIBRARY_LOGGER
