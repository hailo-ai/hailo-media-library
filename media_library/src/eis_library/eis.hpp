#pragma once
#include <vector>
#include <stdexcept>
#include <opencv2/opencv.hpp>
#include "media_library/eis_types.hpp"
#include "media_library/isp_utils.hpp"
#include "iir_filter.hpp"

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

class Vec3dFifoBuffer
{
  public:
    Vec3dFifoBuffer(size_t max_size) : m_max_size(max_size)
    {
    }

    void push(const cv::Vec3d &value)
    {
        if (m_buffer.size() >= m_max_size)
        {
            m_buffer.pop_front();
        }
        m_buffer.push_back(value);
    }

    size_t size() const
    {
        return m_buffer.size();
    }
    bool empty() const
    {
        return m_buffer.empty();
    }
    void clear()
    {
        m_buffer.clear();
    }

    cv::Vec3d mean() const
    {
        if (m_buffer.empty())
            return cv::Vec3d(0, 0, 0);

        cv::Vec3d sum(0, 0, 0);
        for (const auto &v : m_buffer)
            sum += v;
        return sum / static_cast<double>(m_buffer.size());
    }

    cv::Vec3d standard_deviation() const
    {
        if (m_buffer.empty())
            return cv::Vec3d(0, 0, 0);

        cv::Vec3d avg = mean();
        cv::Vec3d variance(0, 0, 0);
        for (const auto &v : m_buffer)
        {
            cv::Vec3d diff = v - avg;
            variance += diff.mul(diff);
        }
        variance /= static_cast<double>(m_buffer.size());
        return cv::Vec3d(std::sqrt(variance[0]), std::sqrt(variance[1]), std::sqrt(variance[2]));
    }

  private:
    size_t m_max_size;
    std::deque<cv::Vec3d> m_buffer;
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

enum class shakes_state_t
{
    NORMAL,
    NOISE,
    VIOLENT,
};

class EIS
{
  public:
    size_t m_frame_count;

    EIS(const std::string &config_filename, uint32_t window_size, uint32_t sample_rate, float min_angle_degrees,
        float max_angle_degrees, size_t shakes_type_buff_size, double iir_hpf_coefficient, double gyro_scale);
    ~EIS();

    cv::Mat smooth(const cv::Mat &current_orientation, double rotational_smoothing_coefficient);
    std::vector<std::pair<uint64_t, cv::Mat>> integrate_rotations_rolling_shutter(
        const std::vector<unbiased_gyro_sample_t> &frame_gyro_records);
    void remove_bias(const std::vector<gyro_sample_t> &gyro_records,
                     std::vector<unbiased_gyro_sample_t> &unbiased_records);
    bool converged();
    std::vector<cv::Mat> get_rolling_shutter_rotations(
        const std::vector<std::pair<uint64_t, cv::Mat>> &rotations_buffer, int grid_height,
        uint64_t middle_exposure_time_of_first_row, std::vector<uint64_t> frame_readout_times, float camera_fov_factor);

    bool check_periodic_reset(std::vector<cv::Mat> &rolling_shutter_rotations, uint32_t curr_fps);
    void reset_history(bool reset_hpf);
    shakes_state_t get_curr_shakes_state();
    std::vector<std::pair<uint64_t, cv::Mat>> get_orientations_based_on_shakes_state(
        std::vector<std::pair<uint64_t, cv::Mat>> current_orientations);

    /*
     * Calculate the timestamp of the middle exposure line
     * according to the sensor parameters and last XVS.
     */
    uint64_t get_middle_exposure_timestamp(uint64_t timestamp, isp_utils::isp_hdr_sensor_params_t &hdr_sensor_params,
                                           float t, uint64_t &threshold_timestamp);

  private:
    uint32_t m_sample_rate;
    gyro_calibration_config_t m_gyro_calibration_config;
    CircularBuffer<cv::Mat> previous_orientations;
    cv::Mat m_gyro_to_cam_rot_mat;
    unbiased_gyro_sample_t m_last_sample = unbiased_gyro_sample_t(0, 0, 0, 0);
    cv::Vec3d m_cur_angle = cv::Vec3d(0.0, 0.0, 0.0);
    cv::Vec3d m_prev_angle = cv::Vec3d(0.0, 0.0, 0.0);
    uint64_t m_latest_time = 0;
    Vec3dFifoBuffer m_rotation_buffer;
    float m_min_angle_deg = 0.0f;
    float m_max_angle_deg = 360.0f;
    cv::Mat last_normal_shakes_state_orientations;
    std::vector<IIRFilter> m_hpf_filters;
};
