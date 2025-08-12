#include "hdr_manager_impl.hpp"
#include "isp_utils.hpp"
#include "hdr_manager.hpp"
#include "logger_macros.hpp"
#include "media_library_types.hpp"
#include "v4l2_ctrl.hpp"
#include "video_device.hpp"
#include "hailo_media_library_perfetto.hpp"

std::optional<HDR::DOL> HdrManager::Impl::get_dol(hdr_dol_t dol)
{
    switch (dol)
    {
    case HDR_DOL_2:
        return std::make_optional(HDR::DOL::HDR_DOL_2);
    case HDR_DOL_3:
        return std::make_optional(HDR::DOL::HDR_DOL_3);
    default:
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Unsupported HDR DOL value: {}", dol);
        return std::nullopt; // Default value
    }
}

bool HdrManager::Impl::is_dol_supported(hdr_dol_t dol)
{
    if (dol == HDR_DOL_2 || dol == HDR_DOL_3)
    {
        return true;
    }
    return false;
}

std::optional<HDR::InputResolution> HdrManager::Impl::get_input_resolution(const output_resolution_t &input_resolution)
{
    if (input_resolution.dimensions.destination_width == 3840 && input_resolution.dimensions.destination_height == 2160)
    {
        return std::make_optional(HDR::InputResolution::RES_4K);
    }
    else if (input_resolution.dimensions.destination_width == 1920 &&
             input_resolution.dimensions.destination_height == 1080)
    {
        return std::make_optional(HDR::InputResolution::RES_FHD);
    }
    else if (input_resolution.dimensions.destination_width == 2688 &&
             input_resolution.dimensions.destination_height == 1520)
    {
        return std::make_optional(HDR::InputResolution::RES_4MP);
    }
    return std::nullopt;
}

bool HdrManager::Impl::is_resolution_supported(const output_resolution_t &input_resolution)
{
    auto resolution = get_input_resolution(input_resolution);
    if (!resolution)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Unsupported HDR resolution: {}x{}",
                              input_resolution.dimensions.destination_width,
                              input_resolution.dimensions.destination_height);
        return false;
    }
    return true;
}

std::optional<std::string> HdrManager::Impl::get_hdr_hef_path(HDR::DOL dol, HDR::InputResolution resolution)
{
    // Assuming the HEF path is stored in a member variable
    std::string resolution_str;
    std::string dol_str;

    switch (dol)
    {
    case HDR::DOL::HDR_DOL_2:
        dol_str = "2";
        break;
    case HDR::DOL::HDR_DOL_3:
        dol_str = "3";
        break;
    default:
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Unsupported HDR DOL value: {}", dol);
        return std::nullopt;
    }

    switch (resolution)
    {
    case HDR::InputResolution::RES_4K:
        resolution_str = "4k";
        break;
    case HDR::InputResolution::RES_FHD:
        resolution_str = "fhd";
        break;
    case HDR::InputResolution::RES_4MP:
        resolution_str = "4mp";
        break;
    default:
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Unsupported HDR resolution: {}", resolution);
        return std::nullopt;
    }

    return "/usr/bin/hdr_" + resolution_str + "_" + dol_str + "_exposures.hef";
}

bool HdrManager::Impl::init(const frontend_config_t &frontend_config)
{
    auto dol = get_dol(frontend_config.hdr_config.dol);
    if (!dol)
    {

        return false;
    }

    auto input_resolution = HdrManager::Impl::get_input_resolution(frontend_config.input_config.resolution);
    if (!input_resolution)
    {
        return false;
    }

    auto hdr_hef_path = get_hdr_hef_path(dol.value(), input_resolution.value());
    if (!hdr_hef_path)
    {
        return false;
    }

    if (m_initialized)
    {
        LOGGER__MODULE__INFO(LOGGER_TYPE, "Reinitializing HdrManager");
        deinit();
    }

    int pixelFormat;
    size_t pixelWidth = 16;

    auto sensor_name = isp_utils::get_sensor_type();
    if (sensor_name.has_value() && sensor_name.value() == isp_utils::SensorType::IMX715)
    {
        pixelFormat = V4L2_PIX_FMT_SGBRG12;
    }
    else
    {
        pixelFormat = V4L2_PIX_FMT_SRGGB12;
    }

    m_raw_capture_device = std::make_unique<HDR::VideoCaptureDevice>();

    if (m_stitcher.init(hdr_hef_path.value(), frontend_config.hailort_config.device_id, SCHEDULER_THRESHOLD,
                        SCHEDULER_TIMEOUT.count(), dol.value()))
    {
        return false;
    }
    m_stitcher.set_on_infer_finish([this](void *ptr) { on_infer(ptr); });

    m_isp_in_device = std::make_unique<HDR::VideoOutputDevice>();

    if (!m_allocator.init(DMA_HEAP_PATH))
    {
        goto err_init_capture_dev;
    }

    if (!m_raw_capture_device->init(RAW_CAPTURE_PATH, "[HDR] raw out", m_allocator, dol.value(),
                                    input_resolution.value(), RAW_CAPTURE_BUFFERS_COUNT, pixelFormat, pixelWidth,
                                    RAW_CAPTURE_DEFAULT_FPS, true, false))
    {
        goto err_init_capture_dev;
    }

    if (!m_isp_in_device->init(HdrManager::Impl::ISP_IN_PATH, "[HDR] ISP in", m_allocator, STITCHED_PLANE_COUNT,
                               input_resolution.value(), HdrManager::Impl::ISP_IN_BUFFERS_COUNT, pixelFormat,
                               pixelWidth))
    {
        goto err_init_isp_in_dev;
    }

    m_isp_fd = open(HdrManager::Impl::YUV_STREAM_DEVICE_PATH, O_RDWR);
    if (m_isp_fd < 0)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to allocate stitch contexts");
        goto err_isp_fd;
    }

    m_dol = dol.value();
    m_wb_buffer_size = dol.value() * HdrManager::Impl::CFA_NUM_CHANNELS;
    if (!alloc_stitch_contexts())
    {
        goto err_alloc_stitch_contexts;
    }

    m_ls_ratio = frontend_config.hdr_config.ls_ratio;
    m_vs_ratio = frontend_config.hdr_config.vs_ratio;
    m_initialized = true;
    LOGGER__MODULE__INFO(LOGGER_TYPE, "HdrManager initialized successfully");
    return true;

err_alloc_stitch_contexts:
    close(m_isp_fd);
err_isp_fd:
err_init_isp_in_dev:
err_init_capture_dev:
    m_isp_in_device = nullptr;
    m_raw_capture_device = nullptr;
    return false;
}

HdrManager::Impl::Impl() = default;

HdrManager::Impl::~Impl()
{
    deinit();
}

void HdrManager::Impl::deinit()
{
    stop();
    free_stitch_contexts();

    m_raw_capture_device = nullptr;
    m_isp_in_device = nullptr;

    close(m_isp_fd);
    m_initialized = false;
    m_wb_clipping_warned = false;
}

void HdrManager::Impl::wait_for_yuv_stream_start()
{
    if (!m_initialized)
        return;

    ioctl(m_isp_fd, VIDEO_WAIT_FOR_STREAM_START);
}

bool HdrManager::Impl::set_ratio()
{
    unsigned int ratio[2];
    memset(ratio, 0, sizeof(ratio));

    ratio[0] = m_ls_ratio * (1 << 16);
    ratio[1] = m_vs_ratio * (1 << 16);
    return v4l2::ext_ctrl_set(v4l2::Video0Ctrl::HDR_RATIOS, std::span{ratio});
}

bool HdrManager::Impl::alloc_stitch_contexts()
{
    // we want an extra context so even when there are no buffers we can already have a context ready
    std::vector<StitchContext> stitch_contexts;
    stitch_contexts.resize(std::min(RAW_CAPTURE_BUFFERS_COUNT, ISP_IN_BUFFERS_COUNT) + 1);
    for (size_t i = 0; i < stitch_contexts.size(); i++)
    {
        stitch_contexts[i].m_in_use = false;

        if (!m_allocator.alloc(m_wb_buffer_size, stitch_contexts[i].m_wb_buffer))
        {
            /* free all wbBuffers that were already allocated */
            for (size_t j = 0; j < i; j++)
            {
                close(stitch_contexts[j].m_wb_buffer.m_fd);
            }
            LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to allocate stitch contexts");
            return false;
        }

        // Allow access to the buffer from this process
        stitch_contexts[i].m_wb_buffer.map();
    }
    m_stitch_contexts = std::move(stitch_contexts);
    return true;
}

void HdrManager::Impl::free_stitch_contexts()
{
    for (auto &stitch_context : m_stitch_contexts)
    {
        stitch_context.m_wb_buffer.unmap();
        close(stitch_context.m_wb_buffer.m_fd);
    }
    m_stitch_contexts.clear();
    m_stitch_contexts.shrink_to_fit();
}

bool HdrManager::Impl::get_stitch_context(StitchContext **context)
{
    for (auto &stitch_context : m_stitch_contexts)
    {
        if (!stitch_context.m_in_use)
        {
            stitch_context.m_in_use = true;
            *context = &stitch_context;
            return true;
        }
    }
    return false;
}

void HdrManager::Impl::put_stitch_context(StitchContext *context)
{
    context->m_in_use = false;
}

bool HdrManager::Impl::update_wb_gains(HDR::DMABuffer &dma_wb_buffer)
{
    auto *wb_buffer = static_cast<unsigned char *>(dma_wb_buffer.ptr());
    int channels_raw[4];
    float channels[4];
    memset(channels_raw, 0, sizeof(channels_raw));
    memset(channels, 0, sizeof(channels));

    if (!m_initialized || !wb_buffer)
        return false;

    const int bayer_pattern_order_rggb[4] = {0, 1, 2, 3};
    const int bayer_pattern_order_gbrg[4] = {2, 3, 0, 1};
    const int *bayer_pattern_order = nullptr;

    switch (m_raw_capture_device->get_pix_fmt())
    {
    case V4L2_PIX_FMT_SRGGB12:
        bayer_pattern_order = bayer_pattern_order_rggb;
        break;
    case V4L2_PIX_FMT_SGBRG12:
        bayer_pattern_order = bayer_pattern_order_gbrg;
        break;
    default:
        // we should never arrive here since we check isSupportedFormat() before
        return false;
    }

    auto wb_r_gain = v4l2::ext_ctrl_get<int, v4l2::Video0Ctrl>(v4l2::Video0Ctrl::WB_R_GAIN);
    if (!wb_r_gain.has_value())
    {
        return false;
    }
    auto wb_gr_gain = v4l2::ext_ctrl_get<int, v4l2::Video0Ctrl>(v4l2::Video0Ctrl::WB_GR_GAIN);
    if (!wb_gr_gain.has_value())
    {
        return false;
    }
    auto wb_gb_gain = v4l2::ext_ctrl_get<int, v4l2::Video0Ctrl>(v4l2::Video0Ctrl::WB_GB_GAIN);
    if (!wb_gb_gain.has_value())
    {
        return false;
    }
    auto wb_b_gain = v4l2::ext_ctrl_get<int, v4l2::Video0Ctrl>(v4l2::Video0Ctrl::WB_B_GAIN);
    if (!wb_b_gain.has_value())
    {
        return false;
    }

    channels_raw[bayer_pattern_order[0]] = wb_r_gain.value();
    channels_raw[bayer_pattern_order[1]] = wb_gr_gain.value();
    channels_raw[bayer_pattern_order[2]] = wb_gb_gain.value();
    channels_raw[bayer_pattern_order[3]] = wb_b_gain.value();

    bool clipping_occurred = false;
    for (int channel = 0; channel < 4; ++channel)
    {
        channels[channel] = ((float)channels_raw[channel]) / 256;
        float channel_quant = channels[channel] / WB_COMPENSATION;
        int channel_to_buffer = std::ceil(channel_quant);

        // clip to 127 to avoid overflow in NN-Core
        if (channel_to_buffer > 127)
        {
            channel_to_buffer = 127;
            clipping_occurred = true;
        }

        for (int plane = 0; plane < m_dol; plane++)
        {
            wb_buffer[channel + plane * 4] = channel_to_buffer;
        }
    }

    // Log warning only once per stream to avoid spam
    if (clipping_occurred && !m_wb_clipping_warned)
    {
        LOGGER__MODULE__WARN(LOGGER_TYPE, "White balance gains clipped to 127, possible bad WB tuning");
        m_wb_clipping_warned = true;
    }

    return true;
}

bool HdrManager::Impl::is_supported_format(int pix_fmt)
{
    constexpr std::array SUPPORTED_FORMATS = {V4L2_PIX_FMT_SRGGB12, V4L2_PIX_FMT_SGBRG12};
    return std::find(SUPPORTED_FORMATS.begin(), SUPPORTED_FORMATS.end(), pix_fmt) != SUPPORTED_FORMATS.end();
}

void HdrManager::Impl::hdr_loop()
{
    StitchContext *stitch_ctx = NULL;
    HDR::VideoBuffer *tmp_buf = NULL;

    if (!m_initialized)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "HDR loop error: Not initialized");
        return;
    }

    wait_for_yuv_stream_start();

    if (!set_ratio())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "HDR loop error: Failed to set ratio");
        return;
    }

    if (!m_isp_in_device->dequeue_buffers())
    {

        LOGGER__MODULE__ERROR(LOGGER_TYPE, "HDR loop error: Failed to dequeue ISP buffers");
        return;
    }

    if (!m_raw_capture_device->dequeue_buffers())
    {

        LOGGER__MODULE__ERROR(LOGGER_TYPE, "HDR loop error: Failed to dequeue raw capture buffers");
        return;
    }

    if (!m_raw_capture_device->queue_buffers())
    {

        LOGGER__MODULE__ERROR(LOGGER_TYPE, "HDR loop error: Failed to queue raw capture buffers");
        return;
    }

    if (!is_supported_format(m_raw_capture_device->get_pix_fmt()))
    {
        LOGGER__MODULE__WARN(LOGGER_TYPE, "Pix Fmt {} not supported.", m_raw_capture_device->get_pix_fmt());
        return;
    }

    while (m_running)
    {
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN("get_stitch_context", HDR_THREADED_TRACK);
        if (!get_stitch_context(&stitch_ctx))
        {
            // this should never happen, but retry regardless
            continue;
        }
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(HDR_THREADED_TRACK);

        HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN("get_buffer(raw)", HDR_THREADED_TRACK);
        if (!m_raw_capture_device->get_buffer(&stitch_ctx->m_raw_buffer))
        {
            put_stitch_context(stitch_ctx);
            continue;
        }
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(HDR_THREADED_TRACK);

        HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN("get_buffer(isp in)", HDR_THREADED_TRACK);
        if (!m_isp_in_device->get_buffer(&stitch_ctx->m_stitched_buffer))
        {
            tmp_buf = stitch_ctx->m_raw_buffer;
            put_stitch_context(stitch_ctx);
            m_raw_capture_device->put_buffer(tmp_buf);
            continue;
        }
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(HDR_THREADED_TRACK);

        stitch_ctx->m_stitched_buffer->get_v4l2_buffer()->timestamp =
            stitch_ctx->m_raw_buffer->get_v4l2_buffer()->timestamp;
        stitch_ctx->m_stitched_buffer->get_v4l2_buffer()->flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;

        HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN("update_wb_gains", HDR_THREADED_TRACK);
        update_wb_gains(stitch_ctx->m_wb_buffer);
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(HDR_THREADED_TRACK);

        HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN("stitcher.prcoess", HDR_THREADED_TRACK);
        m_stitcher.process(stitch_ctx->m_raw_buffer->get_planes(), stitch_ctx->m_wb_buffer.m_fd,
                           stitch_ctx->m_stitched_buffer->get_planes()[0], stitch_ctx);
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(HDR_THREADED_TRACK);
    }
}

bool HdrManager::Impl::start()
{
    std::unique_lock<std::mutex> lock(m_change_state_mutex);
    if (!m_initialized)
        return false;

    if (m_hdr_thread.joinable())
        return false;

    m_running = true;
    m_hdr_thread = std::thread(&HdrManager::Impl::hdr_loop, this);
    if (!m_hdr_thread.joinable())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to start HDR thread");
        m_running = false;
        return false;
    }
    // sleep for 100 ms
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!v4l2::ctrl_set(v4l2::Video0Ctrl::HDR_FORWARD_TIMESTAMPS, true))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to set HDR forward timestamps");
        m_running = false;
        m_hdr_thread.join();
        return false;
    }
    return true;
}

void HdrManager::Impl::stop()
{
    std::unique_lock<std::mutex> lock(m_change_state_mutex);
    if (!m_initialized)
    {
        return;
    }
    m_running = false;
    v4l2::ctrl_set(v4l2::Video0Ctrl::HDR_FORWARD_TIMESTAMPS, false);
    if (m_hdr_thread.joinable())
    {
        m_hdr_thread.join();
    }
}

void HdrManager::Impl::on_infer(void *ptr)
{
    StitchContext *stitch_ctx = static_cast<StitchContext *>(ptr);
    HDR::VideoBuffer *raw_buffer = stitch_ctx->m_raw_buffer;
    HDR::VideoBuffer *stitched_buffer = stitch_ctx->m_stitched_buffer;

    // the stitch context should be returned to the pool before the buffers
    // to allow it to be used again to hold new buffers before the buffers are returned
    put_stitch_context(stitch_ctx);
    m_raw_capture_device->put_buffer(raw_buffer);
    m_isp_in_device->put_buffer(stitched_buffer);
}
