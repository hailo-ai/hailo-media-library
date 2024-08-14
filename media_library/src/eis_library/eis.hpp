#pragma once
#include <vector>
#include <stdexcept>
#include <opencv2/opencv.hpp>
#include "eis_types.hpp"

template <typename T>
class CircularBuffer {
public:
    explicit CircularBuffer(size_t size = 1) // Default size to 1
        : buffer(size), maxSize(size), head(0), tail(0), full(false) {}

    void set_capacity(size_t size) {
        buffer.resize(size);
        maxSize = size;
        head = 0;
        tail = 0;
        full = false;
    }

    void push(const T& item) {
        buffer[head] = item;
        if (full) {
            tail = (tail + 1) % maxSize;
        }
        head = (head + 1) % maxSize;
        full = head == tail;
    }

    bool is_empty() const {
        return (!full && (head == tail));
    }

    size_t size() const {
        if (full) {
            return maxSize;
        }
        if (head >= tail) {
            return head - tail;
        } else {
            return maxSize + head - tail;
        }
    }

    const T& operator[](size_t index) const {
        if (is_empty()) {
            throw std::out_of_range("Buffer is empty");
        }
        return buffer[(tail + index) % maxSize];
    }

    auto begin() const {
        return CircularBufferIterator(*this, 0);
    }

    auto end() const {
        return CircularBufferIterator(*this, size());
    }

    void clear() {
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

    class CircularBufferIterator {
    public:
        CircularBufferIterator(const CircularBuffer& buf, size_t pos)
            : buffer(buf), position(pos) {}

        bool operator!=(const CircularBufferIterator& other) const {
            return position != other.position;
        }

        const T& operator*() const {
            return buffer[position];
        }

        CircularBufferIterator& operator++() {
            position = (position + 1) % buffer.maxSize;
            return *this;
        }

    private:
        const CircularBuffer& buffer;
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
    gyro_calibration_config_t m_gyro_calibration_config;
    prev_high_pass_t prev_high_pass;    
    CircularBuffer<cv::Mat> previous_orientations;
    cv::Mat prev_gyro_orientation;

public:
    EIS(const std::string &config_filename, uint32_t window_size);
    ~EIS() {};

    cv::Mat smooth(const cv::Mat& current_orientation, double rotational_smoothing_coefficient);
    cv::Mat integrate_rotations(uint64_t last_frame_gyro_ts,
                                const std::vector<unbiased_gyro_sample_t>& frame_gyro_records);
    void remove_bias(const std::vector<gyro_sample_t>& gyro_records,
                     std::vector<unbiased_gyro_sample_t>& unbiased_records,
                     double gyro_scale,
                     double iir_hpf_coefficient);
    void reset();
};