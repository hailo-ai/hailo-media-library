#include "hdr_manager_impl.hpp"

#include "hdr_manager.hpp"
#include "hrt_stitcher/hrt_stitcher.hpp"
#include "logger_macros.hpp"
#include "media_library_types.hpp"
#include "sensor_registry.hpp"
#include "video_device.hpp"
#include "media_library_types.hpp"
#include "v4l2_ctrl.hpp"
#include "video_device.hpp"
#include "hailo_media_library_perfetto.hpp"
#include "isp_utils.hpp"

#include <filesystem>
#include <optional>

bool HdrManager::Impl::is_dol_supported(hdr_dol_t dol)
{
    if (dol == HDR_DOL_2 || dol == HDR_DOL_3)
    {
        return true;
    }
    return false;
}

std::optional<std::string> HdrManager::Impl::get_hdr_hef_path(hdr_dol_t dol, Resolution resolution)
{
    // Assuming the HEF path is stored in a member variable
    std::string resolution_str;
    std::string dol_str;

    auto &registry = SensorRegistry::get_instance();
    auto resolution_info = registry.get_resolution_info(resolution);
    if (!resolution_info)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Unable to find resolution");
        return std::nullopt;
    }

    resolution_str = resolution_info->name;
    dol_str = std::to_string(static_cast<int>(dol));
    return "/usr/bin/hdr_" + resolution_str + "_" + dol_str + "_exposures.hef";
}

bool HdrManager::Impl::init(const frontend_config_t &frontend_config)
{
    int raw_isp_fd = -1;
    files_utils::SharedFd isp_fd;
    std::unique_ptr<HDR::VideoOutputDevice> isp_in_device;
    std::unique_ptr<HDR::VideoCaptureDevice> raw_capture_device;
    std::unique_ptr<HailortAsyncStitching> stitcher;
    HDR::DMABufferAllocator allocator;
    auto dol = frontend_config.hdr_config.dol;

    auto input_resolution = SensorRegistry::get_instance().detect_resolution(frontend_config.input_config.resolution);

    auto hdr_hef_path = get_hdr_hef_path(dol, input_resolution.value());
    if (!hdr_hef_path.has_value())
    {
        return false;
    }

    if (!std::filesystem::exists(hdr_hef_path.value()))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "HDR HEF file {} does not exist", hdr_hef_path.value());
        return false;
    }

    if (m_initialized)
    {
        LOGGER__MODULE__INFO(LOGGER_TYPE, "Reinitializing HdrManager");
        deinit();
    }

    size_t pixelWidth = 16;

    auto &registry = SensorRegistry::get_instance();
    auto pixelFormat = registry.get_pixel_format();

    if (!pixelFormat.has_value())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to get sensor pixel format");
        return false;
    }

    stitcher = std::make_unique<HailortAsyncStitching>();
    if (!stitcher)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to create HailortAsyncStitching instance");
        return false;
    }
    if (stitcher->init(hdr_hef_path.value(), frontend_config.hailort_config.device_id, SCHEDULER_THRESHOLD,
                       SCHEDULER_TIMEOUT.count(), dol))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to initialize HailortAsyncStitching with HEF path: {}",
                              hdr_hef_path.value());
        return false;
    }
    stitcher->set_on_infer_finish([this](std::shared_ptr<void> ptr) { on_infer(std::move(ptr)); });

    if (!allocator.init(DMA_HEAP_PATH))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to initialize DMABufferAllocator with heap path: {}", DMA_HEAP_PATH);
        return false;
    }

    if (!isp_utils::set_isp_mcm_mode(isp_utils::ISP_MCM_MODE_STITCHING, m_v4l2_ctrl_manager))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to set MCM_MODE_SEL to ISP_MCM_MODE_STITCHING");
        return false;
    }

    raw_capture_device = std::make_unique<HDR::VideoCaptureDevice>();

    auto raw_capture_path =
        SensorRegistry::get_instance().get_raw_capture_path(frontend_config.input_config.sensor_index);
    if (!raw_capture_path.has_value())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to get raw capture path");
        return false;
    }
    if (!raw_capture_device->init(raw_capture_path.value(), "[HDR] raw out", allocator, dol, input_resolution.value(),
                                  RAW_CAPTURE_BUFFERS_COUNT, pixelFormat.value(), pixelWidth, RAW_CAPTURE_DEFAULT_FPS,
                                  true, false))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to initialize VideoCaptureDevice with path: {}",
                              raw_capture_path.value());
        return false;
    }

    isp_in_device = std::make_unique<HDR::VideoOutputDevice>();

    if (!isp_in_device->init(HdrManager::Impl::ISP_IN_PATH, "[HDR] ISP in", allocator, STITCHED_PLANE_COUNT,
                             input_resolution.value(), HdrManager::Impl::ISP_IN_BUFFERS_COUNT, pixelFormat.value(),
                             pixelWidth))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to initialize VideoOutputDevice with path: {}", ISP_IN_PATH);
        return false;
    }

    auto device_path = SensorRegistry::get_instance().get_video_device_path(frontend_config.input_config.sensor_index);
    if (!device_path.has_value())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to get video device path");
        return false;
    }
    raw_isp_fd = open(device_path.value().c_str(), O_RDWR | O_CLOEXEC);
    if (raw_isp_fd < 0)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to allocate stitch contexts");
        return false;
    }
    isp_fd = files_utils::make_shared_fd(raw_isp_fd);

    auto stitch_contexts_opt = alloc_stitch_contexts(allocator, dol * HdrManager::Impl::CFA_NUM_CHANNELS);
    if (!stitch_contexts_opt)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to allocate stitch contexts");
        return false;
    }

    m_stitch_contexts = std::move(stitch_contexts_opt.value());
    m_dol = dol;
    m_ls_ratio = frontend_config.hdr_config.ls_ratio;
    m_vs_ratio = frontend_config.hdr_config.vs_ratio;
    m_isp_fd = std::move(isp_fd);
    m_isp_in_device = std::move(isp_in_device);
    m_raw_capture_device = std::move(raw_capture_device);
    m_stitcher = std::move(stitcher);
    m_initialized = true;

    LOGGER__MODULE__INFO(LOGGER_TYPE, "HdrManager initialized successfully");
    return true;
}

HdrManager::Impl::Impl(std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager)
    : m_v4l2_ctrl_manager(v4l2_ctrl_manager)
{
}

HdrManager::Impl::~Impl()
{
    deinit();
}

void HdrManager::Impl::deinit()
{
    stop();
    free_stitch_contexts();
    if (!isp_utils::set_isp_mcm_mode(isp_utils::ISP_MCM_MODE_OFF, m_v4l2_ctrl_manager))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to set MCM_MODE_SEL to ISP_MCM_MODE_OFF");
    }

    m_raw_capture_device = nullptr;
    m_isp_in_device = nullptr;
    m_isp_fd = nullptr;
    m_initialized = false;
    m_wb_clipping_warned = false;
}

void HdrManager::Impl::wait_for_yuv_stream_start()
{
    if (!m_initialized)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "HdrManager is not initialized, cannot wait for YUV stream start");
        return;
    }

    ioctl(*m_isp_fd, VIDEO_WAIT_FOR_STREAM_START);
}

bool HdrManager::Impl::set_ratio()
{
    unsigned int ratio[2];
    memset(ratio, 0, sizeof(ratio));

    ratio[0] = m_ls_ratio * (1 << 16);
    ratio[1] = m_vs_ratio * (1 << 16);
    return m_v4l2_ctrl_manager->ext_ctrl_set(v4l2::Video0Ctrl::HDR_RATIOS, std::span{ratio});
}

std::optional<std::vector<HdrManager::Impl::StitchContextPtr>> HdrManager::Impl::alloc_stitch_contexts(
    HDR::DMABufferAllocator &allocator, int wb_buffer_size)
{
    // we want an extra context so even when there are no buffers we can already have a context ready
    std::vector<StitchContextPtr> stitch_contexts;
    stitch_contexts.resize(std::min(RAW_CAPTURE_BUFFERS_COUNT, ISP_IN_BUFFERS_COUNT) + 1);
    for (size_t i = 0; i < stitch_contexts.size(); i++)
    {
        stitch_contexts[i] = std::make_shared<StitchContext>();
        if (!stitch_contexts[i])
        {
            LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to allocate stitch context");
            return std::nullopt;
        }
        stitch_contexts[i]->m_in_use = false;

        if (!allocator.alloc(wb_buffer_size, stitch_contexts[i]->m_wb_buffer))
        {
            LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to allocate WB buffer for stitch context {}", i);
            return std::nullopt;
        }

        // Allow access to the buffer from this process
        stitch_contexts[i]->m_wb_buffer.map();
    }

    return stitch_contexts;
}

void HdrManager::Impl::free_stitch_contexts()
{
    m_stitch_contexts.clear();
    m_stitch_contexts.shrink_to_fit();
}

std::optional<HdrManager::Impl::StitchContextPtr> HdrManager::Impl::get_stitch_context()
{
    for (auto &stitch_context : m_stitch_contexts)
    {
        if (!stitch_context->m_in_use)
        {
            stitch_context->m_in_use = true;
            return stitch_context;
        }
    }
    return std::nullopt;
}

void HdrManager::Impl::put_stitch_context(HdrManager::Impl::StitchContextPtr context)
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

    auto wb_r_gain = m_v4l2_ctrl_manager->ext_ctrl_get<int, v4l2::Video0Ctrl>(v4l2::Video0Ctrl::WB_R_GAIN);
    if (!wb_r_gain.has_value())
    {
        return false;
    }
    auto wb_gr_gain = m_v4l2_ctrl_manager->ext_ctrl_get<int, v4l2::Video0Ctrl>(v4l2::Video0Ctrl::WB_GR_GAIN);
    if (!wb_gr_gain.has_value())
    {
        return false;
    }
    auto wb_gb_gain = m_v4l2_ctrl_manager->ext_ctrl_get<int, v4l2::Video0Ctrl>(v4l2::Video0Ctrl::WB_GB_GAIN);
    if (!wb_gb_gain.has_value())
    {
        return false;
    }
    auto wb_b_gain = m_v4l2_ctrl_manager->ext_ctrl_get<int, v4l2::Video0Ctrl>(v4l2::Video0Ctrl::WB_B_GAIN);
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
    StitchContextPtr stitch_ctx;
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
        auto stitch_context_opt = get_stitch_context();
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(HDR_THREADED_TRACK);
        if (!stitch_context_opt)
        {
            // this should never happen, but retry regardless
            LOGGER__MODULE__ERROR(LOGGER_TYPE, "Getting stitch context failed, retrying...");
            continue;
        }
        stitch_ctx = std::move(stitch_context_opt.value());

        HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN("get_buffer(raw)", HDR_THREADED_TRACK);
        bool get_raw_buf_success = m_raw_capture_device->get_buffer(&stitch_ctx->m_raw_buffer);
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(HDR_THREADED_TRACK);
        if (!get_raw_buf_success)
        {
            put_stitch_context(std::move(stitch_ctx));
            LOGGER__MODULE__ERROR(LOGGER_TYPE, "Getting raw buffer failed, retrying...");
            continue;
        }

        HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN("get_buffer(isp in)", HDR_THREADED_TRACK);
        bool get_stitched_buf_success = m_isp_in_device->get_buffer(&stitch_ctx->m_stitched_buffer);
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(HDR_THREADED_TRACK);
        if (!get_stitched_buf_success)
        {
            tmp_buf = std::move(stitch_ctx->m_raw_buffer);
            put_stitch_context(std::move(stitch_ctx));
            m_raw_capture_device->put_buffer(std::move(tmp_buf));
            LOGGER__MODULE__ERROR(LOGGER_TYPE, "Getting ISP in buffer failed, retrying...");
            continue;
        }

        stitch_ctx->m_stitched_buffer->get_v4l2_buffer()->timestamp =
            stitch_ctx->m_raw_buffer->get_v4l2_buffer()->timestamp;
        stitch_ctx->m_stitched_buffer->get_v4l2_buffer()->flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;

        HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN("update_wb_gains", HDR_THREADED_TRACK);
        update_wb_gains(stitch_ctx->m_wb_buffer);
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(HDR_THREADED_TRACK);

        HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN("stitcher.process", HDR_THREADED_TRACK);
        auto raw_planes = stitch_ctx->m_raw_buffer->get_planes();
        int wb_fd = stitch_ctx->m_wb_buffer.get_fd();
        auto stitched_plane = stitch_ctx->m_stitched_buffer->get_planes()[0];
        if (m_stitcher->process(raw_planes, wb_fd, stitched_plane, std::move(stitch_ctx)) == HAILO_STITCH_SUCCESS)
        {
            ++m_infer_jobs_contexts_queue_size;
        }
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(HDR_THREADED_TRACK);
    }

    while (m_infer_jobs_contexts_queue_size != 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
    if (!m_v4l2_ctrl_manager->ctrl_set(v4l2::Video0Ctrl::HDR_FORWARD_TIMESTAMPS, true))
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
    m_v4l2_ctrl_manager->ctrl_set(v4l2::Video0Ctrl::HDR_FORWARD_TIMESTAMPS, false);
    if (m_hdr_thread.joinable())
    {
        m_hdr_thread.join();
    }
}

void HdrManager::Impl::on_infer(std::shared_ptr<void> ptr)
{
    LOGGER__MODULE__INFO(LOGGER_TYPE, "on_infer beginning");
    StitchContextPtr stitch_ctx = std::static_pointer_cast<StitchContext>(std::move(ptr));
    HDR::VideoBuffer *raw_buffer = stitch_ctx->m_raw_buffer;
    HDR::VideoBuffer *stitched_buffer = stitch_ctx->m_stitched_buffer;

    // the stitch context should be returned to the pool before the buffers
    // to allow it to be used again to hold new buffers before the buffers are returned
    put_stitch_context(stitch_ctx);
    m_raw_capture_device->put_buffer(raw_buffer);
    m_isp_in_device->put_buffer(stitched_buffer);
    --m_infer_jobs_contexts_queue_size;
}
