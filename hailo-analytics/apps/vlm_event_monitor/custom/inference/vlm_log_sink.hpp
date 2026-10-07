#pragma once

// Routes vlm_inference_manager stream-style logs to one of two backends
// depending on whether VLM_USE_ANALYTICS_LOG is defined at compile time:
//   - Defined  → hailo_analytics_logger (spdlog) — used by the VLM app.
//   - Undefined → std::cout / std::cerr          — used by the VLM console

#ifdef VLM_USE_ANALYTICS_LOG

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

#include <sstream>

#define VLM_LOG_INFO_STREAM(expr)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        std::ostringstream _vlm_log_oss;                                                                               \
        _vlm_log_oss << expr;                                                                                          \
        HAILO_ANALYTICS_LOG_INFO("{}", _vlm_log_oss.str());                                                            \
    } while (0)

#define VLM_LOG_DEBUG_STREAM(expr)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        std::ostringstream _vlm_log_oss;                                                                               \
        _vlm_log_oss << expr;                                                                                          \
        HAILO_ANALYTICS_LOG_DEBUG("{}", _vlm_log_oss.str());                                                           \
    } while (0)

#define VLM_LOG_WARN_STREAM(expr)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        std::ostringstream _vlm_log_oss;                                                                               \
        _vlm_log_oss << expr;                                                                                          \
        HAILO_ANALYTICS_LOG_WARN("{}", _vlm_log_oss.str());                                                            \
    } while (0)

#define VLM_LOG_ERROR_STREAM(expr)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        std::ostringstream _vlm_log_oss;                                                                               \
        _vlm_log_oss << expr;                                                                                          \
        HAILO_ANALYTICS_LOG_ERROR("{}", _vlm_log_oss.str());                                                           \
    } while (0)

#else // VLM_USE_ANALYTICS_LOG

#include <iostream>

#define VLM_LOG_INFO_STREAM(expr)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cout << expr << "\n";                                                                                     \
    } while (0)

#define VLM_LOG_DEBUG_STREAM(expr)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cout << expr << "\n";                                                                                     \
    } while (0)

#define VLM_LOG_WARN_STREAM(expr)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cerr << expr << "\n";                                                                                     \
    } while (0)

#define VLM_LOG_ERROR_STREAM(expr)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cerr << expr << "\n";                                                                                     \
    } while (0)

#endif // VLM_USE_ANALYTICS_LOG
