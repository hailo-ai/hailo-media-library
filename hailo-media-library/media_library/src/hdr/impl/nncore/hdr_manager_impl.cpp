#include "hdr_manager_impl.hpp"

#include <string.h>
#include <filesystem>
#include <optional>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "buffer_pool.hpp"
#include "hdr_manager.hpp"
#include "hrt_stitcher/hrt_stitcher.hpp"
#include "isp_manager.hpp"
#include "media_library_types.hpp"
#include "sensor_registry.hpp"
#include "v4l2_ctrl.hpp"
#include "hailo_media_library_perfetto.hpp"

#define MODULE_NAME LoggerType::Hdr

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
    return "/usr/lib/medialib/hdr_config/hdr_" + resolution_str + "_" + dol_str + "dol_h15h.hef";
}

bool HdrManager::Impl::configure(const frontend_config_t &frontend_config)
{
    auto dol = frontend_config.hdr_config.dol;
    auto input_resolution_as_struct = frontend_config.input_config.resolution;

    if (m_initialized && dol == m_dol && input_resolution_as_struct.dimensions_equal(m_input_resolution))
    {
        return true;
    }

    auto input_resolution = SensorRegistry::get_instance().detect_resolution(input_resolution_as_struct);

    if (!input_resolution.has_value())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to detect resolution from input resolution");
        return false;
    }

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

    auto &registry = SensorRegistry::get_instance();
    auto pixelFormat = registry.get_pixel_format();

    if (!pixelFormat.has_value())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to get sensor pixel format");
        return false;
    }

    std::unique_ptr<HailortAsyncStitching> stitcher = std::make_unique<HailortAsyncStitching>();
    if (!stitcher)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to create HailortAsyncStitching instance");
        return false;
    }
    if (stitcher->init(hdr_hef_path.value(), frontend_config.hailort_config.device_id, SCHEDULER_THRESHOLD,
                       SCHEDULER_TIMEOUT.count(), dol, frontend_config.hailort_config.use_hailort_service))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to initialize HailortAsyncStitching with HEF path: {}",
                              hdr_hef_path.value());
        return false;
    }
    stitcher->set_on_infer_finish([this](std::shared_ptr<void> ptr) { on_infer(std::move(ptr)); });

    HDR::DMABufferAllocator allocator;
    if (!allocator.init(DMA_HEAP_PATH))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to initialize DMABufferAllocator with heap path: {}", DMA_HEAP_PATH);
        return false;
    }
    auto stitch_contexts_opt = alloc_stitch_contexts(allocator, dol * HdrManager::Impl::CFA_NUM_CHANNELS);
    if (!stitch_contexts_opt)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to allocate stitch contexts");
        return false;
    }

    m_current_resolution = input_resolution.value();
    m_stitch_contexts = std::move(stitch_contexts_opt.value());
    m_dol = dol;
    m_stitcher = std::move(stitcher);
    m_input_resolution = input_resolution_as_struct;
    m_initialized = true;

    LOGGER__MODULE__INFO(LOGGER_TYPE, "HdrManager initialized successfully");
    return true;
}

HdrManager::Impl::Impl(IspManager &isp_manager) : m_isp_manager(isp_manager)
{
    m_isp_manager.set_subscriber(static_cast<int>(MODULE_NAME), [this](HailoMediaLibraryBufferPtr raw_buffer) {
        return this->handle_frame(raw_buffer);
    });
}

HdrManager::Impl::~Impl()
{
    deinit();
    m_isp_manager.unset_subscriber(static_cast<int>(MODULE_NAME));
}

void HdrManager::Impl::deinit()
{
    free_stitch_contexts();
    m_wb_clipping_warned = false;
    m_initialized = false;
}

std::optional<std::vector<HdrManager::Impl::StitchContextPtr>> HdrManager::Impl::alloc_stitch_contexts(
    HDR::DMABufferAllocator &allocator, int wb_buffer_size)
{
    // we want an extra context so even when there are no buffers we can already have a context ready
    free_stitch_contexts();
    std::vector<StitchContextPtr> stitch_contexts;
    stitch_contexts.resize(std::min(IspManager::RAW_CAPTURE_BUFFERS_COUNT, IspManager::ISP_IN_BUFFERS_COUNT) + 1);
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

void HdrManager::Impl::mark_stitch_context_unused(HdrManager::Impl::StitchContextPtr context)
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

    if (!wb_buffer)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "WB buffer pointer is null");
        return false;
    }

    const int bayer_pattern_order_rggb[4] = {0, 1, 2, 3};
    const int bayer_pattern_order_gbrg[4] = {2, 3, 0, 1};
    const int *bayer_pattern_order = nullptr;

    std::optional<int> pix_fmt_opt = m_isp_manager.get_raw_capture_pix_format();
    if (!pix_fmt_opt.has_value())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to get raw capture pixel format");
        return false;
    }
    switch (pix_fmt_opt.value())
    {
    case V4L2_PIX_FMT_SRGGB12:
        bayer_pattern_order = bayer_pattern_order_rggb;
        break;
    case V4L2_PIX_FMT_SGBRG12:
        bayer_pattern_order = bayer_pattern_order_gbrg;
        break;
    default:
        // we should never arrive here since we check isSupportedFormat() before
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Unsupported pixel format for WB gains update");
        return false;
    }

    auto wb_r_gain = m_isp_manager.m_v4l2_ctrl_manager.ext_ctrl_get<int, v4l2::Video0Ctrl>(v4l2::Video0Ctrl::WB_R_GAIN);
    if (!wb_r_gain.has_value())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to get WB R gain");
        return false;
    }
    auto wb_gr_gain =
        m_isp_manager.m_v4l2_ctrl_manager.ext_ctrl_get<int, v4l2::Video0Ctrl>(v4l2::Video0Ctrl::WB_GR_GAIN);
    if (!wb_gr_gain.has_value())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to get WB GR gain");
        return false;
    }
    auto wb_gb_gain =
        m_isp_manager.m_v4l2_ctrl_manager.ext_ctrl_get<int, v4l2::Video0Ctrl>(v4l2::Video0Ctrl::WB_GB_GAIN);
    if (!wb_gb_gain.has_value())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to get WB GB gain");
        return false;
    }
    auto wb_b_gain = m_isp_manager.m_v4l2_ctrl_manager.ext_ctrl_get<int, v4l2::Video0Ctrl>(v4l2::Video0Ctrl::WB_B_GAIN);
    if (!wb_b_gain.has_value())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to get WB B gain");
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

bool HdrManager::Impl::handle_frame(HailoMediaLibraryBufferPtr raw_buffer)
{
    auto attached_config = raw_buffer->get_attached_profile();
    if (!attached_config->iq_settings.hdr.enabled)
    {
        LOGGER__MODULE__DEBUG(LOGGER_TYPE, "hdr is disabled, continuing without hdr processing");
        return true;
    }
    if (!configure(attached_config->to_frontend_config()))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "HdrManager configuration failed");
        return false;
    }
    std::optional<HailoMediaLibraryBufferPtr> output_frame_opt = m_isp_manager.get_isp_input_buffer();
    if (!output_frame_opt.has_value())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to get ISP input buffer for HDR processing");
        return false;
    }
    auto stitch_context_opt = get_stitch_context();
    if (!stitch_context_opt)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Getting stitch context failed");
        return false;
    }
    auto &stitch_ctx = stitch_context_opt.value();
    HailoMediaLibraryBufferPtr output_frame = output_frame_opt.value();
    output_frame->copy_metadata_from(raw_buffer);

    stitch_ctx->m_stitched_buffer = output_frame;

    HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN("update_wb_gains", HDR_THREADED_TRACK, MEDIA_LIBRARY_CATEGORY);
    update_wb_gains(stitch_ctx->m_wb_buffer);
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(HDR_THREADED_TRACK, MEDIA_LIBRARY_CATEGORY);

    HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN("stitcher.process", HDR_THREADED_TRACK, MEDIA_LIBRARY_CATEGORY);
    int wb_fd = stitch_ctx->m_wb_buffer.get_fd();
    if (m_stitcher->process(raw_buffer, wb_fd, output_frame, std::move(stitch_ctx)) == HAILO_STITCH_SUCCESS)
    {
        ++m_infer_jobs_contexts_queue_size;
    }
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(HDR_THREADED_TRACK, MEDIA_LIBRARY_CATEGORY);

    return true;
}

void HdrManager::Impl::on_infer(std::shared_ptr<void> ptr)
{
    LOGGER__MODULE__TRACE(LOGGER_TYPE, "on_infer beginning");
    StitchContextPtr stitch_ctx = std::static_pointer_cast<StitchContext>(std::move(ptr));
    HailoMediaLibraryBufferPtr stitched_buffer = stitch_ctx->m_stitched_buffer;
    mark_stitch_context_unused(stitch_ctx);
    m_isp_manager.put_buffer_into_isp(stitched_buffer);
    --m_infer_jobs_contexts_queue_size;
}

StitchMode HdrManager::Impl::get_stitch_mode()
{
    return StitchMode::NNCORE;
}
