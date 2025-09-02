#include "logger_macros.hpp"
#include "media_library_logger.hpp"
#include "gyro_device.hpp"
#include "arguments_parser.hpp"
#include <chrono>
#include <ctime>
#include <fstream>
#include <thread>
#include <iomanip>
#include "media_library/v4l2_ctrl.hpp"
#include "media_library/isp_utils.hpp"
#include <limits>

#define MODULE_NAME LoggerType::Eis
#define DEVICE_CLK_TIMESTAMP "monotonic_raw"

#define IIO_CTX_TIMEOUT_MS (100)
#define GYRO_USLEEP_BETWEEN_ITERATIONS (500)
#define GYRO_SAMPLE_MAX_VALUE (std::numeric_limits<decltype(gyro_sample_t::vx)>::max())
#define GYRO_SATURATION_THRESHOLD (GYRO_SAMPLE_MAX_VALUE * 0.99f)
#define GYRO_SATURATION_WAIT_FRAMES (15)

static gyro_sample gyro_sampled;
std::unique_ptr<GyroDevice> gyroApi = nullptr;

static ssize_t rd_sample_demux(const struct iio_channel *chn, void *sample, size_t size, void *d)
{
    std::vector<gyro_sample_t> *samples_vector = (std::vector<gyro_sample> *)d;
    enum iio_chan_type iio_type = iio_channel_get_type(chn);
    union {
        uint16_t val_u16;
        uint64_t val_u64;
        uint8_t val_u8;
        uint32_t val_u32;
    } val;

    switch (size)
    {
    case 1:
        iio_channel_convert(chn, &val.val_u8, sample);
        break;
    case 2:
        iio_channel_convert(chn, &val.val_u16, sample);
        break;
    case 4:
        iio_channel_convert(chn, &val.val_u32, sample);
        break;
    case 8:
        iio_channel_convert(chn, &val.val_u64, sample);
        break;
    default:
        return 0;
    }

    if (iio_type == IIO_TIMESTAMP)
    {
        gyro_sampled.timestamp_ns = val.val_u64;
        if (samples_vector->size() == MAX_VECTOR_SIZE)
        {
            samples_vector->erase(samples_vector->begin());
        }

        samples_vector->emplace_back(gyro_sampled);
        memset(&gyro_sampled, 0, sizeof(gyro_sampled));
    }
    else if (iio_type == IIO_ANGL_VEL)
    {
        enum iio_modifier iio_modifier = iio_channel_get_modifier(chn);
        switch (iio_modifier)
        {
        case IIO_MOD_X:
            gyro_sampled.vx = static_cast<int16_t>(val.val_u16);
            break;
        case IIO_MOD_Y:
            gyro_sampled.vy = static_cast<int16_t>(val.val_u16);
            break;
        case IIO_MOD_Z:
            gyro_sampled.vz = static_cast<int16_t>(val.val_u16);
            break;
        default:
            break;
        }
    }
    return size;
}

// Destructor
GyroDevice::~GyroDevice()
{
    shutdown();
}

void GyroDevice::dump_rec_samples(const std::string &file_path)
{
    std::ofstream file(file_path, std::ios::trunc); // Open the file in trunc mode to overwrite existing content
    uint32_t idx = 0;
    if (!file.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Unable to open file {}", file_path.c_str());
        return;
    }

    // Print header
    file << std::left << std::setw(16) << "idx" << std::left << std::setw(16) << "anglvel_x" << std::left
         << std::setw(16) << "anglvel_y" << std::left << std::setw(16) << "anglvel_y" << std::left << std::setw(16)
         << "timestamp_ns" << std::endl;

    file << std::left << std::setw(14) << "---------------" << std::left << std::setw(14) << " ---------------"
         << std::left << std::setw(14) << " ---------------" << std::left << std::setw(14) << " ---------------"
         << std::left << std::setw(14) << " ---------------" << std::endl;

    while (!m_stopRunningAck || !m_vector_samples->empty())
    {
        if (m_vector_samples->empty())
        {
            usleep(100000); // 100 msec delay
            continue;
        }

        for (auto item = m_vector_samples->dequeue(); item.has_value(); item = m_vector_samples->dequeue())
        {
            const auto &sample = item.value();
            file << std::left << std::setw(16) << idx << std::left << std::setw(16) << sample.vx << std::left
                 << std::setw(16) << sample.vy << std::left << std::setw(16) << sample.vz << std::left << std::setw(16)
                 << sample.timestamp_ns << std::endl;
            idx++;
        }
        usleep(100000); // 100 msec delay
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Finished writing samples to file {}", file_path.c_str());

    file.close();
}

std::optional<gyro_sample_t> GyroDevice::get_closest_vsync_sample(uint64_t frame_timestamp)
{
    return m_vector_samples->find_last([frame_timestamp](const gyro_sample_t &sample) {
        return sample.vx % 2 != 0 && sample.timestamp_ns <= frame_timestamp;
    });
}

tl::expected<std::vector<gyro_sample_t>, gyro_status_t> GyroDevice::get_gyro_samples_by_threshold(
    uint64_t threshold_timestamp)
{
    auto samples = m_vector_samples->dequeue_many(
        [&threshold_timestamp](const gyro_sample_t &sample) { return sample.timestamp_ns <= threshold_timestamp; });

    if (!samples.empty() && std::any_of(samples.begin(), samples.end(), [](const gyro_sample_t &sample) {
            return std::abs(sample.vx) > GYRO_SATURATION_THRESHOLD || std::abs(sample.vy) > GYRO_SATURATION_THRESHOLD ||
                   std::abs(sample.vz) > GYRO_SATURATION_THRESHOLD;
        }))
    {
        // found a saturated sample, go into saturation state
        m_gyro_saturated_count = GYRO_SATURATION_WAIT_FRAMES;
        LOGGER__MODULE__WARNING(MODULE_NAME, "Gyro is saturated, samples will not be retrieved by threshold.");
        return tl::make_unexpected(GYRO_STATUS_SATURATED);
    }
    else if (m_gyro_saturated_count > 0)
    {
        --m_gyro_saturated_count;
        LOGGER__MODULE__WARNING(MODULE_NAME, "Gyro is saturated, cannot retrieve samples by threshold.");
        return tl::make_unexpected(GYRO_STATUS_SATURATED);
    }
    return samples;
}

static void handle_sig(int)
{
    if (gyroApi->stopRunning())
        LOGGER__MODULE__INFO(MODULE_NAME, "Notify process to finish...");
}

static void enable_all_channels(struct iio_device *iio_dev)
{
    unsigned int i, nb_channels = iio_device_get_channels_count(iio_dev);
    LOGGER__MODULE__INFO(MODULE_NAME, "Enable all Gyro channels");
    for (i = 0; i < nb_channels; i++)
        iio_channel_enable(iio_device_get_channel(iio_dev, i));
}

static void disable_all_channels(struct iio_device *iio_dev)
{
    if (!iio_dev)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Received uninitialized Gyro device!");
        return;
    }
    unsigned int i, nb_channels = iio_device_get_channels_count(iio_dev);
    LOGGER__MODULE__INFO(MODULE_NAME, "Disabling all Gyro channels");
    for (i = 0; i < nb_channels; i++)
        iio_channel_disable(iio_device_get_channel(iio_dev, i));
}

const char *GyroDevice::device_name_get()
{
    return m_iio_device_data.name.c_str();
}

void GyroDevice::shutdown()
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Gyro shutdown started...");
    LOGGER__MODULE__INFO(MODULE_NAME, "Destroying buffer");
    if (m_iio_device_data.buf)
    {
        iio_buffer_destroy(m_iio_device_data.buf);
        m_iio_device_data.buf = nullptr;
    }
    if (m_iio_dev)
    {
        disable_all_channels(m_iio_dev);
        m_iio_dev = nullptr;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Destroying ctx");
    if (m_ctx)
    {
        iio_context_destroy(m_ctx);
        m_ctx = nullptr;
    }
    LOGGER__MODULE__INFO(MODULE_NAME, "Gyro shutdown succeeded!");
}

gyro_status_t GyroDevice::exists()
{
    bool previous_context = m_ctx ? true : false;
    if (!previous_context)
    {
        m_ctx = iio_create_local_context();
        if (!m_ctx)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Unable to create IIO context");
            return GYRO_STATUS_IIO_CONTEXT_FAILURE;
        }
    }

    m_iio_dev = iio_context_find_device(m_ctx, m_iio_device_data.name.c_str());
    if (!m_iio_dev)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Device {} not found", m_iio_device_data.name.c_str());
        return GYRO_STATUS_DEVICE_INTERACTION_FAILURE;
    }
    m_iio_dev = NULL;
    if (!previous_context)
    {
        iio_context_destroy(m_ctx);
        m_ctx = NULL;
    }
    return GYRO_STATUS_SUCCESS;
}

gyro_status_t GyroDevice::start()
{
    int rc;

    m_ctx = iio_create_local_context();
    if (!m_ctx)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Unable to create IIO context");
        return GYRO_STATUS_IIO_CONTEXT_FAILURE;
    }

    rc = iio_context_set_timeout(m_ctx, IIO_CTX_TIMEOUT_MS);
    if (rc)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "set timeout failed, err: {}", rc);
        iio_context_destroy(m_ctx);
        return GYRO_STATUS_IIO_CONTEXT_FAILURE;
    }

    gyro_status_t status = prepare_device();
    if (status != GYRO_STATUS_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to prepare device, err: {}", status);
        iio_context_destroy(m_ctx);
        return status;
    }

    return GYRO_STATUS_SUCCESS;
}

gyro_status_t GyroDevice::restart()
{
    shutdown();
    return start();
}

gyro_status_t GyroDevice::device_attr_wr_str(const char *attr, const char *str_val)
{
    if (iio_device_find_attr(m_iio_dev, attr) == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Attribute '{}' not found on device.", attr);
        return GYRO_STATUS_DEVICE_INTERACTION_FAILURE;
    }

    ssize_t rc = iio_device_attr_write(m_iio_dev, attr, str_val);
    if (rc < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to write attribute '{}={}' to device '{}', error code: {}", attr,
                              str_val, device_name_get(), rc);
        return GYRO_STATUS_DEVICE_INTERACTION_FAILURE;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Successfully set attribute '{}' to '{}'.", attr, str_val);
    return GYRO_STATUS_SUCCESS;
}

gyro_status_t GyroDevice::device_buffer_attr_wr_str(const char *attr, const char *str_val)
{
    ssize_t rc = iio_device_buffer_attr_write(m_iio_dev, attr, str_val);
    if (rc < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to write buffer attribute '{}={}' to device '{}', error code: {}",
                              attr, str_val, device_name_get(), rc);
        return GYRO_STATUS_DEVICE_INTERACTION_FAILURE;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Successfully set buffer attribute '{}' to '{}'.", attr, str_val);
    return GYRO_STATUS_SUCCESS;
}

gyro_status_t GyroDevice::channel_attr_wr_str(struct iio_channel *chn, const char *attr, const char *str_val)
{
    if (iio_channel_find_attr(chn, attr) == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Attribute '{}' not found on channel.", attr);
        return GYRO_STATUS_CHAN_INTERACTION_FAILURE;
    }

    ssize_t rc = iio_channel_attr_write(chn, attr, str_val);
    if (rc < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to write attr[{}]={}, rc = {}", attr, str_val, rc);
        return GYRO_STATUS_CHAN_INTERACTION_FAILURE;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Successfully set attribute '{}' to '{}'.", attr, str_val);
    return GYRO_STATUS_SUCCESS;
}

gyro_status_t GyroDevice::device_cfg_set()
{
    if (!m_iio_dev)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Received uninitialized Gyro device!");
        return GYRO_STATUS_ILLEGAL_STATE;
    }

    auto set_device_buffer_attr = [this](const char *attr, const char *value) -> int {
        gyro_status_t status = device_buffer_attr_wr_str(attr, value);
        if (status != GYRO_STATUS_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set {}={} for Gyro buffer device, error code: {}", attr,
                                  value, status);
        }
        return status;
    };

    auto set_device_attr = [this](const char *attr, const char *value) -> int {
        gyro_status_t status = device_attr_wr_str(attr, value);
        if (status != GYRO_STATUS_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set {}={} for Gyro device, error code: {}", attr, value,
                                  status);
        }
        return status;
    };

    if (set_device_buffer_attr("enable", "0") || set_device_attr("current_timestamp_clock", DEVICE_CLK_TIMESTAMP) ||
        set_device_attr("sampling_frequency", m_device_freq.c_str()))
    {
        return GYRO_STATUS_DEVICE_INTERACTION_FAILURE;
    }

    return GYRO_STATUS_SUCCESS;
}

void GyroDevice::show_device_info()
{
    struct iio_channel *channel;
    unsigned int nb_channels = iio_device_get_channels_count(m_iio_dev);
    unsigned int nb_attrs = iio_device_get_attrs_count(m_iio_dev);

    LOGGER__MODULE__DEBUG(MODULE_NAME, "{} has: {} channels, {} attributes", m_iio_device_data.name.c_str(),
                          nb_channels, nb_attrs);

    for (unsigned int j = 0; j < nb_channels && j < MAX_CHANNEL_ID; j++)
    {
        channel = iio_device_get_channel(m_iio_dev, j);
        if (channel != NULL)
        {
            enum iio_chan_type chan_type = iio_channel_get_type(channel);

            LOGGER__MODULE__DEBUG(MODULE_NAME, "{}/channel[{}]={}({}): attrs={}, ctrl={}",
                                  m_iio_device_data.name.c_str(), j, iio_channel_get_id(channel), chan_type,
                                  iio_channel_get_attrs_count(channel), iio_channel_is_enabled(channel) ? "on" : "off");
        }
    }
}

void GyroDevice::prepare_device_data()
{
    m_iio_device_data.nb_channels = iio_device_get_channels_count(m_iio_dev);
    m_iio_device_data.nb_attrs = iio_device_get_attrs_count(m_iio_dev);
    iio_device_set_data(m_iio_dev, &m_iio_device_data);

    LOGGER__MODULE__DEBUG(MODULE_NAME, "{} has: {} channels, {} attributes", m_iio_device_data.name.c_str(),
                          m_iio_device_data.nb_channels, m_iio_device_data.nb_attrs);
}

gyro_status_t GyroDevice::prepare_channel_data()
{
    struct iio_channel *channel;
    for (unsigned int j = 0; j < m_iio_device_data.nb_channels && j < MAX_CHANNEL_ID; j++)
    {
        channel = iio_device_get_channel(m_iio_dev, j);
        if (channel != NULL)
        {
            enum iio_chan_type chan_type = iio_channel_get_type(channel);

            LOGGER__MODULE__DEBUG(MODULE_NAME, "{}/channel[{}]={}({}): attrs={}, ctrl={}",
                                  m_iio_device_data.name.c_str(), j, iio_channel_get_id(channel), chan_type,
                                  iio_channel_get_attrs_count(channel), iio_channel_is_enabled(channel) ? "on" : "off");

            if (chan_type == IIO_ANGL_VEL)
            {
                gyro_status_t status =
                    channel_attr_wr_str(channel, "scale", (std::stringstream() << m_gyro_scale).str().c_str());
                if (status != GYRO_STATUS_SUCCESS)
                {
                    LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set scale for channel[{}], error code: {}", j,
                                          status);
                    return status;
                }
            }
        }
    }

    return GYRO_STATUS_SUCCESS;
}

gyro_status_t GyroDevice::prepare_device()
{
    gyro_status_t status;

    m_iio_dev = iio_context_find_device(m_ctx, m_iio_device_data.name.c_str());
    if (!m_iio_dev)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME,
                              "Gyro device {} not found! Make sure the device is connected and "
                              "sensor_name in the configuration file matches the gyro device name "
                              "(the one displayed in iio_info).",
                              m_iio_device_data.name.c_str());
        m_iio_dev = NULL;
        return GYRO_STATUS_IIO_CONTEXT_FAILURE;
    }

    status = device_cfg_set();
    if (status != GYRO_STATUS_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure Gyro device {}.", m_iio_device_data.name.c_str());
        m_iio_dev = NULL;
        return status;
    }

    prepare_device_data();
    status = prepare_channel_data();
    if (status != GYRO_STATUS_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to prepare channel data, err: {}.", status);
        m_iio_dev = NULL;
        return status;
    }

    enable_all_channels(m_iio_dev);

    m_iio_device_data.buf = iio_device_create_buffer(m_iio_dev, FIFO_BUF_SIZE, false);
    if (!m_iio_device_data.buf)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Unable to create IIO buffer for device {}", m_iio_device_data.name.c_str());
        m_iio_dev = NULL;
        return GYRO_STATUS_IIO_CONTEXT_FAILURE;
    }

    if (!iio_buffer_start(m_iio_device_data.buf))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Unable to start IIO buffer for device {}", m_iio_device_data.name.c_str());
        m_iio_dev = NULL;
        return GYRO_STATUS_IIO_CONTEXT_FAILURE;
    }

    if (iio_buffer_set_blocking_mode(m_iio_device_data.buf, false) < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Unable to set IIO buffer to non-blocking mode for device {}",
                              m_iio_device_data.name.c_str());
        m_iio_dev = NULL;
        return GYRO_STATUS_IIO_CONTEXT_FAILURE;
    }

    return GYRO_STATUS_SUCCESS;
}

gyro_status_t GyroDevice::configure()
{
    gyro_status_t rc = start();
    if (rc != GYRO_STATUS_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure Gyro device. err: {}", rc);
        return rc;
    }

    show_device_info();
    return rc;
}

gyro_status_t GyroDevice::run()
{
    gyro_status_t rc = GYRO_STATUS_SUCCESS;

    std::vector<gyro_sample_t> samples;
    samples.reserve(FIFO_BUF_SIZE);
    if (!m_ctx)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Gyro device not initialized and run called!");
        return GYRO_STATUS_ILLEGAL_STATE;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Gyro device {} started running...", m_iio_device_data.name.c_str());
    while (!m_stopRunning)
    {
        ssize_t nbytes = iio_buffer_refill(m_iio_device_data.buf);
        if (nbytes == -EAGAIN)
        {
            usleep(GYRO_USLEEP_BETWEEN_ITERATIONS); // 0.5 msec delay
            continue;
        }
        if (nbytes < 0)
        {
            LOGGER__MODULE__WARNING(MODULE_NAME, "Could not refill buffer for device {}, rc = {}, restarting device",
                                    m_iio_device_data.name.c_str(), nbytes);
            rc = restart();
            if (rc != GYRO_STATUS_SUCCESS)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to restart Gyro device. err: {}", rc);
                return rc;
            }
            break;
        }

        iio_buffer_foreach_sample(m_iio_device_data.buf, rd_sample_demux, (void *)&samples);
        m_vector_samples->enqueue_many(samples);
        samples.clear();
    }

    shutdown();

    std::lock_guard<std::mutex> lock(m_mtx);
    m_stopRunningAck = true;
    cv.notify_all();

    return rc;
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

    gyroApi = std::make_unique<GyroDevice>(iio_device_name, device_freq, std::stod(gyro_scale));
    gyro_status_t status = gyroApi->configure();
    if (status != GYRO_STATUS_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure GyroDev, status: {}", status);
        return EXIT_FAILURE;
    }
    set_handler(SIGINT, &handle_sig);
    set_handler(SIGTERM, &handle_sig);
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, &oldset);
    std::thread gyroThread = std::thread(&GyroDevice::dump_rec_samples, gyroApi.get(), output_path);
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);

    status = gyroApi->run();
    if (status != GYRO_STATUS_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to run GyroDev, status: {}", status);
        return EXIT_FAILURE;
    }
    gyroThread.join();
    return EXIT_SUCCESS;
}
