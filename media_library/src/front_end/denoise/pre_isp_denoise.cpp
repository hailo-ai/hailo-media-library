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
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "isp_utils.hpp"
#include "sensor_registry.hpp"
#include "video_device.hpp"

#include <linux/v4l2-controls.h>
#include <linux/v4l2-subdev.h>
#include <optional>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <tl/expected.hpp>
#include <ctime>

#define MODULE_NAME LoggerType::Denoise

#define IOCTL_WAIT_FOR_STREAM_START _IO('D', BASE_VIDIOC_PRIVATE + 3)

MediaLibraryPreIspDenoise::MediaLibraryPreIspDenoise(std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager)
    : MediaLibraryDenoise(), m_v4l2_ctrl_manager(v4l2_ctrl_manager)
{
    m_hailort_denoise = std::make_unique<HailortAsyncDenoisePreISP>(
        [this](HailoMediaLibraryBufferPtr output_buffer) { inference_callback(output_buffer); });
    MediaLibraryDenoise::callbacks_t callbacks;
    callbacks.on_buffer_ready = [this](HailoMediaLibraryBufferPtr output_buffer) {
        write_output_buffer(output_buffer);
    };
    observe(callbacks);
}

MediaLibraryPreIspDenoise::~MediaLibraryPreIspDenoise()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Pre ISP Denoise - destructor");

    // Stop the ISP thread and ensure it is fully joined
    stop_isp_thread();

    deinit();

    // Flush and clear all pending buffers before invalidating devices
    m_flushing = true;
    m_inference_callback_condvar.notify_one();
    m_loopback_condvar.notify_one();

    if (m_inference_callback_thread.joinable())
    {
        m_inference_callback_thread.join();
    }

    // Clear any remaining buffers in the loopback queue
    clear_loopback_queue();
}

// overrides

bool MediaLibraryPreIspDenoise::currently_enabled()
{
    return m_denoise_configs.enabled && m_denoise_configs.bayer;
}

bool MediaLibraryPreIspDenoise::enabled(const denoise_config_t &denoise_configs)
{
    return denoise_common::pre_isp_enabled(m_denoise_configs, denoise_configs);
}

bool MediaLibraryPreIspDenoise::disabled(const denoise_config_t &denoise_configs)
{
    return denoise_common::pre_isp_disabled(m_denoise_configs, denoise_configs);
}

bool MediaLibraryPreIspDenoise::enable_changed(const denoise_config_t &denoise_configs)
{
    return denoise_common::pre_isp_enable_changed(m_denoise_configs, denoise_configs);
}

bool MediaLibraryPreIspDenoise::network_changed(const denoise_config_t &denoise_configs,
                                                const hailort_t &hailort_configs)
{
    return denoise_configs.bayer && ((denoise_configs.bayer_network_config != m_denoise_configs.bayer_network_config) ||
                                     (hailort_configs.device_id != m_hailort_configs.device_id));
}

media_library_return MediaLibraryPreIspDenoise::create_and_initialize_buffer_pools(
    const input_video_config_t &input_video_configs)
{
    (void)input_video_configs; // unused parameter
    m_allocator = std::make_shared<HDR::DMABufferAllocator>();
    if (!m_allocator->init(DMA_HEAP_PATH))
    {
        return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Create output dgain buffer pool
    LOGGER__MODULE__DEBUG(
        MODULE_NAME, "Initalizing buffer pool named {} for dgain resolution: width {} height {} in buffers size of {}",
        BUFFER_POOL_NAME_DGAIN, DGAIN_WIDTH, DGAIN_HEIGHT, BUFFER_POOL_MAX_BUFFERS);

    if (m_dgain_buffer_pool == nullptr)
    {
        m_dgain_buffer_pool = std::make_shared<MediaLibraryBufferPool>(
            DGAIN_WIDTH, DGAIN_HEIGHT, HAILO_FORMAT_GRAY16, BUFFER_POOL_MAX_BUFFERS, HAILO_MEMORY_TYPE_DMABUF,
            BUFFER_POOL_NAME_DGAIN);
    }
    if (m_dgain_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init dgain buffer pool");
        return media_library_return::MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    // Create output bls buffer pool
    LOGGER__MODULE__DEBUG(
        MODULE_NAME, "Initalizing buffer pool named {} for bls resolution: width {} height {} in buffers size of {}",
        BUFFER_POOL_NAME_BLS, BLS_WIDTH, BLS_HEIGHT, BUFFER_POOL_MAX_BUFFERS);

    if (m_bls_buffer_pool == nullptr)
    {
        m_bls_buffer_pool = std::make_shared<MediaLibraryBufferPool>(BLS_WIDTH, BLS_HEIGHT, HAILO_FORMAT_GRAY16,
                                                                     BUFFER_POOL_MAX_BUFFERS, HAILO_MEMORY_TYPE_DMABUF,
                                                                     BUFFER_POOL_NAME_BLS);
    }
    if (m_bls_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init bls buffer pool");
        return media_library_return::MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryPreIspDenoise::close_buffer_pools()
{
    stop_isp_thread();
    m_allocator = nullptr;

    // close and free dgain buffer pool
    if (m_dgain_buffer_pool->wait_for_used_buffers() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to wait for used buffers to be released");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    if (m_dgain_buffer_pool->free() != MEDIA_LIBRARY_SUCCESS)
    {
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    // close and free bls buffer pool
    if (m_bls_buffer_pool->wait_for_used_buffers() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to wait for used buffers to be released");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    if (m_bls_buffer_pool->free() != MEDIA_LIBRARY_SUCCESS)
    {
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

bool MediaLibraryPreIspDenoise::process_inference(HailoMediaLibraryBufferPtr input_buffer,
                                                  HailoMediaLibraryBufferPtr loopback_buffer,
                                                  HailoMediaLibraryBufferPtr output_buffer)
{
    if (!m_denoise_configs.bayer_network_config.dgain_channel.empty() ||
        !m_denoise_configs.bayer_network_config.bls_channel.empty())
    {
        // DGAIN
        HailoMediaLibraryBufferPtr dgain_buffer = std::make_shared<hailo_media_library_buffer>();
        if (m_dgain_buffer_pool->acquire_buffer(dgain_buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "failed to acquire buffer for denoise dgain");
            return false;
        }
        if (DmaMemoryAllocator::get_instance().dmabuf_sync_start(dgain_buffer->get_plane_ptr(0)) !=
            MEDIA_LIBRARY_SUCCESS)
            return false;
        *reinterpret_cast<uint16_t *>(dgain_buffer->get_plane_ptr(0)) = get_dgain();
        if (DmaMemoryAllocator::get_instance().dmabuf_sync_end(dgain_buffer->get_plane_ptr(0)) != MEDIA_LIBRARY_SUCCESS)
            return false;

        // BLS
        HailoMediaLibraryBufferPtr bls_buffer = std::make_shared<hailo_media_library_buffer>();
        if (m_bls_buffer_pool->acquire_buffer(bls_buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "failed to acquire buffer for denoise bls");
            return false;
        }
        if (DmaMemoryAllocator::get_instance().dmabuf_sync_start(bls_buffer->get_plane_ptr(0)) != MEDIA_LIBRARY_SUCCESS)
            return false;
        *(reinterpret_cast<uint16_t *>(bls_buffer->get_plane_ptr(0))) = get_bls(v4l2::Video0Ctrl::BLS_RED);
        *(reinterpret_cast<uint16_t *>(bls_buffer->get_plane_ptr(0)) + 1) = get_bls(v4l2::Video0Ctrl::BLS_GREEN_RED);
        *(reinterpret_cast<uint16_t *>(bls_buffer->get_plane_ptr(0)) + 2) = get_bls(v4l2::Video0Ctrl::BLS_GREEN_BLUE);
        *(reinterpret_cast<uint16_t *>(bls_buffer->get_plane_ptr(0)) + 3) = get_bls(v4l2::Video0Ctrl::BLS_BLUE);
        if (DmaMemoryAllocator::get_instance().dmabuf_sync_end(bls_buffer->get_plane_ptr(0)) != MEDIA_LIBRARY_SUCCESS)
            return false;

        return static_cast<HailortAsyncDenoisePreISP *>(m_hailort_denoise.get())
            ->process(output_buffer, input_buffer, loopback_buffer, dgain_buffer, bls_buffer);
    }
    return static_cast<HailortAsyncDenoisePreISP *>(m_hailort_denoise.get())
        ->process(output_buffer, input_buffer, loopback_buffer);
}

media_library_return MediaLibraryPreIspDenoise::acquire_output_buffer(HailoMediaLibraryBufferPtr output_buffer)
{
    // Acquire from output pool
    HDR::VideoBuffer *out_buffer;
    if (!m_isp_in_device->get_buffer(&out_buffer))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "failed to acquire buffer for pre-isp denoise output");
        return media_library_return::MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    // Wrap out buffer in media library buffer
    MediaLibraryPreIspDenoise::hailo_buffer_from_isp_buffer(
        out_buffer, output_buffer, [this](HDR::VideoBuffer *buf) { (void)buf; }, HAILO_FORMAT_GRAY12);
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryPreIspDenoise::acquire_dgain_buffer(HailoMediaLibraryBufferPtr dgain_buffer)
{
    return m_dgain_buffer_pool->acquire_buffer(dgain_buffer);
}

media_library_return MediaLibraryPreIspDenoise::acquire_bls_buffer(HailoMediaLibraryBufferPtr bls_buffer)
{
    return m_bls_buffer_pool->acquire_buffer(bls_buffer);
}

void MediaLibraryPreIspDenoise::copy_meta(HailoMediaLibraryBufferPtr input_buffer,
                                          HailoMediaLibraryBufferPtr output_buffer)
{
    (void)input_buffer;  // Suppress unused parameter warning
    (void)output_buffer; // Suppress unused parameter warning
    // Stub implementation: does nothing in this child class
}

media_library_return MediaLibraryPreIspDenoise::init()
{
    if (m_initialized)
    {
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    int raw_isp_fd = -1;
    files_utils::SharedFd isp_fd;
    std::shared_ptr<HDR::VideoCaptureDevice> raw_capture_device;
    std::shared_ptr<HDR::VideoOutputDevice> isp_in_device;

    const bool dgain_mode = !m_denoise_configs.bayer_network_config.dgain_channel.empty();

    if (MEDIA_LIBRARY_SUCCESS != isp_utils::setup_sdr(m_input_config.resolution, m_v4l2_ctrl_manager, dgain_mode))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to setup sdr configuration");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    auto &registry = SensorRegistry::get_instance();
    auto pixel_format = registry.get_pixel_format();
    auto sensor_res = registry.detect_resolution(m_input_config.resolution);
    if (!pixel_format.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get pixel format for sensor type");
        return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Set MCM in injection mode to 12 bit packed
    if (!isp_utils::set_isp_mcm_mode(isp_utils::ISP_MCM_MODE_PACKED, m_v4l2_ctrl_manager))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set MCM_MODE_SEL to ISP_MCM_MODE_PACKED");
        return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    raw_capture_device = std::make_shared<HDR::VideoCaptureDevice>();
    auto raw_capture_path = SensorRegistry::get_instance().get_raw_capture_path(m_input_config.sensor_index);
    if (!raw_capture_path.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get raw capture path");
        return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    if (!raw_capture_device->init(raw_capture_path.value(), "[Lowlight_Bayer] raw out", *m_allocator, 1,
                                  sensor_res.value(), RAW_CAPTURE_BUFFERS_COUNT, pixel_format.value(), BITS_PER_INPUT,
                                  RAW_CAPTURE_DEFAULT_FPS, true))
    {
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    isp_in_device = std::make_shared<HDR::VideoOutputDevice>();
    if (!isp_in_device->init(ISP_IN_PATH, "[Lowlight_Bayer] ISP in", *m_allocator, 1, sensor_res.value(),
                             ISP_IN_BUFFERS_COUNT, V4L2_PIX_FMT_SRGGB12P, BITS_PER_OUTPUT))
    {
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    auto device_path = SensorRegistry::get_instance().get_video_device_path(m_sensor_index);
    if (!device_path.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get video device path");
        return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    raw_isp_fd = open(device_path.value().c_str(), O_RDWR | O_CLOEXEC);
    if (raw_isp_fd < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open video device: {}", device_path.value());
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    isp_fd = files_utils::make_shared_fd(raw_isp_fd);

    m_raw_capture_device = std::move(raw_capture_device);
    m_isp_in_device = std::move(isp_in_device);
    m_isp_fd = std::move(isp_fd);
    m_initialized = true;

    LOGGER__MODULE__INFO(MODULE_NAME, "Initialized Pre ISP Denoise");
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryPreIspDenoise::deinit()
{
    if (!m_initialized)
        return media_library_return::MEDIA_LIBRARY_SUCCESS;

    m_raw_capture_device = nullptr;
    m_isp_in_device = nullptr;

    if (!isp_utils::set_isp_mcm_mode(isp_utils::ISP_MCM_MODE_OFF, m_v4l2_ctrl_manager))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set MCM_MODE_SEL to ISP_MCM_MODE_OFF");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    m_isp_fd = nullptr;
    m_initialized = false;

    LOGGER__MODULE__INFO(MODULE_NAME, "Deinitialized Pre ISP Denoise");
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryPreIspDenoise::start()
{
    if (!m_initialized)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "MediaLibraryPreIspDenoise is not initialized");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    return start_isp_thread();
}

media_library_return MediaLibraryPreIspDenoise::stop()
{
    return stop_isp_thread();
}

void MediaLibraryPreIspDenoise::write_output_buffer(HailoMediaLibraryBufferPtr output_buffer)
{
    HDR::VideoBuffer *out_buf = static_cast<HDR::VideoBuffer *>(output_buffer->get_on_free_data());
    m_isp_in_device->put_buffer(out_buf);
}

// Wait for the YUV stream to start from video device, required before buffers can be pushed to isp
bool MediaLibraryPreIspDenoise::wait_for_stream_start()
{
    if (ioctl(*m_isp_fd, IOCTL_WAIT_FOR_STREAM_START) != 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "IOCTL_WAIT_FOR_STREAM_START failed, errno: {} ({})", errno,
                              strerror(errno));
        return false;
    }

    return true;
}

media_library_return MediaLibraryPreIspDenoise::start_isp_thread()
{
    m_isp_thread_running = true;
    m_isp_thread = std::thread([this]() {
        if (!wait_for_stream_start())
            return;

        if (!m_isp_in_device->dequeue_buffers())
            return;

        if (!m_raw_capture_device->dequeue_buffers())
            return;

        if (!m_raw_capture_device->queue_buffers())
            return;

        while (m_isp_thread_running)
        {
            // Read from raw capture device
            HDR::VideoBuffer *raw_buffer;
            if (!m_raw_capture_device->get_buffer(&raw_buffer))
                continue;

            // Wrap raw buffer in media library buffer
            HailoMediaLibraryBufferPtr hailo_buffer_raw = std::make_shared<hailo_media_library_buffer>();
            if (hailo_buffer_raw->isp_timestamp_ns == 0)
            {
                // Timestamp is needed when checking if there are pending jobs
                const auto now_time = std::chrono::system_clock::now();
                hailo_buffer_raw->isp_timestamp_ns =
                    std::chrono::time_point_cast<std::chrono::nanoseconds>(now_time).time_since_epoch().count();
            }

            MediaLibraryPreIspDenoise::hailo_buffer_from_isp_buffer(
                raw_buffer, hailo_buffer_raw, [this](HDR::VideoBuffer *buf) { m_raw_capture_device->put_buffer(buf); },
                HAILO_FORMAT_GRAY16);

            // Prepare output buffer to wrap with
            HailoMediaLibraryBufferPtr hailo_buffer_out = std::make_shared<hailo_media_library_buffer>();
            hailo_buffer_out->isp_timestamp_ns = hailo_buffer_raw->isp_timestamp_ns;

            // Start the inference loopback process
            handle_frame(hailo_buffer_raw, hailo_buffer_out);
        }

        while (m_hailort_denoise->has_pending_jobs())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryPreIspDenoise::stop_isp_thread()
{
    m_isp_thread_running = false;
    if (m_isp_thread.joinable())
        m_isp_thread.join();

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

uint16_t MediaLibraryPreIspDenoise::get_dgain()
{
    auto dgain = m_v4l2_ctrl_manager->ext_ctrl_get<uint16_t, v4l2::Video0Ctrl>(v4l2::Video0Ctrl::DG_GAIN);
    if (!dgain.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get DGAIN");
        return 0;
    }

    uint16_t adjusted_dgain = static_cast<uint16_t>((dgain.value() * DGAIN_FACTOR / DGAIN_DIVISOR) + 0.5f);

    return adjusted_dgain;
}

uint16_t MediaLibraryPreIspDenoise::get_bls(v4l2::Video0Ctrl ctrl)
{
    auto bls = m_v4l2_ctrl_manager->ext_ctrl_get<uint16_t *, v4l2::Video0Ctrl>(ctrl);
    if (!bls.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get BLS");
        return 0;
    }

    return bls.value();
}

void MediaLibraryPreIspDenoise::hailo_buffer_from_isp_buffer(HDR::VideoBuffer *video_buffer,
                                                             HailoMediaLibraryBufferPtr hailo_buffer,
                                                             std::function<void(HDR::VideoBuffer *buf)> on_free,
                                                             HailoFormat format)
{
    // get v4l2 data from video buffer
    v4l2_buffer *v4l2_data = video_buffer->get_v4l2_buffer();

    // fill in buffer_data values
    HailoBufferDataPtr buffer_data_ptr;
    hailo_data_plane_t plane;
    plane.fd = video_buffer->get_planes()[0];
    plane.bytesused = v4l2_data->m.planes[0].bytesused;
    // Fill in buffer_data values
    // CMA memory until imaging sub system class supports DMABUF
    buffer_data_ptr = std::make_shared<hailo_buffer_data_t>(
        m_input_config.resolution.dimensions.destination_width, m_input_config.resolution.dimensions.destination_height,
        1, format, HAILO_MEMORY_TYPE_CMA, std::vector<hailo_data_plane_t>{std::move(plane)});

    hailo_buffer->create(nullptr, buffer_data_ptr, std::function<void(void *)>([on_free](void *data) {
                             on_free(static_cast<HDR::VideoBuffer *>(data));
                         }),
                         video_buffer);
}

media_library_return MediaLibraryPreIspDenoise::generate_startup_buffer()
{
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}
