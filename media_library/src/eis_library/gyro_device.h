#ifndef _GYRO_DEVICE_H_
#define _GYRO_DEVICE_H_

#include <iio.h>
#include <vector>
#include <cstring>
#include <atomic>
#include <iostream>
#include <condition_variable>
#include <signal.h>
#include "eis_types.hpp"
#include "media_library_types.hpp"

#define FIFO_BUF_SIZE (1)
#define MAX_CHANNEL_ID (4)
#define MAX_VECTOR_SIZE (1000)

// lsm6dsr_gyro defines
#define DEFAULT_GYRO_DEVICE_NAME "lsm6dsr_gyro"
#define DEFAULT_DEVICE_ODR "833.000000" //"208.000000"
#define DEFAULT_GYRO_SCALE "0.000152716"


#define DEFAULT_GYRO_OUTPUT_PATH "/tmp/gyro_samples.txt"


struct iio_device_data
{
    std::string name;
    struct iio_buffer *buf;
    unsigned int nb_channels;
    unsigned int nb_attrs;
    int sample_count;
};

struct generic_val
{
    union
    {
        int8_t val_8;
        int16_t val_16;
        int32_t val_32;
        int64_t val_64;
        uint8_t val_u8;
        uint16_t val_u16;
        uint32_t val_u32;
        uint64_t val_u64;
    };
};

class GyroDev
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

    int start();
    void shutdown();
    int restart();
    void clear_samples(uint64_t until_time_ns);
    int device_attr_wr_str(const char *attr,
                           const char *str_val);
    int channel_attr_wr_str(struct iio_channel *chn, const char *attr,
                            const char *str_val);
    int channel_attr_set(struct iio_channel *chn, const char *attr, const char *str_val);
    int device_cfg_set();
    void show_device_info();
    void prepare_device_data();
    void prepare_channel_data();
    void prepare_device();
    void enable_all_channels();
    void disable_all_channels();
    const char *device_name_get();

public:
    std::condition_variable cv;
    GyroDev(std::string name, std::string device_freq, double gyro_scale)
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
    int run();
    std::vector<gyro_sample_t> get_samples(uint64_t start_time_ns, uint64_t end_time_ns, bool clear_samples_until_end_time = true);
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
    bool get_closest_vsync_sample(uint64_t frame_timestamp, std::vector<gyro_sample_t>::iterator& it_closest);
    std::vector<gyro_sample_t> get_gyro_samples_for_frame_vsync(std::vector<gyro_sample_t>::iterator odd_closest_sample,
                                                               uint64_t threshold_timestamp);
    std::vector<gyro_sample_t> get_gyro_samples_for_frame_isp_timestamp(uint64_t threshold_timestamp);                                                           
    std::vector<gyro_sample_t> get_samples_until_next_odd_vx(uint64_t start_timestamp);
};

#endif //  _GYRO_DEVICE_H_
