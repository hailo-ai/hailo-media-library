#pragma once

#include "media_library_types.hpp"
#include <stdint.h>

/**
 * Gets the time difference between 2 time specs in milliseconds.
 * @param[in] after     The second time spec.
 * @param[in] before    The first time spec.\
 * @returns The time differnece in milliseconds.
 */
static inline int64_t media_library_difftimespec_ms(const struct timespec after, const struct timespec before)
{
    return ((int64_t)after.tv_sec - (int64_t)before.tv_sec) * (int64_t)1000 + ((int64_t)after.tv_nsec - (int64_t)before.tv_nsec) / 1000000;
}
