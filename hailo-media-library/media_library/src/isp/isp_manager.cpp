#include "isp_manager.hpp"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <tl/expected.hpp>
#include <sys/time.h>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include "media_library/cloexec_fstream.hpp"
#include <unordered_map>
#include <compare>
#include <ctime>
#include <exception>
#include <fstream> // IWYU pragma: keep
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "pre_isp_denoise.hpp"
#include "config_attacher.hpp"
#include "config_manager.hpp"
#include "hdr_manager.hpp"
#include "sensor_registry.hpp"
#include "buffer_pool.hpp"
#include "isp_utils.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "v4l2_ctrl.hpp"
#include "video_buffer.hpp"
#include "video_device.hpp"
#include "media_library_buffer.hpp"
#include "sensor_types.hpp"
#include "dsp_utils.hpp"

#define MODULE_NAME LoggerType::Isp

static constexpr uint64_t NS_PER_SEC = 1'000'000'000ULL;
static constexpr uint64_t NS_PER_USEC = 1'000ULL;

static uint64_t timeval_to_ns(const struct timeval &tv)
{
    return static_cast<uint64_t>(tv.tv_sec) * NS_PER_SEC + static_cast<uint64_t>(tv.tv_usec) * NS_PER_USEC;
}

static struct timeval ns_to_timeval(uint64_t ns)
{
    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(ns / NS_PER_SEC);
    tv.tv_usec = static_cast<suseconds_t>((ns % NS_PER_SEC) / NS_PER_USEC);
    return tv;
}

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
    add_internal_subscribers();
}

void IspManager::add_internal_subscribers()
{
    set_subscriber(static_cast<int>(MODULE_NAME),
                   [this](HailoMediaLibraryBufferPtr raw_buffer) { run_passthrough(raw_buffer); });
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
    case Mode::PRE_ISP_SDR:
        return true;
    case Mode::UNKNOWN:
        return false;
    default:
        LOGGER__MODULE__ERROR(MODULE_NAME, "Unknown mode");
        return false;
    }
}

bool IspManager::setup_sensor_and_isp(const ModeDescriptor &desc, const frontend_config_t &frontend_config)
{
    // Compression / decompression happen in the MCM write / read paths respectively.
    // If we stream in HDR and one of these paths is used (video6 for write, video10 for read),
    // the matching flag must be true. Per-mode expectations:
    //   HDR -> (true, true)
    //   SDR -> (false, false)
    const bool hdr_compression = (desc.target_mode == Mode::HDR_DENOISE);
    if (isp_utils::edit_media_server_hdr_compression(hdr_compression, hdr_compression) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to update HDR compression flags for mode {}",
                              to_string(desc.target_mode));
        return false;
    }

    auto &registry = SensorRegistry::get_instance();
    if (desc.is_hdr)
    {
        auto mode_info =
            registry.get_sensor_mode_info_hdr(frontend_config.input_config.resolution, frontend_config.hdr_config.dol,
                                              frontend_config.input_config.sensor_index);
        if (!mode_info)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get sensor mode info");
            return false;
        }
        auto stitch_mode = HdrManager::get_stitch_mode();
        if (isp_utils::setup_hdr(frontend_config.input_config.resolution, static_cast<int>(stitch_mode)) !=
            MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to setup HDR");
            return false;
        }
        return set_v4l2_controls(desc, mode_info->csi_mode);
    }

    auto mode_info = registry.get_sensor_mode_info_sdr(frontend_config.input_config.resolution,
                                                       frontend_config.input_config.sensor_index);
    if (!mode_info)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get sensor mode info");
        return false;
    }
    if (isp_utils::setup_sdr(frontend_config.input_config.resolution) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to setup SDR");
        return false;
    }
    return set_v4l2_controls(desc, mode_info->csi_mode);
}

bool IspManager::set_v4l2_controls(const ModeDescriptor &desc, int csi_mode)
{
    if (is_fast_denoise_switch_transition(desc))
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Fast denoise switch transition, skipping V4L2 sensor controls");
        return true;
    }

    if (!m_v4l2_ctrl_manager.ext_ctrl_set(get_wdr_ctrl_type(), desc.is_hdr))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set IMX_WDR");
        return false;
    }
    if (!set_custom_rhs1_from_profile())
    {
        return false;
    }
    if (!m_v4l2_ctrl_manager.ext_ctrl_set(get_csi_ctrl_type(), csi_mode))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set CSI_MODE_SEL");
        return false;
    }
    if (!m_v4l2_ctrl_manager.ext_ctrl_set(get_mcm_ctrl_type(), desc.mcm_mode))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set MCM_MODE_SEL");
        return false;
    }
    return true;
}

bool IspManager::setup_devices(const ModeDescriptor &desc, const frontend_config_t &frontend_config)
{
    if (is_fast_denoise_switch_transition(desc))
    {
        LOGGER__MODULE__INFO(MODULE_NAME,
                             "Fast denoise switch active, keeping raw capture and ISP input devices alive");
        return true;
    }

    if (m_is_started)
    {
        stop();
    }

    if (desc.uses_pre_isp_pipeline)
    {
        if (!setup_raw_capture_device(frontend_config))
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to setup raw capture device");
            return false;
        }
        if (!setup_isp_input_device(frontend_config))
        {
            m_raw_capture_device = nullptr;
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to setup ISP input device");
            return false;
        }
    }
    else
    {
        m_raw_capture_device = nullptr;
        m_isp_in_device = nullptr;
    }
    return true;
}

bool IspManager::is_fast_denoise_switch_transition(const ModeDescriptor &desc) const
{
    if (!m_fast_denoise_switch)
    {
        return false;
    }
    if (!m_raw_capture_device || !m_isp_in_device)
    {
        return false;
    }
    if (desc.mcm_mode != isp_utils::ISP_MCM_MODE_INJECTION)
    {
        return false;
    }
    if (m_current_mcm_mode != isp_utils::ISP_MCM_MODE_INJECTION)
    {
        return false;
    }
    const Mode current = m_current_mode;
    const bool current_eligible = current == Mode::PRE_ISP_SDR || current == Mode::PRE_ISP_DENOISE;
    const bool target_eligible = desc.target_mode == Mode::PRE_ISP_SDR || desc.target_mode == Mode::PRE_ISP_DENOISE;
    return current_eligible && target_eligible;
}

bool IspManager::switch_mode(const ModeDescriptor &desc, const frontend_config_t &frontend_config)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Switching to mode {}", to_string(desc.target_mode));

    const bool fast_denoise_path = is_fast_denoise_switch_transition(desc);

    m_fast_toggle_mode = get_fast_toggle_mode(desc.target_mode);
    if (!prepare_for_fast_toggle(m_fast_toggle_mode))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to prepare for fast toggle");
        return false;
    }

    if (!setup_sensor_and_isp(desc, frontend_config))
        return false;

    if (!setup_devices(desc, frontend_config))
        return false;

    if (desc.is_hdr)
    {
        m_ls_ratio = frontend_config.hdr_config.ls_ratio;
        m_vs_ratio = frontend_config.hdr_config.vs_ratio;
    }

    m_current_mode = desc.target_mode;
    m_current_mcm_mode = desc.mcm_mode;

    if (fast_denoise_path)
    {
        if (desc.target_mode == Mode::PRE_ISP_SDR)
        {
            m_pending_bls_value = 200;
            m_arm_bls_for_next_raw = true;
        }
        else if (desc.target_mode == Mode::PRE_ISP_DENOISE)
        {
            m_pending_bls_value = 0;
            m_arm_bls_for_next_raw = true;
        }
    }

    if (m_fast_toggle_mode != FastToggleMode::OFF && !fast_denoise_path)
        start();

    m_fast_toggle_mode = FastToggleMode::OFF;
    m_mode_cv.notify_all();
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
    if (frontend_config.denoise_config.fast_denoise_switch)
    {
        return false;
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

    if (buffer_type == BufferType::RAW_CAPTURE)
    {
        hailo_buffer->isp_timestamp_ns = timeval_to_ns(v4l2_data->timestamp);
    }

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

        if (m_arm_bls_for_next_raw.exchange(false))
        {
            hailo_buffer->pending_bls_value = m_pending_bls_value;
        }

        if (m_fast_toggle_mode != FastToggleMode::OFF)
        {
            m_config_attacher->attach_fallback_config(hailo_buffer);
        }
        else
        {
            m_config_attacher->attach_config(hailo_buffer);
        }

        auto subscriber_id = active_subscriber_for_mode(m_current_mode.load());
        if (!subscriber_id.has_value())
        {
            continue;
        }
        auto it = m_raw_buffer_subscribers.find(*subscriber_id);
        if (it == m_raw_buffer_subscribers.end() || !it->second)
        {
            continue;
        }
        it->second(hailo_buffer);
    }
}

std::optional<IspManager::SubscriberId> IspManager::active_subscriber_for_mode(Mode mode) const
{
    switch (mode)
    {
    case Mode::PRE_ISP_SDR:
        return static_cast<int>(LoggerType::Isp);
    case Mode::PRE_ISP_DENOISE:
        return static_cast<int>(LoggerType::Denoise);
    case Mode::HDR_NNCORE_STITCH:
        return static_cast<int>(LoggerType::Hdr);
    case Mode::HDR_DENOISE:
        return static_cast<int>(LoggerType::Denoise);
    default:
        return std::nullopt;
    }
}

void IspManager::run_passthrough(HailoMediaLibraryBufferPtr raw_buffer)
{
    if (!m_isp_in_device || !m_raw_capture_device)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Passthrough: devices not initialized");
        return;
    }

    constexpr bool is_packed_isp_input = false;
    auto isp_input_opt = get_isp_input_buffer(is_packed_isp_input);
    if (!isp_input_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Passthrough: failed to acquire ISP input slot");
        return;
    }
    HailoMediaLibraryBufferPtr isp_input_buffer = isp_input_opt.value();
    auto *slot = static_cast<HDR::VideoBuffer *>(isp_input_buffer->get_on_free_data());

    auto *raw_video_buffer = static_cast<HDR::VideoBuffer *>(raw_buffer->get_on_free_data());
    if (!raw_video_buffer)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Passthrough: raw buffer has no underlying VideoBuffer");
        return;
    }

    const uint32_t plane_count = raw_buffer->get_num_of_planes();
    if (plane_count != isp_input_buffer->get_num_of_planes())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Passthrough: plane count mismatch (raw: {}, isp_in: {})", plane_count,
                              isp_input_buffer->get_num_of_planes());
        return;
    }

    slot->swap_plane_fds_with(*raw_video_buffer);
    for (uint32_t i = 0; i < plane_count; ++i)
    {
        std::swap(isp_input_buffer->buffer_data->planes[i].fd, raw_buffer->buffer_data->planes[i].fd);
    }

    isp_input_buffer->pending_bls_value = raw_buffer->pending_bls_value;
    isp_input_buffer->isp_timestamp_ns = raw_buffer->isp_timestamp_ns;
    isp_input_buffer->attach_profile(raw_buffer->get_attached_profile());

    if (!put_buffer_into_isp(isp_input_buffer))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Passthrough: failed to submit ISP input slot");
        slot->swap_plane_fds_with(*raw_video_buffer);
        for (uint32_t i = 0; i < plane_count; ++i)
        {
            std::swap(isp_input_buffer->buffer_data->planes[i].fd, raw_buffer->buffer_data->planes[i].fd);
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
    const bool in_hdr_stitch_mode = m_current_mode == Mode::HDR_NNCORE_STITCH || m_current_mode == Mode::HDR_ISP_STITCH;

    if (!hdr_enabled && !denoise_enabled)
    {
        m_fast_denoise_switch = frontend_config.denoise_config.fast_denoise_switch;
    }

    if (m_fast_denoise_switch && denoise_enabled && is_input_isp_frame_packed(frontend_config))
    {
        LOGGER__MODULE__WARNING(MODULE_NAME, "fast_denoise_switch is enabled but the denoise profile is VD (packed); "
                                             "falling back to normal mode switching");
        m_fast_denoise_switch = false;
    }

    if (!hdr_enabled && denoise_enabled && m_current_mode == Mode::PRE_ISP_DENOISE && m_is_started)
    {
        const auto target_mcm = is_input_isp_frame_packed(frontend_config) ? isp_utils::ISP_MCM_MODE_PACKED
                                                                           : isp_utils::ISP_MCM_MODE_INJECTION;
        if (target_mcm != m_current_mcm_mode)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "Switching between VD and HDM denoise methods at runtime is not supported");
            return false;
        }
    }

    if (hdr_enabled && denoise_enabled && needs_mode_switch(m_current_mode == Mode::HDR_DENOISE, m_is_started))
    {
        if (is_input_isp_frame_packed(frontend_config))
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Setting up isp input device for HDR in packed mode is not supported");
            return false;
        }
        if (HdrManager::get_stitch_mode() == StitchMode::NNCORE)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "HDR denoise is not supported for this platform");
            return false;
        }
        return switch_mode(HDR_DENOISE_DESCRIPTOR, frontend_config);
    }
    else if (hdr_enabled && !denoise_enabled && needs_mode_switch(in_hdr_stitch_mode, m_is_started))
    {
        auto stitch_mode = HdrManager::get_stitch_mode();
        if (stitch_mode != StitchMode::NNCORE && stitch_mode != StitchMode::ISP)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Unsupported stitch mode");
            return false;
        }
        const auto &desc =
            (stitch_mode == StitchMode::NNCORE) ? HDR_NNCORE_STITCH_DESCRIPTOR : HDR_ISP_STITCH_DESCRIPTOR;
        return switch_mode(desc, frontend_config);
    }
    else if (!hdr_enabled && denoise_enabled &&
             needs_mode_switch(m_current_mode == Mode::PRE_ISP_DENOISE, m_is_started))
    {
        const auto &desc =
            is_input_isp_frame_packed(frontend_config) ? PRE_ISP_DENOISE_VD_DESCRIPTOR : PRE_ISP_DENOISE_HDM_DESCRIPTOR;
        return switch_mode(desc, frontend_config);
    }
    else if (!hdr_enabled && !denoise_enabled)
    {
        const bool dual = m_config_manager_interactor->is_dual_sensor();
        const bool want_pre_isp_sdr = m_fast_denoise_switch && !dual;
        const Mode target_sdr_mode = want_pre_isp_sdr ? Mode::PRE_ISP_SDR : Mode::SDR;
        if (needs_mode_switch(m_current_mode == target_sdr_mode, m_is_started))
        {
            if (want_pre_isp_sdr)
            {
                return switch_mode(SDR_FAST_DENOISE_DESCRIPTOR, frontend_config);
            }
            const auto &desc = dual ? SDR_DUAL_SENSOR_DESCRIPTOR : SDR_DESCRIPTOR;
            return switch_mode(desc, frontend_config);
        }
    }

    if (is_current_mode_hdr() && hdr_ratios_changed)
    {
        return set_hdr_ratios(frontend_config.hdr_config.ls_ratio, frontend_config.hdr_config.vs_ratio);
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Set Config called, but No mode switch required");
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

    if (m_is_started && is_current_mode_using_pre_isp_pipeline())
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Disabling ISP_FORWARD_TIMESTAMPS");
        if (!m_v4l2_ctrl_manager.ctrl_set(v4l2::Video0Ctrl::ISP_FORWARD_TIMESTAMPS, false))
        {
            LOGGER__MODULE__WARNING(MODULE_NAME, "Failed to disable ISP_FORWARD_TIMESTAMPS");
        }
    }

    m_is_started = false;
}

void IspManager::deinit()
{
    stop();

    m_fast_denoise_switch = false;

    restore_isp_config_files_to_default();

    frontend_config_t sdr_config{};
    sdr_config.input_config.resolution = m_input_resolution;
    bool dual = m_config_manager_interactor && m_config_manager_interactor->is_dual_sensor();
    const auto &desc = dual ? SDR_DUAL_SENSOR_DESCRIPTOR : SDR_DESCRIPTOR;
    switch_mode(desc, sdr_config);

    m_raw_capture_device = nullptr;
    m_isp_in_device = nullptr;

    unset_subscriber(static_cast<int>(MODULE_NAME));

    std::unique_lock<std::mutex> lock(m_mode_mutex);
    m_raw_buffer_subscribers.clear();
    m_current_mode = Mode::UNKNOWN;
    m_current_mcm_mode = isp_utils::ISP_MCM_MODE_OFF;
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

        // Tell the kernel to copy the raw-capture buffer timestamp onto the ISP output buffer
        // instead of stamping "now". Required for EIS.
        LOGGER__MODULE__INFO(MODULE_NAME, "Enabling ISP_FORWARD_TIMESTAMPS");
        if (!m_v4l2_ctrl_manager.ctrl_set(v4l2::Video0Ctrl::ISP_FORWARD_TIMESTAMPS, true))
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to enable ISP_FORWARD_TIMESTAMPS");
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

v4l2::ImxCtrl IspManager::get_custom_rhs1_ctrl_type()
{
    if (m_fast_toggle_mode != FastToggleMode::OFF)
    {
        return v4l2::ImxCtrl::CUSTOM_RHS1_FAST_TOGGLE;
    }
    return v4l2::ImxCtrl::CUSTOM_RHS1;
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
    if (!m_v4l2_ctrl_manager.ext_ctrl_set(get_custom_rhs1_ctrl_type(), custom_rhs1))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set CUSTOM_RHS1");
        return false;
    }
    return true;
}

bool IspManager::is_current_mode_using_pre_isp_pipeline()
{
    return m_current_mode == Mode::PRE_ISP_DENOISE || m_current_mode == Mode::HDR_DENOISE ||
           m_current_mode == Mode::HDR_NNCORE_STITCH || m_current_mode == Mode::PRE_ISP_SDR;
}

bool IspManager::is_current_mode_hdr()
{
    return m_current_mode == Mode::HDR_NNCORE_STITCH || m_current_mode == Mode::HDR_ISP_STITCH ||
           m_current_mode == Mode::HDR_DENOISE;
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

    auto pixel_format = registry.get_pixel_format(sensor_index);
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

    auto pixel_format = registry.get_pixel_format(frontend_config.input_config.sensor_index);
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
    HDR::VideoBuffer *isp_buffer = static_cast<HDR::VideoBuffer *>(buffer_to_insert_into_isp->get_on_free_data());

    isp_buffer->get_v4l2_buffer()->timestamp = ns_to_timeval(buffer_to_insert_into_isp->isp_timestamp_ns);

    if (buffer_to_insert_into_isp->pending_bls_value.has_value())
    {
        apply_pending_bls(buffer_to_insert_into_isp->pending_bls_value.value());
        buffer_to_insert_into_isp->pending_bls_value.reset();
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

void IspManager::apply_pending_bls(int bls_value)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Applying BLS controls on first ISP injection: value={}", bls_value);
    using v4l2::Video0Ctrl;

    constexpr bool BLS_MODE_USE_FIXED_VALUES = false;
    if (!m_v4l2_ctrl_manager.ext_ctrl_set(Video0Ctrl::BLS_MODE, BLS_MODE_USE_FIXED_VALUES))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set BLS_MODE");
    }
    auto bls_mode_readback = m_v4l2_ctrl_manager.ext_ctrl_get<bool, Video0Ctrl>(Video0Ctrl::BLS_MODE);
    if (!bls_mode_readback.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "BLS_MODE readback failed");
    }
    else
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "BLS readback ctrl {} = {} (expected {})",
                             static_cast<int>(Video0Ctrl::BLS_MODE), static_cast<int>(bls_mode_readback.value()),
                             static_cast<int>(BLS_MODE_USE_FIXED_VALUES));
        assert(bls_mode_readback.value() == BLS_MODE_USE_FIXED_VALUES);
    }

    uint16_t bls_value16 = static_cast<uint16_t>(bls_value);
    static constexpr std::array<Video0Ctrl, 4> BLS_CTRLS = {
        Video0Ctrl::BLS_RED,
        Video0Ctrl::BLS_GREEN_RED,
        Video0Ctrl::BLS_GREEN_BLUE,
        Video0Ctrl::BLS_BLUE,
    };
    for (auto ctrl : BLS_CTRLS)
    {
        if (!m_v4l2_ctrl_manager.ext_ctrl_set(ctrl, &bls_value16))
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set BLS control {}", static_cast<int>(ctrl));
            continue;
        }
        auto rb = m_v4l2_ctrl_manager.ext_ctrl_get<uint16_t *, Video0Ctrl>(ctrl);
        if (!rb.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "BLS readback failed for ctrl {}", static_cast<int>(ctrl));
            continue;
        }
        LOGGER__MODULE__INFO(MODULE_NAME, "BLS readback ctrl {} = {} (expected {})", static_cast<int>(ctrl), rb.value(),
                             bls_value16);
    }
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

    cloexec::ofstream file(file_path);
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
