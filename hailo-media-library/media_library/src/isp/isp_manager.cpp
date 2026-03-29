#include "isp_manager.hpp"
#include "pre_isp_denoise.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <unordered_map>

#include "config_attacher.hpp"
#include "config_manager.hpp"
#include "hdr_manager.hpp"
#include "sensor_registry.hpp"
#include "buffer_pool.hpp"
#include "isp_utils.hpp"
#include "logger_macros.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "v4l2_ctrl.hpp"
#include "video_buffer.hpp"
#include "video_device.hpp"

#define MODULE_NAME LoggerType::Isp

bool IspManager::ModePair::operator==(const ModePair &other) const
{
    return from == other.from && to == other.to;
}

std::size_t IspManager::ModePairHash::operator()(const ModePair &pair) const
{
    std::size_t h1 = std::hash<int>()(static_cast<int>(pair.from));
    std::size_t h2 = std::hash<int>()(static_cast<int>(pair.to));
    return h1 ^ (h2 << 1) ^ (h2 >> 1);
}

IspManager::IspManager() : m_loop_running(false), m_current_mode(Mode::UNKNOWN), m_config_manager_interactor(nullptr)
{
    m_allocator.init(DMA_HEAP_PATH);
}

IspManager::~IspManager()
{
    deinit();
}

bool IspManager::init_isp_out_device()
{
    if (m_isp_out_fd)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "ISP out device already initialized");
        return true;
    }

    if (!m_config_manager_interactor)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor not set, cannot initialize ISP out device");
        return false;
    }

    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No current profile is set in the configuration");
        return false;
    }

    size_t sensor_index = current_profile_opt.value()->sensor_config.input_video.sensor_id;
    auto video_device_path_opt = SensorRegistry::get_instance().get_video_device_path(sensor_index);
    if (!video_device_path_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get video device path");
        return false;
    }

    auto video_device_path = video_device_path_opt.value();
    LOGGER__MODULE__INFO(MODULE_NAME, "Initializing ISP out device: {} (sensor index: {})", video_device_path,
                         sensor_index);

    int isp_out_fd = open(video_device_path.c_str(), O_RDWR | O_CLOEXEC);
    if (isp_out_fd < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open {}, errno: {} ({})", video_device_path, errno,
                              strerror(errno));
        return false;
    }

    m_isp_out_fd = files_utils::make_shared_fd(isp_out_fd);
    m_v4l2_ctrl_manager.set_sensor_index(sensor_index);

    LOGGER__MODULE__DEBUG(MODULE_NAME, "ISP out device initialized successfully");
    return true;
}

bool IspManager::set_subscriber(int module_id, IspManager::RawBufferSubscriberCallback &&callback)
{
    std::unique_lock<std::mutex> lock(m_mode_mutex);
    if (m_raw_buffer_subscribers.find(module_id) != m_raw_buffer_subscribers.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Subscriber already set");
        return false;
    }
    m_raw_buffer_subscribers[module_id] = std::move(callback);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Subscriber set successfully");
    return true;
}

void IspManager::unset_subscriber(int module_id)
{
    std::unique_lock<std::mutex> lock(m_mode_mutex);
    m_raw_buffer_subscribers.erase(module_id);
}

bool IspManager::mode_has_raw_video_source(Mode mode)
{
    switch (mode)
    {
    case Mode::SDR:
        return false;
    case Mode::HDR_ISP_STITCH:
        return false;
    case Mode::HDR_NNCORE_STITCH:
        return true;
    case Mode::PRE_ISP_DENOISE:
        return true;
    case Mode::HDR_DENOISE:
        return true;
    case Mode::UNKNOWN:
        return false;
    default:
        LOGGER__MODULE__ERROR(MODULE_NAME, "Unknown mode");
        return false;
    }
}

bool IspManager::set_mcm_mode(Mode mode, std::optional<bool> is_input_packed)
{
    isp_utils::isp_mcm_mode mcm_mode;

    switch (mode)
    {
    case Mode::SDR: {
        bool dual_sensor = m_config_manager_interactor->is_dual_sensor();
        mcm_mode = dual_sensor ? isp_utils::ISP_MCM_MODE_MULTI_SENSOR : isp_utils::ISP_MCM_MODE_OFF;
        break;
    }
    case Mode::HDR_ISP_STITCH:
        mcm_mode = isp_utils::ISP_MCM_MODE_OFF;
        break;
    case Mode::HDR_NNCORE_STITCH:
        mcm_mode = isp_utils::ISP_MCM_MODE_STITCHING;
        break;
    case Mode::PRE_ISP_DENOISE: {
        if (!is_input_packed.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "is_input_packed must be provided for PRE_ISP_DENOISE mode");
            return false;
        }
        mcm_mode = is_input_packed.value() ? isp_utils::ISP_MCM_MODE_PACKED : isp_utils::ISP_MCM_MODE_INJECTION;
        break;
    }
    case Mode::HDR_DENOISE:
        mcm_mode = isp_utils::ISP_MCM_MODE_RAW_WRITE;
        break;
    case Mode::UNKNOWN:
    default:
        LOGGER__MODULE__ERROR(MODULE_NAME, "Cannot determine MCM mode for UNKNOWN mode");
        return false;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Setting MCM_MODE_SEL to {}", mcm_mode);
    if (!m_v4l2_ctrl_manager.ext_ctrl_set(get_mcm_ctrl_type(), mcm_mode))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set MCM_MODE_SEL to {}", mcm_mode);
        return false;
    }

    return true;
}

bool IspManager::wait_for_raw_video_source()
{
    std::unique_lock<std::mutex> lock(m_mode_mutex);
    m_mode_cv.wait(lock, [this]() { return (mode_has_raw_video_source(m_current_mode)) || !m_loop_running; });
    return true;
}

bool IspManager::is_input_isp_frame_packed(const frontend_config_t &frontend_config)
{
    if (frontend_config.denoise_config.enabled && frontend_config.denoise_config.bayer)
    {
        return MediaLibraryPreIspDenoise::is_packed_output(frontend_config.denoise_config);
    }
    return !frontend_config.hdr_config.enabled;
}

HailoMediaLibraryBufferPtr IspManager::hailo_buffer_from_isp_buffer(BufferType buffer_type,
                                                                    HDR::VideoBuffer *isp_buffer,
                                                                    bool is_packed_isp_input)
{
    // get v4l2 data from video buffer
    v4l2_buffer *v4l2_data = isp_buffer->get_v4l2_buffer();
    const HailoFormat format = is_packed_isp_input ? HAILO_FORMAT_GRAY12 : HAILO_FORMAT_GRAY16;
    const HailoMemoryType memory_type = HAILO_MEMORY_TYPE_CMA;
    std::function<void(void *)> on_free = nullptr;
    // should return automatically to raw capture device
    // and only when explictily calling when inserting to isp input device
    if (buffer_type == BufferType::RAW_CAPTURE)
    {
        ++m_currently_used_raw_capture_frames;
        on_free = [this](void *buf) {
            HDR::VideoBuffer *isp_buffer = static_cast<HDR::VideoBuffer *>(buf);
            if (!m_raw_capture_device->put_buffer(isp_buffer))
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to return raw buffer to raw capture device");
            }
            --m_currently_used_raw_capture_frames;
        };
    }
    else if (buffer_type == BufferType::ISP_INPUT)
    {
        ++m_currently_used_isp_input_frames;
        on_free = [this](void *buf) {
            if (buf == nullptr)
            {
                LOGGER__MODULE__TRACE(MODULE_NAME, "Buffer already inserted to ISP input device");
                return;
            }
            HDR::VideoBuffer *isp_buffer = static_cast<HDR::VideoBuffer *>(buf);
            if (!m_isp_in_device->put_buffer(isp_buffer))
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to return buffer to ISP input device");
            }
            --m_currently_used_isp_input_frames;
        };
    }
    // fill in buffer_data values
    HailoBufferDataPtr buffer_data_ptr;
    std::vector<hailo_data_plane_t> planes;
    std::vector plane_fds = isp_buffer->get_planes();
    for (size_t i = 0; i < plane_fds.size(); ++i)
    {
        hailo_data_plane_t plane;
        plane.fd = plane_fds[i];
        plane.bytesused = v4l2_data->m.planes[i].bytesused;
        planes.push_back(plane);
    }
    const int planes_count = plane_fds.size();
    // Fill in buffer_data values
    // CMA memory until imaging sub system class supports DMABUF
    buffer_data_ptr = std::make_shared<hailo_buffer_data_t>(m_input_resolution.dimensions.destination_width,
                                                            m_input_resolution.dimensions.destination_height,
                                                            planes_count, format, memory_type, planes);

    HailoMediaLibraryBufferPtr hailo_buffer = std::make_shared<hailo_media_library_buffer>();
    hailo_buffer->create(nullptr, buffer_data_ptr, on_free, isp_buffer);

    return hailo_buffer;
}

void IspManager::pull_raw_video_buffers_loop()
{
    while (wait_for_raw_video_source() && m_loop_running)
    {
        std::unique_lock<std::mutex> lock(m_mode_mutex);
        HDR::VideoBuffer *raw_buffer = nullptr;
        if (!m_raw_capture_device)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Raw capture device is not initialized");
            break;
        }
        bool get_raw_buf_success = m_raw_capture_device->get_buffer(&raw_buffer);
        if (!get_raw_buf_success)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Getting raw buffer failed, retrying...");
            continue;
        }
        constexpr bool is_packed_format = false;
        auto hailo_buffer = hailo_buffer_from_isp_buffer(BufferType::RAW_CAPTURE, raw_buffer, is_packed_format);

        if (m_fast_toggle_mode != FastToggleMode::OFF)
        {
            m_config_attacher->attach_fallback_config(hailo_buffer);
        }
        else
        {
            m_config_attacher->attach_config(hailo_buffer);
        }

        for (const auto &[module_id, callback] : m_raw_buffer_subscribers)
        {
            if (callback)
            {
                callback(hailo_buffer);
            }
        }
    }
}

IspManager::FastToggleMode IspManager::get_fast_toggle_mode(Mode switch_to_mode)
{
    if (!m_profile_based_mode)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Fast toggle is only supported in profile based mode");
        return FastToggleMode::OFF;
    }

    if (!m_is_started)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Fast toggle is only supported when a stream is running");
        return FastToggleMode::OFF;
    }

    ModePair transition = {m_current_mode, switch_to_mode};
    auto mode_it = fast_toggle_mode_map.find(transition);
    if (mode_it == fast_toggle_mode_map.end())
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Fast toggle mode not found for transition from mode {} to mode {}",
                              static_cast<int>(m_current_mode.load()), static_cast<int>(switch_to_mode));
        return FastToggleMode::OFF;
    }

    return mode_it->second;
}

static bool is_hdr_config(const frontend_config_t &frontend_config)
{
    return frontend_config.hdr_config.enabled;
}

static bool is_pre_isp_denoise_config(const frontend_config_t &frontend_config)
{
    return frontend_config.denoise_config.enabled && frontend_config.denoise_config.bayer;
}

// Check if mode switch is needed (not in target mode, or not started yet)
static bool needs_mode_switch(bool is_target_mode, bool is_started)
{
    return !is_target_mode || !is_started;
}

bool IspManager::set_config(const frontend_config_t &frontend_config)
{
    isp_utils::set_isp_config_files_path(frontend_config.isp_config.isp_config_files_path);
    m_input_resolution = frontend_config.input_config.resolution;

    LOGGER__MODULE__DEBUG(MODULE_NAME, "current mode: {}", to_string(m_current_mode));

    const bool hdr_enabled = is_hdr_config(frontend_config);
    const bool denoise_enabled = is_pre_isp_denoise_config(frontend_config);
    const bool hdr_ratios_changed =
        m_ls_ratio != frontend_config.hdr_config.ls_ratio || m_vs_ratio != frontend_config.hdr_config.vs_ratio;

    // Determine if we're already in an HDR mode (for HDR-only case, both stitch modes are valid)
    const bool in_hdr_stitch_mode = m_current_mode == Mode::HDR_NNCORE_STITCH || m_current_mode == Mode::HDR_ISP_STITCH;

    if (hdr_enabled && denoise_enabled && needs_mode_switch(m_current_mode == Mode::HDR_DENOISE, m_is_started))
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "switching to hdr denoise");
        return switch_to_hdr_denoise(frontend_config);
    }
    else if (hdr_enabled && !denoise_enabled && needs_mode_switch(in_hdr_stitch_mode, m_is_started))
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "switching to hdr");
        return switch_to_hdr(frontend_config);
    }
    else if (!hdr_enabled && denoise_enabled &&
             needs_mode_switch(m_current_mode == Mode::PRE_ISP_DENOISE, m_is_started))
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "switching to pre isp denoise");
        return switch_to_pre_isp_denoise(frontend_config);
    }
    else if (!hdr_enabled && !denoise_enabled && needs_mode_switch(m_current_mode == Mode::SDR, m_is_started))
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "switching to sdr");
        return switch_to_sdr();
    }
    else if (is_current_mode_hdr() && hdr_ratios_changed)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "setting hdr ratios");
        return set_hdr_ratios(frontend_config.hdr_config.ls_ratio, frontend_config.hdr_config.vs_ratio);
    }
    else
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Set Config called, but No mode switch required");
        return true;
    }

    return true;
}

void IspManager::drain_buffers()
{
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (m_currently_used_raw_capture_frames > 0 || m_currently_used_isp_input_frames > 0)
    {
        if (std::chrono::steady_clock::now() > deadline)
        {
            LOGGER__MODULE__WARNING(MODULE_NAME,
                                    "Timeout while waiting for used frames to be returned. "
                                    "Currently used raw capture frames: {}, isp input frames: {}",
                                    m_currently_used_raw_capture_frames, m_currently_used_isp_input_frames);
            break;
        }
        LOGGER__MODULE__DEBUG(MODULE_NAME,
                              "Waiting for used frames to be returned. "
                              "Currently used raw capture frames: {}, isp input frames: {}",
                              m_currently_used_raw_capture_frames, m_currently_used_isp_input_frames);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void IspManager::stop()
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Stopping IspManager");
    m_loop_running = false;
    m_mode_cv.notify_all();
    if (m_get_raw_buffers_loop_thread.joinable())
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Waiting for raw buffer loop thread to join");
        m_get_raw_buffers_loop_thread.join();
        LOGGER__MODULE__INFO(MODULE_NAME, "Raw buffer loop thread joined successfully");
    }

    drain_buffers();

    if (m_raw_capture_device)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Stopping raw capture device stream");
        m_raw_capture_device->stop_stream();
        LOGGER__MODULE__INFO(MODULE_NAME, "Raw capture device stream stopped successfully");
    }
    if (m_isp_in_device)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Stopping ISP input device stream");
        m_isp_in_device->stop_stream();
        LOGGER__MODULE__INFO(MODULE_NAME, "ISP input device stream stopped successfully");
    }

    m_is_started = false;
}

void IspManager::deinit()
{
    stop();

    restore_isp_config_files_to_default();
    switch_to_sdr();

    m_raw_capture_device = nullptr;
    m_isp_in_device = nullptr;

    std::unique_lock<std::mutex> lock(m_mode_mutex);
    m_raw_buffer_subscribers.clear();
    m_current_mode = Mode::UNKNOWN;
}

bool IspManager::fast_toggle_ctrl()
{
    constexpr size_t FAST_TOGGLE_RETRY_COUNT = 10;
    constexpr std::chrono::milliseconds FAST_TOGGLE_SLEEP_MS(50);

    std::this_thread::sleep_for(
        FAST_TOGGLE_SLEEP_MS); // make sure we have at least 1 frame in raw capture and isp in device

    if (!m_v4l2_ctrl_manager.ctrl_set_retry_on_eagain(v4l2::Video0Ctrl::FAST_TOGGLE, m_fast_toggle_mode.load(),
                                                      FAST_TOGGLE_RETRY_COUNT, FAST_TOGGLE_SLEEP_MS))
    {
        LOGGER__MODULE__WARNING(MODULE_NAME, "Failed to set FAST_TOGGLE_START, after {} attempts",
                                FAST_TOGGLE_RETRY_COUNT);
        return false;
    }

    return true;
}

bool IspManager::start()
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Starting IspManager");
    if (m_is_started)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "IspManager is already started");
        return true;
    }

    if (is_current_mode_using_pre_isp_pipeline())
    {
        if (!m_raw_capture_device || !m_isp_in_device)
        {
            LOGGER__MODULE__INFO(MODULE_NAME,
                                 "Raw capture device or ISP input device are not needed, nothing to start");
            return false;
        }

        if (!m_isp_in_device->start_stream())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start ISP input device stream");
            return false;
        }

        if (!m_raw_capture_device->start_stream())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start raw capture device stream");
            return false;
        }
    }

    if (m_fast_toggle_mode != FastToggleMode::OFF)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Starting fast toggle mode");
        if (!fast_toggle_ctrl())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set FAST_TOGGLE_START");
            return false;
        }
    }
    else
    {
        if (!m_isp_out_fd)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "ISP out device not initialized");
            return false;
        }

        LOGGER__MODULE__INFO(MODULE_NAME, "Waiting for stream start");
        if (ioctl(*m_isp_out_fd, VIDEO_WAIT_FOR_STREAM_START) != 0)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "VIDEO_WAIT_FOR_STREAM_START failed, errno: {} ({})", errno,
                                  strerror(errno));
            return false;
        }
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Stream started successfully");
    }

    if (is_current_mode_hdr() && !set_hdr_ratios(m_ls_ratio, m_vs_ratio))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set HDR ratios");
        return false;
    }

    if (is_current_mode_using_pre_isp_pipeline())
    {
        if (!m_isp_in_device->dequeue_buffers())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to dequeue ISP buffers");
            return false;
        }
        m_loop_running = true;
        m_get_raw_buffers_loop_thread = std::thread(&IspManager::pull_raw_video_buffers_loop, this);
        m_mode_cv.notify_all();
    }

    m_is_started = true;
    return true;
}

v4l2::ImxCtrl IspManager::get_wdr_ctrl_type()
{
    if (m_fast_toggle_mode != FastToggleMode::OFF)
    {
        return v4l2::ImxCtrl::IMX_WDR_FAST_TOGGLE;
    }
    return v4l2::ImxCtrl::IMX_WDR;
}

v4l2::CsiCtrl IspManager::get_csi_ctrl_type()
{
    if (m_fast_toggle_mode != FastToggleMode::OFF)
    {
        return v4l2::CsiCtrl::CSI_MODE_SEL_FAST_TOGGLE;
    }
    return v4l2::CsiCtrl::CSI_MODE_SEL;
}

v4l2::IspCtrl IspManager::get_mcm_ctrl_type()
{
    if (m_fast_toggle_mode != FastToggleMode::OFF)
    {
        return v4l2::IspCtrl::MCM_MODE_FAST_TOGGLE_SEL;
    }
    return v4l2::IspCtrl::MCM_MODE_SEL;
}

bool IspManager::set_custom_rhs1_from_profile()
{
    if (!m_config_manager_interactor)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor not set, cannot set CUSTOM_RHS1");
        return false;
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No current profile, cannot set CUSTOM_RHS1");
        return false;
    }
    const auto &profile = current_profile_opt.value();
    if (!profile)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Current profile is null, cannot set CUSTOM_RHS1");
        return false;
    }
    uint32_t custom_rhs1 = profile->sensor_config.sensor_configuration.custom_readout_timing_short;
    if (!m_v4l2_ctrl_manager.ext_ctrl_set(v4l2::ImxCtrl::CUSTOM_RHS1, custom_rhs1))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set CUSTOM_RHS1");
        return false;
    }
    return true;
}

bool IspManager::is_current_mode_using_pre_isp_pipeline()
{
    return m_current_mode == Mode::PRE_ISP_DENOISE || m_current_mode == Mode::HDR_DENOISE ||
           m_current_mode == Mode::HDR_NNCORE_STITCH;
}

bool IspManager::is_current_mode_hdr()
{
    return m_current_mode == Mode::HDR_NNCORE_STITCH || m_current_mode == Mode::HDR_ISP_STITCH ||
           m_current_mode == Mode::HDR_DENOISE;
}

bool IspManager::switch_to_sdr()
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Switching to SDR mode");

    m_fast_toggle_mode = get_fast_toggle_mode(Mode::SDR);
    if (!prepare_for_fast_toggle(m_fast_toggle_mode))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to prepare for fast toggle");
        return false;
    }

    auto &registry = SensorRegistry::get_instance();
    auto mode_info = registry.get_sensor_mode_info_sdr(m_input_resolution);
    if (!mode_info)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get sensor mode info for SDR setup");
        return false;
    }
    if (isp_utils::setup_sdr(m_input_resolution) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to setup SDR");
        return false;
    }
    if (!m_v4l2_ctrl_manager.ext_ctrl_set(get_wdr_ctrl_type(), false))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set IMX_WDR");
        return false;
    }
    if (!set_custom_rhs1_from_profile())
    {
        return false;
    }

    if (!m_v4l2_ctrl_manager.ext_ctrl_set(get_csi_ctrl_type(), mode_info->csi_mode))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set CSI_MODE_SEL");
        return false;
    }

    if (!set_mcm_mode(Mode::SDR))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set MCM mode to SDR");
        return false;
    }

    m_current_mode = Mode::SDR;
    if (m_fast_toggle_mode != FastToggleMode::OFF)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Starting fast toggle mode");
        stop();
        start();
    }
    m_raw_capture_device = nullptr;
    m_isp_in_device = nullptr;
    m_fast_toggle_mode = FastToggleMode::OFF;
    m_mode_cv.notify_all();
    return true;
}

bool IspManager::set_hdr_ratios(float ls_ratio, float vs_ratio)
{
    unsigned int ratio[2];
    memset(ratio, 0, sizeof(ratio));

    ratio[0] = 15 * (1 << 16);
    ratio[1] = vs_ratio * (1 << 16);
    bool ret = m_v4l2_ctrl_manager.ext_ctrl_set(v4l2::Video0Ctrl::HDR_RATIOS, std::span{ratio});
    if (!ret)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set HDR ratios");
        return false;
    }
    m_ls_ratio = ls_ratio;
    m_vs_ratio = vs_ratio;
    return true;
}

bool IspManager::switch_to_hdr(const frontend_config_t &frontend_config)
{
    auto &registry = SensorRegistry::get_instance();
    auto stitch_mode = HdrManager::get_stitch_mode();
    LOGGER__MODULE__INFO(MODULE_NAME, "Switching to HDR mode, stitch mode: {}",
                         (stitch_mode == StitchMode::NNCORE) ? "NNCORE" : "ISP");

    Mode hdr_mode;
    if (stitch_mode == StitchMode::ISP)
    {
        hdr_mode = Mode::HDR_ISP_STITCH;
        m_fast_toggle_mode = get_fast_toggle_mode(Mode::HDR_ISP_STITCH);
    }
    else if (stitch_mode == StitchMode::NNCORE)
    {
        hdr_mode = Mode::HDR_NNCORE_STITCH;
        m_fast_toggle_mode = get_fast_toggle_mode(Mode::HDR_NNCORE_STITCH);
    }
    else
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Unsupported stitch mode");
        return false;
    }

    if (!prepare_for_fast_toggle(m_fast_toggle_mode))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to prepare for fast toggle");
        return false;
    }

    auto mode_info =
        registry.get_sensor_mode_info_hdr(frontend_config.input_config.resolution, frontend_config.hdr_config.dol);
    if (!mode_info)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get sensor mode info for HDR setup");
        return false;
    }
    if (isp_utils::setup_hdr(frontend_config.input_config.resolution, static_cast<int>(stitch_mode)) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to setup HDR");
        return false;
    }
    if (!m_v4l2_ctrl_manager.ext_ctrl_set(get_wdr_ctrl_type(), true))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set IMX_WDR");
        return false;
    }
    if (!set_custom_rhs1_from_profile())
    {
        return false;
    }
    if (!m_v4l2_ctrl_manager.ext_ctrl_set(get_csi_ctrl_type(), mode_info->csi_mode))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set CSI_MODE_SEL");
        return false;
    }
    if (!set_mcm_mode(hdr_mode))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set MCM mode to HDR");
        return false;
    }

    m_ls_ratio = frontend_config.hdr_config.ls_ratio;
    m_vs_ratio = frontend_config.hdr_config.vs_ratio;

    // no fast toggle
    if (stitch_mode == StitchMode::ISP)
    {
        m_fast_toggle_mode = FastToggleMode::OFF;
        m_current_mode = Mode::HDR_ISP_STITCH;
        m_mode_cv.notify_all();
        m_isp_in_device = nullptr;
        m_raw_capture_device = nullptr;
        return true;
    }

    stop();
    if (!setup_raw_capture_device(frontend_config))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to setup raw capture device for HDR");
        return false;
    }
    if (!setup_isp_input_device(frontend_config))
    {
        m_raw_capture_device = nullptr;
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to setup isp input device for HDR");
        return false;
    }

    m_current_mode = Mode::HDR_NNCORE_STITCH;
    if (m_fast_toggle_mode != FastToggleMode::OFF)
    {
        start();
    }
    m_fast_toggle_mode = FastToggleMode::OFF;
    m_mode_cv.notify_all();
    return true;
}

bool IspManager::switch_to_pre_isp_denoise(const frontend_config_t &frontend_config)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Setting up SDR configuration for Pre-ISP denoise");

    m_fast_toggle_mode = get_fast_toggle_mode(Mode::PRE_ISP_DENOISE);
    if (!prepare_for_fast_toggle(m_fast_toggle_mode))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to prepare for fast toggle");
        return false;
    }

    auto &registry = SensorRegistry::get_instance();
    auto mode_info = registry.get_sensor_mode_info_sdr(frontend_config.input_config.resolution);
    if (!mode_info)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get sensor mode info for Pre-ISP denoise setup");
        return false;
    }
    if (isp_utils::setup_sdr(frontend_config.input_config.resolution) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to setup SDR configuration for Pre-ISP denoise");
        return false;
    }
    if (!m_v4l2_ctrl_manager.ext_ctrl_set(get_wdr_ctrl_type(), false))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set IMX_WDR");
        return false;
    }
    if (!set_custom_rhs1_from_profile())
    {
        return false;
    }

    if (!m_v4l2_ctrl_manager.ext_ctrl_set(get_csi_ctrl_type(), mode_info->csi_mode))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set CSI_MODE_SEL");
        return false;
    }
    if (!set_mcm_mode(Mode::PRE_ISP_DENOISE, is_input_isp_frame_packed(frontend_config)))
    {
        return false;
    }

    stop(); // stopping raw capture and isp input devices gracefully before reiniting them
    if (!setup_raw_capture_device(frontend_config))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to setup raw capture device for SDR");
        return false;
    }
    if (!setup_isp_input_device(frontend_config))
    {
        m_raw_capture_device = nullptr;
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to setup isp input device for SDR");
        return false;
    }

    m_current_mode = Mode::PRE_ISP_DENOISE;
    if (m_fast_toggle_mode != FastToggleMode::OFF)
    {
        start();
    }
    m_fast_toggle_mode = FastToggleMode::OFF;
    m_mode_cv.notify_all();
    return true;
}

bool IspManager::switch_to_hdr_denoise(const frontend_config_t &frontend_config)
{
    if (is_input_isp_frame_packed(frontend_config))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Setting up isp input device for HDR in packed mode is not supported");
        return false;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Setting up HDR configuration for Pre-ISP denoise");

    auto &registry = SensorRegistry::get_instance();

    auto stitch_mode = HdrManager::get_stitch_mode();
    if (stitch_mode == StitchMode::NNCORE)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "HDR denoise is not supported for this platform");
        return false;
    }
    LOGGER__MODULE__INFO(MODULE_NAME, "Switching to HDR mode, stitch mode: {}",
                         (stitch_mode == StitchMode::NNCORE) ? "NNCORE" : "ISP");

    auto mode_info =
        registry.get_sensor_mode_info_hdr(frontend_config.input_config.resolution, frontend_config.hdr_config.dol);
    if (!mode_info)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get sensor mode info for HDR setup");
        return false;
    }
    if (isp_utils::setup_hdr(frontend_config.input_config.resolution, static_cast<int>(stitch_mode)) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to setup HDR");
        return false;
    }
    if (!m_v4l2_ctrl_manager.ext_ctrl_set(v4l2::ImxCtrl::IMX_WDR, true))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set IMX_WDR");
        return false;
    }
    if (!set_custom_rhs1_from_profile())
    {
        return false;
    }
    if (!m_v4l2_ctrl_manager.ext_ctrl_set(v4l2::CsiCtrl::CSI_MODE_SEL, mode_info->csi_mode))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set CSI_MODE_SEL");
        return false;
    }

    if (!set_mcm_mode(Mode::HDR_DENOISE))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set MCM mode to HDR_DENOISE");
        return false;
    }
    if (!setup_raw_capture_device(frontend_config))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to setup raw capture device for HDR");
        return false;
    }
    if (!setup_isp_input_device(frontend_config))
    {
        m_raw_capture_device = nullptr;
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to setup isp input device for HDR");
        return false;
    }
    m_ls_ratio = frontend_config.hdr_config.ls_ratio;
    m_vs_ratio = frontend_config.hdr_config.vs_ratio;
    m_current_mode = Mode::HDR_DENOISE;
    m_mode_cv.notify_all();

    return true;
}

bool IspManager::setup_raw_capture_device(const frontend_config_t &frontend_config)
{
    if (m_raw_capture_device != nullptr)
    {
        m_raw_capture_device = nullptr; // otherwise ISP issues
    }

    const auto is_hdr_denoise = frontend_config.hdr_config.enabled && frontend_config.denoise_config.enabled;
    if (is_hdr_denoise && (HdrManager::get_stitch_mode() == StitchMode::NNCORE ||
                           !frontend_config.denoise_config.bayer || frontend_config.input_config.sensor_index != 0))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "HDR denoise is not supported for this configuration");
        return false;
    }

    size_t sensor_index = frontend_config.input_config.sensor_index;
    LOGGER__MODULE__INFO(MODULE_NAME, "Setting up raw capture device");
    auto &registry = SensorRegistry::get_instance();
    auto raw_capture_path =
        is_hdr_denoise ? registry.get_hdr_capture_path(sensor_index) : registry.get_raw_capture_path(sensor_index);
    if (!raw_capture_path.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get raw capture path for sensor_index: {}", sensor_index);
        return false;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Raw capture path: {}", raw_capture_path.value());

    auto pixel_format = registry.get_pixel_format();
    if (!pixel_format.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get pixel format for sensor type");
        return false;
    }

    auto sensor_res = registry.detect_resolution(frontend_config.input_config.resolution);
    if (!sensor_res.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Sensor resolution is not available");
        return false;
    }

    const bool should_copy_timestamp = true;
    const bool queue_buffers_on_stream_start = true;
    const int num_of_exposures = get_num_of_exposures(frontend_config);
    auto raw_capture_device = std::make_unique<HDR::VideoCaptureDevice>();
    if (!raw_capture_device->init(raw_capture_path.value(), "raw out", m_allocator, num_of_exposures,
                                  sensor_res.value(), RAW_CAPTURE_BUFFERS_COUNT, pixel_format.value(),
                                  RAW_CAPTURE_BITS_PER_PIXEL, RAW_CAPTURE_DEFAULT_FPS, queue_buffers_on_stream_start,
                                  should_copy_timestamp))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to initialize raw capture device - path: {}",
                              raw_capture_path.value());
        return false;
    }
    m_raw_capture_device = std::move(raw_capture_device);
    LOGGER__MODULE__TRACE(MODULE_NAME, "Raw capture device initialized successfully");
    return true;
}

bool IspManager::setup_isp_input_device(const frontend_config_t &frontend_config)
{
    if (m_isp_in_device != nullptr)
    {
        m_isp_in_device = nullptr; // otherwise ISP issues
    }
    auto &registry = SensorRegistry::get_instance();
    LOGGER__MODULE__INFO(MODULE_NAME, "Setting up isp input device");

    auto pixel_format = registry.get_pixel_format();
    auto sensor_res = registry.detect_resolution(frontend_config.input_config.resolution);
    if (!pixel_format.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get pixel format for sensor type");
        return false;
    }
    const int num_of_exposures = 1;
    const auto isp_pixel_format = is_input_isp_frame_packed(frontend_config)
                                      ? get_packed_pixel_format(pixel_format.value())
                                      : pixel_format.value();
    const auto bits_per_pixel =
        is_input_isp_frame_packed(frontend_config) ? ISP_INPUT_BITS_PER_PACKED_PIXEL : ISP_INPUT_BITS_PER_PADDED_PIXEL;
    auto isp_in_device = std::make_unique<HDR::VideoOutputDevice>();
    if (!isp_in_device->init(ISP_IN_PATH, "ISP in", m_allocator, num_of_exposures, sensor_res.value(),
                             ISP_IN_BUFFERS_COUNT, isp_pixel_format.value(), bits_per_pixel))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to initialize ISP input device - path: {}", ISP_IN_PATH);
        return false;
    }

    m_isp_in_device = std::move(isp_in_device);
    LOGGER__MODULE__TRACE(MODULE_NAME, "ISP input device initialized successfully");
    return true;
}

size_t IspManager::get_num_of_exposures(const frontend_config_t &frontend_config)
{
    const size_t num_of_exposures = 1;
    const auto stitch_mode = HdrManager::get_stitch_mode();
    if (stitch_mode == StitchMode::NNCORE && frontend_config.hdr_config.enabled)
    {
        // dol is the numeric value of exposures
        // IspManager sees exposures only when stitching in NNCORE mode
        return frontend_config.hdr_config.dol;
    }
    return num_of_exposures;
}

std::optional<uint32_t> IspManager::get_packed_pixel_format(uint32_t sensor_pixel_format)
{
    switch (sensor_pixel_format)
    {
    // 12-bit 16bpp container -> 12-bit packed.
    case V4L2_PIX_FMT_SRGGB12:
        return V4L2_PIX_FMT_SRGGB12P;
    case V4L2_PIX_FMT_SGRBG12:
        return V4L2_PIX_FMT_SGRBG12P;
    case V4L2_PIX_FMT_SGBRG12:
        return V4L2_PIX_FMT_SGBRG12P;
    case V4L2_PIX_FMT_SBGGR12:
        return V4L2_PIX_FMT_SBGGR12P;

    default:
        return std::nullopt;
    }
}

std::optional<HailoMediaLibraryBufferPtr> IspManager::get_isp_input_buffer(bool is_packed_isp_input)
{
    if (!m_isp_in_device)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "ISP input device is not initialized");
        return std::nullopt;
    }
    HDR::VideoBuffer *in_buf;
    if (!m_isp_in_device->get_buffer(&in_buf))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get buffer from ISP device");
        return std::nullopt;
    }
    return hailo_buffer_from_isp_buffer(BufferType::ISP_INPUT, in_buf, is_packed_isp_input);
}

bool IspManager::put_buffer_into_isp(HailoMediaLibraryBufferPtr buffer_to_insert_into_isp)
{
    if (!m_isp_in_device)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "ISP input device is not initialized");
        return false;
    }

    if (!m_isp_in_device->put_buffer(static_cast<HDR::VideoBuffer *>(buffer_to_insert_into_isp->get_on_free_data())))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to put buffer to ISP device");
        return false;
    }
    buffer_to_insert_into_isp->clear_on_free_data();
    --m_currently_used_isp_input_frames;
    return true;
}

static std::stringstream get_timestamped_stringstream()
{
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::stringstream timestamp;
    timestamp << std::put_time(std::localtime(&time_t_now), "%Y%m%d%H%M%S") << std::setw(3) << std::setfill('0')
              << ms.count();
    return timestamp;
}

static void safe_remove_symlink_target(const std::filesystem::path &symlink)
{
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(symlink)))
    {
        try
        {
            std::filesystem::path target = std::filesystem::read_symlink(symlink);
            if (std::filesystem::exists(target))
            {
                std::filesystem::remove(target); // Remove the target only if it exists
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error reading symlink " << symlink << ": " << e.what() << '\n';
        }
        std::filesystem::remove(symlink); // Remove the symlink itself
    }
}

static bool write_to_file(const std::string &file_path, const std::string &file_content)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Configuring {} config file", file_path);

    std::ofstream file(file_path);
    if (!file.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open file for writing: {}", file_path);
        return false;
    }
    file << file_content;
    file.close();
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Config written to {}", file_path);

    return true;
}
void IspManager::set_config_manager_interactor(const ConfigManagerInteractor *config_manager_interactor,
                                               bool profile_based_mode)
{
    m_profile_based_mode = profile_based_mode;
    m_config_attacher = std::make_unique<ConfigAttacher>(config_manager_interactor);
    m_config_manager_interactor = config_manager_interactor;

    if (!init_isp_out_device())
    {
        LOGGER__MODULE__WARNING(MODULE_NAME,
                                "Failed to initialize ISP out device during set_config_manager_interactor");
    }
}

bool IspManager::remove_current_isp_config_files_symlinks()
{
    auto symlink_3aconfig_opt = m_config_manager_interactor->get_isp_3a_config_symlink_path();
    auto symlink_sensor_opt = m_config_manager_interactor->get_isp_sensor_symlink_path();
    if (!symlink_3aconfig_opt.has_value() || !symlink_sensor_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "ISP symlink paths are not set in the configuration");
        return false;
    }
    auto &symlink_3aconfig = symlink_3aconfig_opt.value();
    auto &symlink_sensor = symlink_sensor_opt.value();
    try
    {
        safe_remove_symlink_target(symlink_3aconfig);
        safe_remove_symlink_target(symlink_sensor);
    }
    catch (std::filesystem::filesystem_error &e)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to remove old symlinks: {}", e.what());
        return false;
    }
    return true;
}

bool IspManager::restore_isp_config_files_to_default()
{
    return remove_current_isp_config_files_symlinks();
}

bool IspManager::modify_isp_config_files()
{
    if (!m_profile_based_mode)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Not in profile based mode, skipping ISP config modification");
        return true;
    }

    if (!m_config_manager_interactor)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "ConfigManagerInteractor is not initialized");
        return false;
    }
    tl::expected<std::string, media_library_return> aaa_config_exp = m_config_manager_interactor->get_3a_config();
    if (!aaa_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get 3A config from MediaLibConfigManager");
        return aaa_config_exp.error();
    }
    auto sensor_entry_opt = m_config_manager_interactor->get_sensor_entry_config();
    if (!sensor_entry_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get sensor entry config from MediaLibConfigManager");
        return false;
    }
    std::stringstream timestamp = get_timestamped_stringstream();

    // Construct destination file paths in /tmp/
    std::string new_3aconfig_filepath = "/tmp/TripleAConfig_" + timestamp.str() + ".json";
    write_to_file(new_3aconfig_filepath, aaa_config_exp.value());

    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No current profile is set in the configuration");
        return false;
    }
    size_t sensor_index = current_profile_opt.value()->sensor_config.input_video.sensor_id;
    std::string new_sensor_entry_filepath =
        "/tmp/Sensor" + std::to_string(sensor_index) + "Entry_" + timestamp.str() + ".json";
    write_to_file(new_sensor_entry_filepath, sensor_entry_opt.value());

    auto symlink_3aconfig_opt = m_config_manager_interactor->get_isp_3a_config_symlink_path();
    auto symlink_sensor_opt = m_config_manager_interactor->get_isp_sensor_symlink_path();
    if (!symlink_3aconfig_opt.has_value() || !symlink_sensor_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "ISP symlink paths are not set in the configuration");
        return false;
    }

    if (!remove_current_isp_config_files_symlinks())
    {
        return false;
    }

    auto &symlink_3aconfig = symlink_3aconfig_opt.value();
    auto &symlink_sensor = symlink_sensor_opt.value();
    try
    {
        std::filesystem::create_symlink(new_3aconfig_filepath, symlink_3aconfig);
        std::filesystem::create_symlink(new_sensor_entry_filepath, symlink_sensor);
    }
    catch (std::filesystem::filesystem_error &e)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create symlinks: {}", e.what());
        return false;
    }
    return true;
}

std::optional<int> IspManager::get_raw_capture_pix_format()
{
    if (!m_raw_capture_device)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Raw capture device is not initialized");
        return std::nullopt;
    }
    return m_raw_capture_device->get_pix_fmt();
}

bool IspManager::is_fast_toggle_supported()
{
    return HdrManager::get_stitch_mode() == StitchMode::NNCORE;
}

bool IspManager::prepare_for_fast_toggle(FastToggleMode fast_toggle_mode)
{
    if (fast_toggle_mode == FastToggleMode::OFF)
    {
        return true;
    }
    LOGGER__MODULE__INFO(MODULE_NAME, "Preparing for fast toggle from mode {} to mode {}",
                         static_cast<int>(m_current_mode.load()), static_cast<int>(fast_toggle_mode));
    if (!m_v4l2_ctrl_manager.ctrl_set(v4l2::Video0Ctrl::FAST_TOGGLE_PREPARE, static_cast<int>(fast_toggle_mode)))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set FAST_TOGGLE_PREPARE");
        return false;
    }

    // modify should happen when changing v4l2src from null to ready, but when fast toggling it
    // doesn't happen so we can to do it here, after setting FAST_TOGGLE_PREPARE
    modify_isp_config_files();
    return true;
}
