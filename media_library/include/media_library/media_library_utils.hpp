#pragma once

#include "media_library_types.hpp"
#include <time.h>
#include <stdint.h>

/**
 * Gets the time of timespec in milliseconds.
 * @param[in] time     The time spec.
 * @returns The time in milliseconds.
 */
static inline int64_t media_library_timespec_to_ms(const struct timespec time)
{
    return (int64_t)time.tv_sec * (int64_t)1000 + (int64_t)time.tv_nsec / (int64_t)1000000;
}

/**
 * Gets the time difference between 2 time specs in milliseconds.
 * @param[in] after     The second time spec.
 * @param[in] before    The first time spec.\
 * @returns The time differnece in milliseconds.
 */
static inline int64_t media_library_difftimespec_ms(const struct timespec after, const struct timespec before)
{
    return media_library_timespec_to_ms(after) - media_library_timespec_to_ms(before);
}

static inline int64_t media_library_get_timespec_ms()
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return media_library_timespec_to_ms(time);
}