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
/**
 * @file media_library_instance_lock.hpp
 * @brief Cross-process exclusivity guard for a single MediaLibrary instance,
 *        keyed by the sensor_id it owns.
 *
 * Enforces the invariant: on a given machine, only one MediaLibrary instance
 * may exist per sensor. The dual-sensor feature is supported because each
 * sensor_id has its own independent lock file.
 *
 * Implementation: Linux advisory file lock (flock, LOCK_EX|LOCK_NB). The
 * kernel releases the lock automatically when the process exits — including
 * abnormal termination (SIGKILL, segfault) — so no explicit stale-lock
 * cleanup is needed.
 */
#pragma once

#include "media_library_logger.hpp"
#include "media_library_types.hpp"

#include <cerrno>
#include <fcntl.h>
#include <fmt/core.h>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

class MediaLibraryInstanceLock
{
  public:
    explicit MediaLibraryInstanceLock(sensor_id_t sensor_id)
        : m_sensor_id(sensor_id), m_lock_path(build_lock_path(sensor_id))
    {
    }

    MediaLibraryInstanceLock(const MediaLibraryInstanceLock &) = delete;
    MediaLibraryInstanceLock &operator=(const MediaLibraryInstanceLock &) = delete;

    MediaLibraryInstanceLock(MediaLibraryInstanceLock &&other) noexcept
        : m_sensor_id(other.m_sensor_id), m_lock_path(std::move(other.m_lock_path)), m_fd(other.m_fd),
          m_locked(other.m_locked)
    {
        other.m_fd = -1;
        other.m_locked = false;
    }

    MediaLibraryInstanceLock &operator=(MediaLibraryInstanceLock &&) = delete;

    ~MediaLibraryInstanceLock()
    {
        release();
    }

    /**
     * @brief Try to acquire the exclusive instance lock for this sensor_id.
     * @return true if acquired, false if another process already holds it.
     *
     * Safe to call once per instance. Subsequent calls return the current
     * acquisition state without re-attempting.
     */
    bool try_acquire()
    {
        if (m_locked)
        {
            return true;
        }

        m_fd = open_lock_file();
        if (m_fd < 0)
        {
            LOGGER__MODULE__ERROR(LoggerType::Api,
                                  "Failed to open media library instance lock file '{}' for sensor {}: errno={}",
                                  m_lock_path, static_cast<int>(m_sensor_id), errno);
            return false;
        }

        if (flock(m_fd, LOCK_EX | LOCK_NB) != 0)
        {
            // EWOULDBLOCK / EAGAIN → another process already holds it.
            LOGGER__MODULE__DEBUG(LoggerType::Api,
                                  "Media library instance lock for sensor {} is held by another process (errno={})",
                                  static_cast<int>(m_sensor_id), errno);
            close(m_fd);
            m_fd = -1;
            return false;
        }

        m_locked = true;
        return true;
    }

  private:
    static constexpr const char *LOCK_DIRECTORY = "/var/lock";
    static constexpr mode_t LOCK_DIRECTORY_MODE = 0755;
    static constexpr mode_t LOCK_FILE_MODE = 0644;

    sensor_id_t m_sensor_id;
    std::string m_lock_path;
    int m_fd = -1;
    bool m_locked = false;

    static std::string build_lock_path(sensor_id_t sensor_id)
    {
        return fmt::format("{}/hailo-media-library-{}.lock", LOCK_DIRECTORY, static_cast<int>(sensor_id));
    }

    int open_lock_file() const
    {
        int fd = ::open(m_lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, LOCK_FILE_MODE);
        if (fd >= 0)
        {
            return fd;
        }

        // Directory may not exist on this system. Try to create it once and retry.
        if (mkdir(LOCK_DIRECTORY, LOCK_DIRECTORY_MODE) != 0 && errno != EEXIST)
        {
            return -1;
        }
        return ::open(m_lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, LOCK_FILE_MODE);
    }

    void release()
    {
        if (m_fd >= 0)
        {
            if (m_locked)
            {
                flock(m_fd, LOCK_UN);
            }
            close(m_fd);
        }
        m_fd = -1;
        m_locked = false;
    }
};
