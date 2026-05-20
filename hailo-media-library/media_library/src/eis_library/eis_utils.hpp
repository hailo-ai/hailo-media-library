#pragma once

#include <stddef.h>
#include <opencv2/calib3d.hpp> // IWYU pragma: keep
#include <opencv2/core.hpp>
#include <deque>
#include <stdexcept>
#include <vector>

namespace eis_utils
{

template <typename T> class CircularBuffer
{
  public:
    explicit CircularBuffer(size_t size = 1) // Default size to 1
        : buffer(size), maxSize(size), head(0), tail(0), full(false)
    {
    }

    void set_capacity(size_t size)
    {
        buffer.resize(size);
        maxSize = size;
        head = 0;
        tail = 0;
        full = false;
    }

    void push(const T &item)
    {
        buffer[head] = item;
        if (full)
        {
            tail = (tail + 1) % maxSize;
        }
        head = (head + 1) % maxSize;
        full = head == tail;
    }

    bool is_empty() const
    {
        return (!full && (head == tail));
    }

    size_t size() const
    {
        if (full)
        {
            return maxSize;
        }
        if (head >= tail)
        {
            return head - tail;
        }
        else
        {
            return maxSize + head - tail;
        }
    }

    const T &operator[](size_t index) const
    {
        if (is_empty())
        {
            throw std::out_of_range("Buffer is empty");
        }
        return buffer[(tail + index) % maxSize];
    }

    auto begin() const
    {
        return CircularBufferIterator(*this, 0);
    }

    auto end() const
    {
        return CircularBufferIterator(*this, size());
    }

    void clear()
    {
        head = 0;
        tail = 0;
        full = false;
    }

  private:
    std::vector<T> buffer;
    size_t maxSize;
    size_t head;
    size_t tail;
    bool full;

    class CircularBufferIterator
    {
      public:
        CircularBufferIterator(const CircularBuffer &buf, size_t pos) : buffer(buf), position(pos)
        {
        }

        bool operator!=(const CircularBufferIterator &other) const
        {
            return position != other.position;
        }

        const T &operator*() const
        {
            return buffer[position];
        }

        CircularBufferIterator &operator++()
        {
            position = (position + 1) % buffer.maxSize;
            return *this;
        }

      private:
        const CircularBuffer &buffer;
        size_t position;
    };
};

class Vec3dFifoBuffer
{
  public:
    Vec3dFifoBuffer(size_t max_size);

    void push(const cv::Vec3d &value);
    size_t size() const;
    bool empty() const;
    void clear();
    cv::Vec3d mean() const;
    cv::Vec3d standard_deviation() const;

  private:
    size_t m_max_size;
    std::deque<cv::Vec3d> m_buffer;
    cv::Vec3d m_sum;    // Running sum of elements
    cv::Vec3d m_sum_sq; // Running sum of squared elements
};

/**
 * @brief Clamp Euler angles to within specified maximum values
 *
 * @param angles Vector containing [roll, pitch, yaw] in radians
 * @param max_angles Vector containing maximum allowed [roll, pitch, yaw] in radians
 * @return Clamped angles vector
 */
cv::Vec3d clamp_euler_angles(const cv::Vec3d &angles, const cv::Vec3d &max_angles);

/**
 * @brief Convert Euler angles (roll, pitch, yaw) to a rotation matrix
 *
 * Uses the ZYX convention: R = R_z(yaw) * R_y(pitch) * R_x(roll)
 *
 * @param angles Vector containing [roll, pitch, yaw] in radians
 * @return 3x3 rotation matrix (CV_64F)
 */
cv::Mat euler_angles_to_rot_mat(const cv::Vec3d &angles);

/**
 * @brief Convert a rotation matrix to Euler angles (roll, pitch, yaw)
 *
 * Inverse of euler_angles_to_rot_mat, extracts angles from R = R_z * R_y * R_x
 *
 * @param R 3x3 rotation matrix (CV_64F)
 * @return Vector containing [roll, pitch, yaw] in radians
 */
cv::Vec3d rot_mat_to_euler_angles(const cv::Mat &R);

} // namespace eis_utils
