#include "media_library_logger.hpp"
#include "gyro_device.h"
#include "arguments_parser.hpp"
#include <chrono>
#include <ctime>
#include <fstream>
#include <thread>
#include <iomanip>

#define DEVICE_CLK_TIMESTAMP "monotonic_raw"

static gyro_sample gyro_sampled;
GyroDev *gyroApi;

static ssize_t rd_sample_demux(const struct iio_channel *chn, void *sample,
                               size_t size, void *d)
{
    std::vector<gyro_sample_t> *samples_vector = (std::vector<gyro_sample> *)d;
    enum iio_chan_type iio_type;
    struct generic_val val;

    iio_type = iio_channel_get_type(chn);

    if (size == 2)
    {
        iio_channel_convert(chn, &val.val_u16, sample);
    }
    else if (size == 8)
    {
        iio_channel_convert(chn, &val.val_u64, sample);
    }
    else if (size == 1)
    {
        iio_channel_convert(chn, &val.val_u8, sample);
    }
    else if (size == 4)
    {
        iio_channel_convert(chn, &val.val_u32, sample);
    }

    if (IIO_TIMESTAMP == iio_type)
    {
        gyro_sampled.timestamp_ns = val.val_u64;

        if (samples_vector->size() == MAX_VECTOR_SIZE)
        {
            samples_vector->erase(samples_vector->begin());
        }
        samples_vector->push_back(gyro_sampled);
        memset(&gyro_sampled, 0, sizeof(gyro_sampled));
    }
    else if (IIO_ANGL_VEL == iio_type)
    {
        enum iio_modifier iio_modifier;
        iio_modifier = iio_channel_get_modifier(chn);
        switch (iio_modifier)
        {
        case IIO_MOD_X:
            gyro_sampled.vx = (int16_t)val.val_u16;
            break;
        case IIO_MOD_Y:
            gyro_sampled.vy = (int16_t)val.val_u16;
            break;
        case IIO_MOD_Z:
            gyro_sampled.vz = (int16_t)val.val_u16;
            break;
        default:
            break;
        }
    }
    return size;
}

// Function to clear samples with timestamps less than until_time_ns
void GyroDev::clear_samples(uint64_t until_time_ns)
{
    // Find the partition point where elements should no longer be removed
    auto partitionIt = std::lower_bound(
        m_vector_samples.begin(), m_vector_samples.end(), until_time_ns,
        [](const gyro_sample_t &sample, uint64_t time)
        {
            return sample.timestamp_ns <= time;
        });

    // Erase all elements up to the partition point
    m_vector_samples.erase(m_vector_samples.begin(), partitionIt);
}

void GyroDev::dump_rec_samples(const std::string &file_path)
{
    std::ofstream file(file_path, std::ios::trunc); // Open the file in trunc mode to overwrite existing content
    uint32_t idx = 0;
    if (!file.is_open())
    {
        LOGGER__ERROR("Unable to open file {}", file_path.c_str());
        return;
    }

    // Print header
    file << std::left << std::setw(16) << "idx"
         << std::left << std::setw(16) << "anglvel_x"
         << std::left << std::setw(16) << "anglvel_y"
         << std::left << std::setw(16) << "anglvel_y"
         << std::left << std::setw(16) << "timestamp_ns"
         << std::endl;

    file << std::left << std::setw(14) << "---------------"
         << std::left << std::setw(14) << " ---------------"
         << std::left << std::setw(14) << " ---------------"
         << std::left << std::setw(14) << " ---------------"
         << std::left << std::setw(14) << " ---------------" << std::endl;

    while (!m_stopRunningAck || !m_vector_samples.empty())
    {
        if (m_vector_samples.empty())
        {
            usleep(100000); // 100 msec delay
            continue;
        }

        auto last_sample_it = m_vector_samples.end();
        for (auto it = m_vector_samples.begin(); it != last_sample_it; ++it)
        {
            const auto &sample = *it;
            file << std::left << std::setw(16) << idx
                 << std::left << std::setw(16) << sample.vx
                 << std::left << std::setw(16) << sample.vy
                 << std::left << std::setw(16) << sample.vz
                 << std::left << std::setw(16) << sample.timestamp_ns
                 << std::endl;
            idx++;
        }

        m_vector_samples.erase(m_vector_samples.begin(), last_sample_it);
        usleep(100000); // 100 msec delay
    }

    LOGGER__INFO("Finished writing samples to file {}", file_path.c_str());

    file.close();
}

std::vector<gyro_sample_t>
GyroDev::get_samples(uint64_t start_time_ns, uint64_t end_time_ns,
                     bool clear_samples_until_end_time)
{
    // Find the start of the range
    auto startIt = std::lower_bound(
        m_vector_samples.begin(), m_vector_samples.end(), start_time_ns,
        [](const gyro_sample_t &sample, uint64_t time)
        {
            return sample.timestamp_ns < time;
        });

    // Find the end of the range (exclusive)
    auto endIt = std::upper_bound(startIt, m_vector_samples.end(), end_time_ns,
                                  [](uint64_t time, const gyro_sample_t &sample)
                                  {
                                      return time < sample.timestamp_ns;
                                  });

    // Copy the elements within the range
    std::vector<gyro_sample_t> samplesInRange(startIt, endIt);

    // Optionally clear samples until the end time
    if (clear_samples_until_end_time)
    {
        m_vector_samples.erase(m_vector_samples.begin(), endIt);
    }

    return samplesInRange;
}

std::vector<gyro_sample_t> GyroDev::get_samples_closest_to_timestamp(uint64_t input_timestamp)
{
    /* Find the sample with odd vx closest to the input timestamp */
    auto it_closest = m_vector_samples.end();
    for (auto it = m_vector_samples.begin(); it != m_vector_samples.end(); it++)
    {
        if (it->timestamp_ns > input_timestamp)
        {
            break;
        }

        if (it->vx % 2 != 0)
        {
            it_closest = it;
        }
    }

    std::vector<gyro_sample_t> result(m_vector_samples.begin(), it_closest);
    m_vector_samples.erase(m_vector_samples.begin(), it_closest);
    return result;
}

std::vector<gyro_sample_t> GyroDev::get_samples_until_next_odd_vx(uint64_t start_timestamp)
{
    auto first_relevant_sample = m_vector_samples.begin();
    auto last_relevant_sample = m_vector_samples.begin();

    for (auto it = m_vector_samples.begin(); it != m_vector_samples.end(); it++)
    {
        const auto &sample = *it;
        if (sample.timestamp_ns == start_timestamp)
        {
            first_relevant_sample = std::next(it);
        }
        else if ((sample.timestamp_ns > start_timestamp) && (sample.vx % 2 != 0))
        {
            last_relevant_sample = it;
            break;
        }
    }

    std::vector<gyro_sample_t> result(first_relevant_sample, std::next(last_relevant_sample));
    m_vector_samples.erase(m_vector_samples.begin(), std::next(last_relevant_sample));
    return result;
}

static void handle_sig(int sig)
{
    if (gyroApi->stopRunning())
        LOGGER__INFO("Notify process to finish...");
}

void GyroDev::enable_all_channels()
{
    unsigned int i, nb_channels = iio_device_get_channels_count(m_iio_dev);
    LOGGER__INFO("enable all channels");
    for (i = 0; i < nb_channels; i++)
        iio_channel_enable(iio_device_get_channel(m_iio_dev, i));
}

void GyroDev::disable_all_channels()
{
    unsigned int i, nb_channels = iio_device_get_channels_count(m_iio_dev);
    LOGGER__INFO("disable all channels");
    for (i = 0; i < nb_channels; i++)
        iio_channel_disable(iio_device_get_channel(m_iio_dev, i));
}

const char *GyroDev::device_name_get()
{
    return m_iio_device_data.name.c_str();
}

void GyroDev::shutdown()
{
    disable_all_channels();

    LOGGER__INFO("destroying buffer");
    if (m_iio_device_data.buf)
    {
        iio_buffer_destroy(m_iio_device_data.buf);
    }
    LOGGER__INFO("destroying ctx");
    if (m_ctx)
    {
        iio_context_destroy(m_ctx);
    }
    LOGGER__INFO("shutdown succeeded");
}

/* write device attribute: string */
int GyroDev::device_attr_wr_str(const char *attr,
                                const char *str_val)
{
    ssize_t rc;
    char *attr_str;

    LOGGER__INFO("Set attribute {} to {}", attr, str_val);
    attr_str = (char *)iio_device_find_attr(m_iio_dev, attr);
    if (!attr_str)
    {
        LOGGER__ERROR("{} attribute not found.", attr);
        return -EINVAL;
    }

    rc = iio_device_attr_write(m_iio_dev, attr, str_val);
    if (rc < 0)
    {
        LOGGER__ERROR("Failed to write attr[{}]={} to device {}, rc = {}",
                      attr, str_val, device_name_get(), rc);
        return -EIO;
    }

    return 0;
}

/* write channel attribute: string */
int GyroDev::channel_attr_wr_str(struct iio_channel *chn, const char *attr,
                                 const char *str_val)
{
    ssize_t rc;
    char *attr_str;

    attr_str = (char *)iio_channel_find_attr(chn, attr);
    if (!attr_str)
    {
        LOGGER__ERROR("{} attribute not found.", attr);
        return -EINVAL;
    }

    rc = iio_channel_attr_write(chn, attr, str_val);
    if (rc < 0)
    {
        LOGGER__ERROR("Failed to write attr[{}]={}, rc = {}", attr, str_val,
                      rc);
        return -EIO;
    }

    return 0;
}

/* applies streaming configuration through IIO */
int GyroDev::channel_attr_set(struct iio_channel *chn, const char *attr,
                              const char *str_val)
{
    if (!chn)
    {
        return -EINVAL;
    }

    return channel_attr_wr_str(chn, attr, str_val);
}

int GyroDev::device_cfg_set()
{
    int rc = 0;

    if (!m_iio_dev)
    {
        return -EINVAL;
    }
    rc = device_attr_wr_str("current_timestamp_clock", DEVICE_CLK_TIMESTAMP);
    if (rc)
    {
        return rc;
    }
    rc = device_attr_wr_str("sampling_frequency", m_device_freq.c_str());
    if (rc)
    {
        return rc;
    }

    return 0;
}

void GyroDev::show_device_info()
{
    struct iio_channel *channel;
    unsigned int nb_channels = iio_device_get_channels_count(m_iio_dev);
    unsigned int nb_attrs = iio_device_get_attrs_count(m_iio_dev);

    LOGGER__DEBUG("{} has: {} channels, {} attributes", m_iio_device_data.name.c_str(),
                  nb_channels, nb_attrs);

    for (unsigned int j = 0; j < nb_channels && j < MAX_CHANNEL_ID; j++)
    {
        channel = iio_device_get_channel(m_iio_dev, j);
        if (channel != NULL)
        {
            enum iio_chan_type chan_type = iio_channel_get_type(channel);

            LOGGER__DEBUG("{}/channel[{}]={}({}): attrs={}, ctrl={}",
                          m_iio_device_data.name.c_str(), j, iio_channel_get_id(channel), chan_type,
                          iio_channel_get_attrs_count(channel),
                          iio_channel_is_enabled(channel) ? "on" : "off");
        }
    }
}

void GyroDev::prepare_device_data()
{
    m_iio_device_data.nb_channels = iio_device_get_channels_count(m_iio_dev);
    m_iio_device_data.nb_attrs = iio_device_get_attrs_count(m_iio_dev);
    iio_device_set_data(m_iio_dev, &m_iio_device_data);

    LOGGER__DEBUG("{} has: {} channels, {} attributes", m_iio_device_data.name.c_str(),
                  m_iio_device_data.nb_channels, m_iio_device_data.nb_attrs);
}

void GyroDev::prepare_channel_data()
{
    struct iio_channel *channel;
    for (unsigned int j = 0; j < m_iio_device_data.nb_channels && j < MAX_CHANNEL_ID;
         j++)
    {
        channel = iio_device_get_channel(m_iio_dev, j);
        if (channel != NULL)
        {
            enum iio_chan_type chan_type = iio_channel_get_type(channel);

            LOGGER__DEBUG("{}/channel[{}]={}({}): attrs={}, ctrl={}",
                          m_iio_device_data.name.c_str(), j, iio_channel_get_id(channel), chan_type,
                          iio_channel_get_attrs_count(channel),
                          iio_channel_is_enabled(channel) ? "on" : "off");

            if (chan_type == IIO_ANGL_VEL)
            {
                channel_attr_set(channel, "scale", (std::stringstream() << m_gyro_scale).str().c_str());
            }
        }
    }
}

void GyroDev::prepare_device()
{
    m_iio_dev = iio_context_find_device(m_ctx, m_iio_device_data.name.c_str());
    if (!m_iio_dev)
    {
        LOGGER__ERROR("Unable to find IIO device {}", m_iio_device_data.name.c_str());
        m_iio_dev = NULL;
    }

    device_cfg_set();
    prepare_device_data();
    prepare_channel_data();
    enable_all_channels();

    m_iio_device_data.buf = iio_device_create_buffer(m_iio_dev, FIFO_BUF_SIZE, false);
    if (!m_iio_device_data.buf)
    {
        LOGGER__ERROR("Unable to create IIO buffer for device {}",
                      m_iio_device_data.name.c_str());
        m_iio_dev = NULL;
    }

    // Enable the buffer
    if (!iio_buffer_start(m_iio_device_data.buf))
    {
        LOGGER__ERROR("Unable to start IIO buffer for device {}",
                      m_iio_device_data.name.c_str());
        m_iio_dev = NULL;
    }
}

int GyroDev::run()
{
    int max_tries = 10;
    int rc;

    // Create context
    m_ctx = iio_create_local_context();
    if (!m_ctx)
    {
        LOGGER__ERROR("Unable to create IIO context");
        return EXIT_FAILURE;
    }

    rc = iio_context_set_timeout(m_ctx, 5000);
    if (rc)
    {
        LOGGER__ERROR("set timeout failed");
        iio_context_destroy(m_ctx);
        return EXIT_FAILURE;
    }

    prepare_device();
    if (!m_iio_dev)
    {
        rc = EXIT_FAILURE;
        return rc;
    }
    show_device_info();

    // Read data from the buffer
    while (!m_stopRunning)
    {
        for (int i = 0; i < max_tries && !m_stopRunning; i++)
        {
            ssize_t nbytes = iio_buffer_refill(m_iio_device_data.buf);
            if (nbytes < 0 && !m_stopRunning)
            {
                fprintf(stderr, "Error refilling buffer for device %s, rc = %zd\n", m_iio_device_data.name.c_str(), nbytes);
                break;
            }
            iio_buffer_foreach_sample(m_iio_device_data.buf, rd_sample_demux,
                                      (void *)&m_vector_samples);
        }
        usleep(1000); // 1 msec delay
    }

    shutdown();

    std::lock_guard<std::mutex> lock(m_mtx);
    m_stopRunningAck = true;
    cv.notify_all();

    return 0;
}

static void set_handler(int signal_nb, void (*handler)(int))
{
    struct sigaction sig;
    sigaction(signal_nb, NULL, &sig);
    sig.sa_handler = handler;
    sigaction(signal_nb, &sig, NULL);
}

int main(int argc, char *argv[])
{
    int argument_handling_results;
    sigset_t set, oldset;
    std::string output_path, iio_device_name, device_freq, gyro_scale;

    argument_handling_results =
        arguments_parser::handle_arguments(argc, argv, iio_device_name, output_path, device_freq, gyro_scale);
    if (argument_handling_results == -1)
    {
        return 0;
    }

    gyroApi = new GyroDev(iio_device_name, device_freq, std::stod(gyro_scale));
    set_handler(SIGINT, &handle_sig);
    set_handler(SIGTERM, &handle_sig);
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, &oldset);
    std::thread gyroThread = std::thread(&GyroDev::dump_rec_samples, gyroApi, output_path);
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);

    gyroApi->run();
    gyroThread.join();
    delete gyroApi;
    return EXIT_SUCCESS;
}
