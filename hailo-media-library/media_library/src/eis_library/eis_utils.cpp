#include "eis_utils.hpp"

#include <bits/std_abs.h>
#include <algorithm>
#include <cmath>

namespace eis_utils
{

// Vec3dFifoBuffer implementation
Vec3dFifoBuffer::Vec3dFifoBuffer(size_t max_size) : m_max_size(max_size), m_sum(0, 0, 0), m_sum_sq(0, 0, 0)
{
}

void Vec3dFifoBuffer::push(const cv::Vec3d &value)
{
    if (m_buffer.size() >= m_max_size)
    {
        // Remove the oldest element from running sums
        const cv::Vec3d &old_value = m_buffer.front();
        m_sum -= old_value;
        m_sum_sq -= old_value.mul(old_value);
        m_buffer.pop_front();
    }
    // Add new element to running sums
    m_sum += value;
    m_sum_sq += value.mul(value);
    m_buffer.push_back(value);
}

size_t Vec3dFifoBuffer::size() const
{
    return m_buffer.size();
}

bool Vec3dFifoBuffer::empty() const
{
    return m_buffer.empty();
}

void Vec3dFifoBuffer::clear()
{
    m_buffer.clear();
    m_sum = cv::Vec3d(0, 0, 0);
    m_sum_sq = cv::Vec3d(0, 0, 0);
}

cv::Vec3d Vec3dFifoBuffer::mean() const
{
    if (m_buffer.empty())
        return cv::Vec3d(0, 0, 0);

    return m_sum / static_cast<double>(m_buffer.size());
}

cv::Vec3d Vec3dFifoBuffer::standard_deviation() const
{
    if (m_buffer.empty())
        return cv::Vec3d(0, 0, 0);

    double n = static_cast<double>(m_buffer.size());
    // Variance = E[X^2] - E[X]^2
    cv::Vec3d mean_val = m_sum / n;
    cv::Vec3d mean_sq = m_sum_sq / n;
    cv::Vec3d variance = mean_sq - mean_val.mul(mean_val);
    // Clamp to avoid negative variance due to floating point errors
    variance[0] = std::max(0.0, variance[0]);
    variance[1] = std::max(0.0, variance[1]);
    variance[2] = std::max(0.0, variance[2]);
    return cv::Vec3d(std::sqrt(variance[0]), std::sqrt(variance[1]), std::sqrt(variance[2]));
}

// Utility functions implementation
cv::Vec3d clamp_euler_angles(const cv::Vec3d &angles, const cv::Vec3d &max_angles)
{
    cv::Vec3d clamped_angles;
    clamped_angles[0] = std::clamp(angles[0], -max_angles[0], max_angles[0]);
    clamped_angles[1] = std::clamp(angles[1], -max_angles[1], max_angles[1]);
    clamped_angles[2] = std::clamp(angles[2], -max_angles[2], max_angles[2]);
    return clamped_angles;
}

cv::Mat euler_angles_to_rot_mat(const cv::Vec3d &angles)
{
    double roll = angles[0], pitch = angles[1], yaw = angles[2];

    double sin_roll = std::sin(roll), cos_roll = std::cos(roll);
    double sin_pitch = std::sin(pitch), cos_pitch = std::cos(pitch);
    double sin_yaw = std::sin(yaw), cos_yaw = std::cos(yaw);

    cv::Mat R_x = (cv::Mat_<double>(3, 3) << 1, 0, 0, 0, cos_roll, -sin_roll, 0, sin_roll, cos_roll);
    cv::Mat R_y = (cv::Mat_<double>(3, 3) << cos_pitch, 0, sin_pitch, 0, 1, 0, -sin_pitch, 0, cos_pitch);
    cv::Mat R_z = (cv::Mat_<double>(3, 3) << cos_yaw, -sin_yaw, 0, sin_yaw, cos_yaw, 0, 0, 0, 1);

    return R_z * R_y * R_x;
}

cv::Vec3d rot_mat_to_euler_angles(const cv::Mat &R)
{
    // Extract Euler angles from rotation matrix R = R_z * R_y * R_x (ZYX convention)
    // Following the inverse of euler_angles_to_rot_mat

    const double gimbal_lock_threshold = 0.9999;
    double sin_pitch = -R.at<double>(2, 0);

    double roll, pitch, yaw;

    if (std::abs(sin_pitch) >= gimbal_lock_threshold)
    {
        // Gimbal lock: pitch is close to +/- 90 degrees
        pitch = (sin_pitch > 0) ? M_PI_2 : -M_PI_2;
        yaw = 0.0; // Arbitrary choice
        roll = std::atan2(-R.at<double>(0, 1), R.at<double>(1, 1));
    }
    else
    {
        pitch = std::asin(sin_pitch);
        roll = std::atan2(R.at<double>(2, 1), R.at<double>(2, 2));
        yaw = std::atan2(R.at<double>(1, 0), R.at<double>(0, 0));
    }

    return cv::Vec3d(roll, pitch, yaw);
}
} // namespace eis_utils
