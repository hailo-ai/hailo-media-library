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
#define SPDLOG_ACTIVE_LEVEL (SPDLOG_LEVEL_TRACE)
#endif

#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

extern std::shared_ptr<spdlog::logger> _hailo_analytics_logger;

#define HAILO_ANALYTICS_LOG_TRACE(...)                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        SPDLOG_LOGGER_TRACE(_hailo_analytics_logger, __VA_ARGS__);                                                     \
    } while (0)
#define HAILO_ANALYTICS_LOG_DEBUG(...)                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        SPDLOG_LOGGER_DEBUG(_hailo_analytics_logger, __VA_ARGS__);                                                     \
    } while (0)
#define HAILO_ANALYTICS_LOG_INFO(...)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        SPDLOG_LOGGER_INFO(_hailo_analytics_logger, __VA_ARGS__);                                                      \
    } while (0)
#define HAILO_ANALYTICS_LOG_WARN(...)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        SPDLOG_LOGGER_WARN(_hailo_analytics_logger, __VA_ARGS__);                                                      \
    } while (0)
#define HAILO_ANALYTICS_LOG_ERROR(...)                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        SPDLOG_LOGGER_ERROR(_hailo_analytics_logger, __VA_ARGS__);                                                     \
    } while (0)
#define HAILO_ANALYTICS_LOG_CRITICAL(...)                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        SPDLOG_LOGGER_CRITICAL(_hailo_analytics_logger, __VA_ARGS__);                                                  \
    } while (0)
