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

#include "pre_isp_denoise.hpp"
#include "hailort_denoise.hpp"
#include "denoise_common.hpp"
#include "buffer_pool.hpp"
#include "config_manager.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "media_library_utils.hpp"
#include "isp_utils.hpp"
#include "v4l2_ctrl.hpp"
#include "imaging/hailo_video_device.hpp"

#include <iostream>
#include <linux/v4l2-controls.h>
#include <linux/v4l2-subdev.h>
#include <stdint.h>
#include <string>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <tl/expected.hpp>
#include <vector>
#include <shared_mutex>
#include <chrono>
#include <ctime>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>

#define ISP_SUBDEVICE_NAME "hailo-isp"
#define IOCTL_WAIT_FOR_STREAM_START _IO('D', BASE_VIDIOC_PRIVATE + 3)
#define ISPIOC_V4L2_SET_MCM_MODE _IOWR('I', BASE_VIDIOC_PRIVATE + 10, uint32_t)

enum isp_mcm_mode
{
    ISP_MCM_MODE_OFF = 0,
    ISP_MCM_MODE_STITCHING, // default mode for MCM
    ISP_MCM_MODE_INJECTION,
    ISP_MCM_MODE_MAX
};

class MediaLibraryPreIspDenoise::Impl final
{
  public:
    Impl();
    ~Impl();
    Impl(Impl &&) = delete;
    Impl &operator=(Impl &&) = delete;

    // Configure the denoise module with new json string
    media_library_return configure(const std::string &config_string);

    // Configure the denoise module with denoise_config_t and hailort_t object
    media_library_return configure(const denoise_config_t &denoise_configs, const hailort_t &hailort_configs);

    // get the denoise configurations object
    denoise_config_t get_denoise_configs();

    // get the hailort configurations object
    hailort_t get_hailort_configs();

    // get the enabled config status
    bool is_enabled();

    // start the ISP thread
    media_library_return start();

    // stop the ISP thread
    media_library_return stop();

    // set the callbacks object
    media_library_return observe(const MediaLibraryPreIspDenoise::callbacks_t &callbacks);

    // buffer wrapper
    static void hailo_buffer_from_isp_buffer(HDR::VideoBuffer *video_buffer, HailoMediaLibraryBufferPtr buffer,
                                             std::function<void(HDR::VideoBuffer *buf)> on_free);

  private:
    static constexpr size_t QUEUE_DEFAULT_SIZE = 4;

    static constexpr int HAILORT_SCHEDULER_THRESHOLD = 1;
    static constexpr std::chrono::milliseconds HAILORT_SCHEDULER_TIMEOUT{1000};
    static constexpr int HAILORT_SCHEDULER_BATCH_SIZE = 2;

    static constexpr size_t BUFFER_POOL_MAX_BUFFERS = 10;
    static constexpr size_t BUFFER_POOL_BUFFER_WIDTH = 3840;
    static constexpr size_t BUFFER_POOL_BUFFER_HEIGHT = 2160;
    static constexpr const char *BUFFER_POOL_NAME = "pre_isp_denoise_output";

    static constexpr int RAW_CAPTURE_BUFFERS_COUNT = 5;
    static constexpr int ISP_IN_BUFFERS_COUNT = 3;
    static constexpr const char *YUV_STREAM_PATH = "/dev/video0";
    static constexpr const char *RAW_CAPTURE_PATH = "/dev/video2";
    static constexpr const char *ISP_IN_PATH = "/dev/video3";
    static constexpr const char *DMA_HEAP_PATH = "/dev/dma_heap/linux,cma";
    static constexpr int RAW_CAPTURE_DEFAULT_FPS = 30;
    int m_isp_fd;
    std::shared_ptr<HDR::VideoCaptureDevice> m_raw_capture_device;
    std::shared_ptr<HDR::VideoOutputDevice> m_isp_in_device;
    std::shared_ptr<HDR::DMABufferAllocator> m_allocator;

    // configured flag - to determine if first configuration was done
    // configuration manager
    ConfigManager m_denoise_config_manager;
    ConfigManager m_hailort_config_manager;
    std::vector<MediaLibraryPreIspDenoise::callbacks_t> m_callbacks;
    // output buffer pool
    std::shared_ptr<MediaLibraryBufferPool> m_output_buffer_pool;
    // operation configurations
    denoise_config_t m_denoise_configs;
    hailort_t m_hailort_configs;
    // configuration mutex
    std::shared_mutex rw_lock;
    // HRT module
    HailortAsyncDenoise m_hailort_denoise;
    bool m_configured = false;
    // ISP thread
    std::thread m_isp_thread;
    std::atomic<bool> m_isp_thread_running;
    // Staging queue
    std::condition_variable m_staging_condvar;
    std::mutex m_staging_mutex;
    std::queue<HailoMediaLibraryBufferPtr> m_staging_queue;

    media_library_return create_and_initialize_buffer_pools();
    media_library_return decode_config_json_string(denoise_config_t &denoise_configs, hailort_t &hailort_configs,
                                                   std::string config_string);
    void inference_callback(HailoMediaLibraryBufferPtr output_buffer);
    media_library_return start_isp_thread();
    media_library_return stop_isp_thread();
    void wait_for_stream_start();
    int set_isp_mcm_mode(uint32_t target_mcm_mode);
    void queue_staging_buffer(HailoMediaLibraryBufferPtr buffer);
    HailoMediaLibraryBufferPtr dequeue_staging_buffer();
    void clear_staging_queue();
    std::pair<int, size_t> getPixelFormatAndWidth();
};

//------------------------ MediaLibraryPreIspDenoise ------------------------

MediaLibraryPreIspDenoise::MediaLibraryPreIspDenoise()
{
    m_impl = std::make_unique<MediaLibraryPreIspDenoise::Impl>();
}

MediaLibraryPreIspDenoise::~MediaLibraryPreIspDenoise() = default;

media_library_return MediaLibraryPreIspDenoise::configure(const std::string &config_string)
{
    return m_impl->configure(config_string);
}

media_library_return MediaLibraryPreIspDenoise::configure(const denoise_config_t &denoise_configs,
                                                          const hailort_t &hailort_configs)
{
    return m_impl->configure(denoise_configs, hailort_configs);
}

denoise_config_t MediaLibraryPreIspDenoise::get_denoise_configs()
{
    return m_impl->get_denoise_configs();
}

hailort_t MediaLibraryPreIspDenoise::get_hailort_configs()
{
    return m_impl->get_hailort_configs();
}

bool MediaLibraryPreIspDenoise::is_enabled()
{
    return m_impl->is_enabled();
}

media_library_return MediaLibraryPreIspDenoise::observe(const MediaLibraryPreIspDenoise::callbacks_t &callbacks)
{
    return m_impl->observe(callbacks);
}

media_library_return MediaLibraryPreIspDenoise::start()
{
    return m_impl->start();
}

media_library_return MediaLibraryPreIspDenoise::stop()
{
    return m_impl->stop();
}

//------------------------ MediaLibraryPreIspDenoise::Impl ------------------------

MediaLibraryPreIspDenoise::Impl::Impl()
    : m_denoise_config_manager(ConfigSchema::CONFIG_SCHEMA_DENOISE),
      m_hailort_config_manager(ConfigSchema::CONFIG_SCHEMA_HAILORT),
      m_output_buffer_pool(std::make_shared<MediaLibraryBufferPool>(BUFFER_POOL_BUFFER_WIDTH, BUFFER_POOL_BUFFER_HEIGHT,
                                                                    HAILO_FORMAT_NV12, BUFFER_POOL_MAX_BUFFERS,
                                                                    HAILO_MEMORY_TYPE_DMABUF, BUFFER_POOL_NAME)),
      m_hailort_denoise([this](HailoMediaLibraryBufferPtr output_buffer) { inference_callback(output_buffer); })
{
    m_isp_fd = open(YUV_STREAM_PATH, O_RDWR);
}

MediaLibraryPreIspDenoise::Impl::~Impl()
{
    LOGGER__DEBUG("Pre ISP Denoise - destructor");
    stop_isp_thread();
    m_output_buffer_pool->for_each_buffer(
        [this](int fd, size_t size) { return m_hailort_denoise.unmap_buffer_to_hailort(fd, size); });
    m_raw_capture_device = nullptr;
    m_isp_in_device = nullptr;
    close(m_isp_fd);
}

media_library_return MediaLibraryPreIspDenoise::Impl::decode_config_json_string(denoise_config_t &denoise_configs,
                                                                                hailort_t &hailort_configs,
                                                                                std::string config_string)
{
    media_library_return hailort_status =
        m_hailort_config_manager.config_string_to_struct<hailort_t>(config_string, hailort_configs);
    if (hailort_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode Hailort config from json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    media_library_return denoise_status =
        m_denoise_config_manager.config_string_to_struct<denoise_config_t>(config_string, denoise_configs);
    if (denoise_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode denoise config from json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryPreIspDenoise::Impl::configure(const std::string &config_string)
{
    denoise_config_t denoise_configs;
    hailort_t hailort_configs;
    LOGGER__INFO("Configuring denoise Decoding json string");
    if (decode_config_json_string(denoise_configs, hailort_configs, config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return configure(denoise_configs, hailort_configs);
}

media_library_return MediaLibraryPreIspDenoise::Impl::configure(const denoise_config_t &denoise_configs,
                                                                const hailort_t &hailort_configs)
{
    LOGGER__INFO("Configuring pre ISP denoise");
    std::unique_lock<std::shared_mutex> lock(rw_lock);

    bool enabled_changed = denoise_common::pre_isp_enable_changed(m_denoise_configs, denoise_configs);
    LOGGER__INFO("NOTE: Loopback limit configurations are only applied when denoise is enabled.");

    if (!enabled_changed && !denoise_configs.enabled)
    {
        LOGGER__INFO("Pre ISP Denoise Remains disabled, skipping configuration");
        return MEDIA_LIBRARY_SUCCESS;
    }

    if (denoise_configs.bayer && ((denoise_configs.bayer_network_config != m_denoise_configs.bayer_network_config) ||
                                  (hailort_configs.device_id != m_hailort_configs.device_id)))
    {
        if (!m_hailort_denoise.set_config(denoise_configs, hailort_configs.device_id, HAILORT_SCHEDULER_THRESHOLD,
                                          HAILORT_SCHEDULER_TIMEOUT, HAILORT_SCHEDULER_BATCH_SIZE))
        {
            LOGGER__ERROR("Failed to init hailort");
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    // check if enabling
    if (denoise_common::pre_isp_enabled(m_denoise_configs, denoise_configs))
    {
        media_library_return ret = create_and_initialize_buffer_pools();
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to allocate denoise buffer pool");
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
        m_allocator = std::make_shared<HDR::DMABufferAllocator>();
        if (!m_allocator->init(DMA_HEAP_PATH))
        {
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
        auto [pixelFormat, pixelWidth] = getPixelFormatAndWidth();
        m_raw_capture_device = std::make_shared<HDR::VideoCaptureDevice>();
        if (!m_raw_capture_device->init(RAW_CAPTURE_PATH, *m_allocator, 1, HDR::InputResolution::RES_4K,
                                        RAW_CAPTURE_BUFFERS_COUNT, pixelFormat, pixelWidth, RAW_CAPTURE_DEFAULT_FPS,
                                        true))
        {
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
        m_isp_in_device = std::make_shared<HDR::VideoOutputDevice>();
        if (!m_isp_in_device->init(ISP_IN_PATH, *m_allocator, 1, HDR::InputResolution::RES_4K, ISP_IN_BUFFERS_COUNT,
                                   pixelFormat, pixelWidth))
        {
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    // check if disabling
    if (denoise_common::pre_isp_disabled(m_denoise_configs, denoise_configs))
    {
        stop_isp_thread();
        m_output_buffer_pool->for_each_buffer(
            [this](int fd, size_t size) { return m_hailort_denoise.unmap_buffer_to_hailort(fd, size); });

        // Wait 1000 milliseconds for all used buffers to be released
        if (m_output_buffer_pool->wait_for_used_buffers(1000) != MEDIA_LIBRARY_SUCCESS)
        {
            return MEDIA_LIBRARY_ERROR;
        }
        if (m_output_buffer_pool->free() != MEDIA_LIBRARY_SUCCESS)
        {
            return MEDIA_LIBRARY_ERROR;
        }
        m_raw_capture_device = nullptr;
        m_isp_in_device = nullptr;
        m_allocator = nullptr;
    }

    // Call observing callbacks in case configuration changed
    for (auto &callbacks : m_callbacks)
    {
        // We use the denoise_common::pre_isp_enabled to cover corner cases
        if (enabled_changed && callbacks.on_enable_changed)
            callbacks.on_enable_changed(denoise_common::pre_isp_enabled(m_denoise_configs, denoise_configs));
        if (enabled_changed && callbacks.send_event)
        {
            callbacks.send_event(denoise_common::pre_isp_enabled(m_denoise_configs, denoise_configs));
        }
    }

    m_denoise_configs = denoise_configs;
    m_hailort_configs = hailort_configs;
    m_configured = true;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryPreIspDenoise::Impl::create_and_initialize_buffer_pools()
{
    // Create output buffer pool
    LOGGER__DEBUG("Initalizing buffer pool named {} for output resolution: width {} height {} in buffers size of {}",
                  BUFFER_POOL_NAME, BUFFER_POOL_BUFFER_WIDTH, BUFFER_POOL_BUFFER_HEIGHT, BUFFER_POOL_MAX_BUFFERS);
    if (m_output_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to init buffer pool");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    // Pre-mapping buffers to HailoRT boost performance
    m_output_buffer_pool->for_each_buffer(
        [this](int fd, size_t size) { return m_hailort_denoise.map_buffer_to_hailort(fd, size); });

    return MEDIA_LIBRARY_SUCCESS;
}

denoise_config_t MediaLibraryPreIspDenoise::Impl::get_denoise_configs()
{
    std::unique_lock<std::shared_mutex> lock(rw_lock);
    return m_denoise_configs;
}

hailort_t MediaLibraryPreIspDenoise::Impl::get_hailort_configs()
{
    std::unique_lock<std::shared_mutex> lock(rw_lock);
    return m_hailort_configs;
}

bool MediaLibraryPreIspDenoise::Impl::is_enabled()
{
    std::unique_lock<std::shared_mutex> lock(rw_lock);
    return m_denoise_configs.enabled && m_denoise_configs.bayer;
}

media_library_return MediaLibraryPreIspDenoise::Impl::observe(const MediaLibraryPreIspDenoise::callbacks_t &callbacks)
{
    m_callbacks.push_back(callbacks);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryPreIspDenoise::Impl::start()
{
    return start_isp_thread();
}

media_library_return MediaLibraryPreIspDenoise::Impl::stop()
{
    return stop_isp_thread();
}

void MediaLibraryPreIspDenoise::Impl::inference_callback(HailoMediaLibraryBufferPtr output_buffer)
{
    HDR::VideoBuffer *out_buf = static_cast<HDR::VideoBuffer *>(output_buffer->get_on_free_data());
    m_isp_in_device->putBuffer(out_buf);
    HailoMediaLibraryBufferPtr buffer = dequeue_staging_buffer();
}

// Wait for the YUV stream to start (/dev/video0), required before
// buffers can be pushed to isp
void MediaLibraryPreIspDenoise::Impl::wait_for_stream_start()
{
    ioctl(m_isp_fd, IOCTL_WAIT_FOR_STREAM_START);
}

media_library_return MediaLibraryPreIspDenoise::Impl::start_isp_thread()
{
    m_isp_thread_running = true;
    m_isp_thread = std::thread([this]() {
        // Set MCM to injection mode
        int status = set_isp_mcm_mode(ISP_MCM_MODE_INJECTION);
        if (status < 0)
        {
            LOGGER__ERROR("Error: failed to set mcm mode, ret = {}", status);
            return;
        }

        wait_for_stream_start();

        if (!m_isp_in_device->dequeueBuffers())
            return;

        if (!m_raw_capture_device->dequeueBuffers())
            return;

        if (!m_raw_capture_device->queueBuffers())
            return;

        while (m_isp_thread_running)
        {
            // Read from raw capture device
            HDR::VideoBuffer *raw_buffer;
            if (!m_raw_capture_device->getBuffer(&raw_buffer))
                continue;

            // Wrap raw buffer in media library buffer
            HailoMediaLibraryBufferPtr hailo_buffer_raw = std::make_shared<hailo_media_library_buffer>();
            MediaLibraryPreIspDenoise::Impl::hailo_buffer_from_isp_buffer(
                raw_buffer, hailo_buffer_raw, [this](HDR::VideoBuffer *buf) { m_raw_capture_device->putBuffer(buf); });

            // Acquire from output pool
            HDR::VideoBuffer *out_buffer;
            if (!m_isp_in_device->getBuffer(&out_buffer))
            {
                m_raw_capture_device->putBuffer(raw_buffer);
                continue;
            }

            // Wrap out buffer in media library buffer
            HailoMediaLibraryBufferPtr hailo_buffer_out = std::make_shared<hailo_media_library_buffer>();
            MediaLibraryPreIspDenoise::Impl::hailo_buffer_from_isp_buffer(
                out_buffer, hailo_buffer_out, [this](HDR::VideoBuffer *buf) { m_isp_in_device->putBuffer(buf); });

            // Stage buffers for inference finish
            queue_staging_buffer(hailo_buffer_out);

            // Call infer() of loopback manager
            m_hailort_denoise.process(hailo_buffer_raw, hailo_buffer_raw, hailo_buffer_out);
        }
    });
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryPreIspDenoise::Impl::stop_isp_thread()
{
    m_isp_thread_running = false;
    if (m_isp_thread.joinable())
        m_isp_thread.join();
    return MEDIA_LIBRARY_SUCCESS;
}

int MediaLibraryPreIspDenoise::Impl::set_isp_mcm_mode(uint32_t target_mcm_mode)
{
    if (auto isp_path = isp_utils::find_subdevice_path(ISP_SUBDEVICE_NAME); !isp_path.empty())
    {
        isp_utils::ctrl::v4l2Control v4l2_ctrl(isp_path);
        if (!v4l2_ctrl.v4l2_ioctl_set(ISPIOC_V4L2_SET_MCM_MODE, target_mcm_mode))
            LOGGER__WARN("Failed to set CSI_MODE_SEL for {} to {}", isp_path, target_mcm_mode);
    }
    else
    {
        LOGGER__ERROR("Subdevice {} not found.", ISP_SUBDEVICE_NAME);
        return -1;
    }
    return 0;
}

void MediaLibraryPreIspDenoise::Impl::hailo_buffer_from_isp_buffer(HDR::VideoBuffer *video_buffer,
                                                                   HailoMediaLibraryBufferPtr hailo_buffer,
                                                                   std::function<void(HDR::VideoBuffer *buf)> on_free)
{
    // get v4l2 data from video buffer
    v4l2_buffer *v4l2_data = video_buffer->getV4l2Buffer();

    // fill in buffer_data values
    HailoBufferDataPtr buffer_data_ptr;
    hailo_data_plane_t plane;
    plane.fd = video_buffer->getPlanes()[0];
    plane.bytesused = v4l2_data->m.planes[0].bytesused;
    plane.bytesperline = 2 * BUFFER_POOL_BUFFER_WIDTH; // 16 bits per pixel
    // Fill in buffer_data values
    // HAILO_FORMAT_GRAY16 since we have one plane 16 bit per pixel
    // CMA memory until imaging sub system class supports DMABUF
    buffer_data_ptr = std::make_shared<hailo_buffer_data_t>(BUFFER_POOL_BUFFER_WIDTH, BUFFER_POOL_BUFFER_HEIGHT, 1,
                                                            HAILO_FORMAT_GRAY16, HAILO_MEMORY_TYPE_CMA,
                                                            std::vector<hailo_data_plane_t>{std::move(plane)});

    hailo_buffer->create(nullptr, buffer_data_ptr, std::function<void(void *)>([on_free](void *data) {
                             on_free(static_cast<HDR::VideoBuffer *>(data));
                         }),
                         video_buffer);
}

// Staging queue controls

void MediaLibraryPreIspDenoise::Impl::queue_staging_buffer(HailoMediaLibraryBufferPtr buffer)
{
    std::unique_lock<std::mutex> lock(m_staging_mutex);
    m_staging_condvar.wait(lock, [this] { return m_staging_queue.size() < QUEUE_DEFAULT_SIZE; });
    m_staging_queue.push(buffer);
    m_staging_condvar.notify_one();
}

HailoMediaLibraryBufferPtr MediaLibraryPreIspDenoise::Impl::dequeue_staging_buffer()
{
    std::unique_lock<std::mutex> lock(m_staging_mutex);
    m_staging_condvar.wait(lock, [this] { return !m_staging_queue.empty() || m_isp_thread_running; });
    if (m_staging_queue.empty())
    {
        return nullptr;
    }
    HailoMediaLibraryBufferPtr buffer = m_staging_queue.front();
    m_staging_queue.pop();
    m_staging_condvar.notify_one();
    return buffer;
}

void MediaLibraryPreIspDenoise::Impl::clear_staging_queue()
{
    std::unique_lock<std::mutex> lock(m_staging_mutex);
    while (!m_staging_queue.empty())
    {
        HailoMediaLibraryBufferPtr buffer = m_staging_queue.front();
        m_staging_queue.pop();
    }
    m_staging_condvar.notify_one();
}

std::pair<int, size_t> MediaLibraryPreIspDenoise::Impl::getPixelFormatAndWidth()
{
    static constexpr std::pair<int, size_t> defaultPixelFormat(V4L2_PIX_FMT_SRGGB12, 16);
    static const std::unordered_map<std::string, std::pair<int, size_t>> sensorFormats = {
        {"imx334", {V4L2_PIX_FMT_SRGGB12, 16}}, // unpacked
        {"imx678", {V4L2_PIX_FMT_SRGGB12, 16}}, // unpacked
        {"imx715", {V4L2_PIX_FMT_SGBRG12, 16}}, // unpacked
    };
    std::string sensorName = isp_utils::find_sensor_name();
    if (sensorFormats.find(sensorName) != sensorFormats.end())
    {
        return sensorFormats.at(sensorName);
    }
    return defaultPixelFormat;
}
