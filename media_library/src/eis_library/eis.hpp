#pragma once
#include <vector>
#include <stdexcept>
#include <opencv2/opencv.hpp>
#include "eis_types.hpp"

/* The time after which we want to reset the EIS (10 minutes: 60 seconds * 10) */
#define EIS_RESET_TIME (60 * 10)

/* The delta after EIS_RESET_FRAMES_NUM after which we reset no matter what */
#define EIS_OPTIMAL_RESET_FRAMES_CHECK_NUM (600)

/* The threshold we consider to be "close enough" to the identity
    matrix, is used when periodically resetting EIS */
#define EIS_RESET_ANGLES_THRESHOLD (0.1 * (CV_PI / 180.0))

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

struct prev_high_pass_t
{
    double prev_gyro_x;
    double prev_gyro_y;
    double prev_gyro_z;
    double prev_smooth_x;
    double prev_smooth_y;
    double prev_smooth_z;
};

struct gyro_calibration_config_t
{
    float gbias_x;
    float gbias_y;
    float gbias_z;
    float rot_x;
    float rot_y;
    float rot_z;
};

class EIS
{
  private:
    uint32_t m_sample_rate;
    gyro_calibration_config_t m_gyro_calibration_config;
    prev_high_pass_t prev_high_pass;
    CircularBuffer<cv::Mat> previous_orientations;
    cv::Mat m_gyro_to_cam_rot_mat;
    cv::Mat m_prev_total_rotation;
    unbiased_gyro_sample_t m_last_sample = unbiased_gyro_sample_t(0, 0, 0, 0);
    cv::Vec3d m_cur_angle = cv::Vec3d(0.0, 0.0, 0.0);
    cv::Vec3d m_prev_angle = cv::Vec3d(0.0, 0.0, 0.0);

  public:
    size_t m_frame_count;

    EIS(const std::string &config_filename, uint32_t window_size, uint32_t sample_rate);
    ~EIS() {};

    cv::Mat smooth(const cv::Mat &current_orientation, double rotational_smoothing_coefficient);
    cv::Mat integrate_rotations(uint64_t last_threshold_timestamp, uint64_t curr_threshold_timestamp,
                                const std::vector<unbiased_gyro_sample_t> &frame_gyro_records);
    std::vector<std::pair<uint64_t, cv::Mat>> integrate_rotations_rolling_shutter(
        const std::vector<unbiased_gyro_sample_t> &frame_gyro_records);
    void remove_bias(const std::vector<gyro_sample_t> &gyro_records,
                     std::vector<unbiased_gyro_sample_t> &unbiased_records, double gyro_scale,
                     double iir_hpf_coefficient);

    std::vector<cv::Mat> get_rolling_shutter_rotations(
        const std::vector<std::pair<uint64_t, cv::Mat>> &rotations_buffer, int grid_height,
        uint64_t middle_exposure_time_of_first_row, uint64_t frame_readout_time);

    bool check_periodic_reset(std::vector<cv::Mat> &rolling_shutter_rotations, uint32_t curr_fps);
    void reset_history();
};
