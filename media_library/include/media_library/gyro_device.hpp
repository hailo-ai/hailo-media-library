#pragma once 
#include <iio.h>
#include <vector>
#include <cstring>
#include <atomic>
#include <iostream>
#include <condition_variable>
#include <signal.h>
#include "eis_types.hpp"
#include "media_library/media_library_types.hpp"

#define FIFO_BUF_SIZE (1)
#define MAX_CHANNEL_ID (4)
#define MAX_VECTOR_SIZE (1000)

// lsm6dsr_gyro defines
#define DEFAULT_GYRO_DEVICE_NAME "lsm6dsr_gyro"
#define DEFAULT_DEVICE_ODR "833.000000" //"208.000000"
#define DEFAULT_GYRO_SCALE "0.000152716"
#define DEFAULT_GYRO_OUTPUT_PATH "/tmp/gyro_samples.txt"

typedef enum
{
    GYRO_STATUS_SUCCESS = 0,
    GYRO_STATUS_IIO_CONTEXT_FAILURE,
    GYRO_STATUS_ILLEGAL_STATE,
    GYRO_STATUS_DEVICE_INTERACTION_FAILURE,
    GYRO_STATUS_CHAN_INTERACTION_FAILURE,

    GYRO_STATUS_UNKNOWN_ERROR
} gyro_status_t;

struct iio_device_data
{
    std::string name;
    struct iio_buffer *buf;
    unsigned int nb_channels;
    unsigned int nb_attrs;
    int sample_count;
};

class GyroDevice
{
private:
    struct iio_context *m_ctx = NULL;
    struct iio_device *m_iio_dev;
    std::vector<gyro_sample_t> m_vector_samples;
    std::string m_device_freq;
    double m_gyro_scale;
    sig_atomic_t m_stopRunning;
    volatile bool m_stopRunningAck;
    struct iio_device_data m_iio_device_data;
    std::mutex m_mtx;

    gyro_status_t start();
    void shutdown();
    gyro_status_t restart();
    gyro_status_t device_attr_wr_str(const char *attr, const char *str_val);
    gyro_status_t channel_attr_wr_str(struct iio_channel *chn, const char *attr,
                                      const char *str_val);
    gyro_status_t device_cfg_set();
    void show_device_info();
    void prepare_device_data();
    gyro_status_t prepare_channel_data();
    gyro_status_t prepare_device();
    const char *device_name_get();

public:
    std::condition_variable cv;
    GyroDevice(std::string name, std::string device_freq, double gyro_scale)
    {
        m_device_freq = device_freq;
        m_gyro_scale = gyro_scale;
        m_stopRunning = 0;
        m_stopRunningAck = false;
        m_iio_device_data = {
            .name = name,
            .buf = NULL,
            .nb_channels = 0,
            .nb_attrs = 0,
            .sample_count = FIFO_BUF_SIZE * 10000,
        };
    };
    ~GyroDevice();
    gyro_status_t exists();
    gyro_status_t configure();
    gyro_status_t run();
    sig_atomic_t stopRunning()
    {
        if (m_stopRunning)
            return 0;
        m_stopRunning = 1;
        return m_stopRunning;
    }
    void dump_rec_samples(const std::string &file_path);
    bool get_stopRunningAck()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_stopRunningAck;
    }
    bool get_closest_vsync_sample(uint64_t frame_timestamp, std::vector<gyro_sample_t>::iterator &it_closest);
    std::vector<gyro_sample_t> get_gyro_samples_for_frame_vsync(std::vector<gyro_sample_t>::iterator odd_closest_sample,
                                                                uint64_t threshold_timestamp);
    std::vector<gyro_sample_t> get_gyro_samples_for_frame_isp_timestamp(uint64_t threshold_timestamp);
};