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
 * included in all copies or substantial portions of the Software.  * * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT
 * WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR
 * A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "pre_isp_denoise.hpp"
#include "hailort_denoise.hpp"
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

media_library_return free_buffer_pool(MediaLibraryBufferPoolPtr &buffer_pool, const std::string &buffer_pool_name)
{
    if (buffer_pool != nullptr)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Waiting for {} buffer pool to release used buffers", buffer_pool_name);
        if (buffer_pool->wait_for_used_buffers() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to wait for {} used buffers to be released", buffer_pool_name);
            return media_library_return::MEDIA_LIBRARY_ERROR;
        }

        LOGGER__MODULE__DEBUG(MODULE_NAME, "Freeing {} buffer pool", buffer_pool_name);
        if (buffer_pool->free() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to free {} buffer pool", buffer_pool_name);
            return media_library_return::MEDIA_LIBRARY_ERROR;
        }
        buffer_pool = nullptr;
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return initialize_buffer_pool(const std::string &buffer_pool_name, int width, int height,
                                            int buffers_size, HailoFormat format,
                                            MediaLibraryBufferPoolPtr &buffer_pool)
{
    // Create and initialize a buffer pool
    LOGGER__MODULE__DEBUG(MODULE_NAME,
                          "Initalizing buffer pool named {} for resolution: width {} height {} in buffers size of {}",
                          buffer_pool_name, width, height, buffers_size);

    if (buffer_pool == nullptr)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Creating buffer pool - {}x{}, {} buffers", width, height, buffers_size);
        buffer_pool = std::make_shared<MediaLibraryBufferPool>(width, height, format, buffers_size,
                                                               HAILO_MEMORY_TYPE_DMABUF, buffer_pool_name);
    }
    if (buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to initialize buffer pool {}", buffer_pool_name);
        return media_library_return::MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Buffer pool {} initialized successfully", buffer_pool_name);
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return set_dummy_output_buffer(MediaLibraryBufferPoolPtr buffer_pool,
                                             NetworkInferenceBindingsPtr bindings, int index)
{
    HailoMediaLibraryBufferPtr dummy_output_buffer = std::make_shared<hailo_media_library_buffer>();
    auto ret = buffer_pool->acquire_buffer(dummy_output_buffer);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to acquire dummy output buffer");
        return media_library_return::MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }
    std::memset(dummy_output_buffer->get_plane_ptr(0), 0, dummy_output_buffer->get_plane_size(0));
    bind_output_buffer(bindings, index, dummy_output_buffer);
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

void timestamp_buffer_if_missing(HailoMediaLibraryBufferPtr hailo_buffer_raw)
{
    if (hailo_buffer_raw->isp_timestamp_ns == 0)
    {
        // Timestamp is needed when checking if there are pending jobs
        const auto now_time = std::chrono::system_clock::now();
        hailo_buffer_raw->isp_timestamp_ns =
            std::chrono::time_point_cast<std::chrono::nanoseconds>(now_time).time_since_epoch().count();
    }
}

// MediaLibraryPreIspDenoise implementation
MediaLibraryPreIspDenoise::MediaLibraryPreIspDenoise(IspManager &isp_manager)
    : MediaLibraryDenoise(), m_isp_manager(isp_manager)
{
    // Create default VD mode HailoRT instance
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Creating default VD mode HailoRT instance");
    m_hailort_denoise = std::make_unique<HailortAsyncDenoisePreISPVd>(
        [this](NetworkInferenceBindingsPtr bindings) { inference_callback(bindings); });

    MediaLibraryDenoise::callbacks_t callbacks;
    callbacks.on_buffer_ready = [this](HailoMediaLibraryBufferPtr output_buffer) {
        m_isp_manager.put_buffer_into_isp(output_buffer);
    };
    observe(callbacks);

    m_isp_manager.set_subscriber(static_cast<int>(MODULE_NAME), [this](HailoMediaLibraryBufferPtr raw_buffer) {
        timestamp_buffer_if_missing(raw_buffer);
        return this->handle_frame(raw_buffer);
    });
}

MediaLibraryPreIspDenoise::~MediaLibraryPreIspDenoise()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Pre ISP Denoise - destructor");
    deinit();
    m_isp_manager.unset_subscriber(static_cast<int>(MODULE_NAME));
}

bool MediaLibraryPreIspDenoise::process_inference(NetworkInferenceBindingsPtr bindings)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Processing Pre-ISP denoise inference");
    return m_hailort_denoise->process(std::move(bindings));
}

// overrides

bool MediaLibraryPreIspDenoise::is_enabled(const denoise_config_t &denoise_configs)
{
    return denoise_configs.enabled && denoise_configs.bayer;
}

void MediaLibraryPreIspDenoise::copy_meta(HailoMediaLibraryBufferPtr input_buffer,
                                          HailoMediaLibraryBufferPtr output_buffer)
{
    output_buffer->isp_timestamp_ns = input_buffer->isp_timestamp_ns;
    output_buffer->attach_profile(input_buffer->get_attached_profile());
}

void MediaLibraryPreIspDenoise::prepare_hailort_instance(const denoise_config_t &denoise_configs)
{
    ensure_correct_hailort_instance(denoise_configs);
}

bool MediaLibraryPreIspDenoise::is_packed_output(const denoise_config_t &denoise_configs)
{
    if (determine_hdm_mode(denoise_configs) == HailortAsyncDenoiseType::PreISPHdm)
    {
        return HailortAsyncDenoisePreISPHdm::PACKED_OUTPUT;
    }
    return HailortAsyncDenoisePreISPVd::PACKED_OUTPUT;
}

HailortAsyncDenoiseType MediaLibraryPreIspDenoise::determine_hdm_mode(const denoise_config_t &denoise_configs)
{
    const bool is_hdm = !denoise_configs.bayer_network_config.input_fusion_feedback.empty() &&
                        !denoise_configs.bayer_network_config.output_fusion_feedback.empty() &&
                        !denoise_configs.bayer_network_config.output_gamma_feedback.empty() &&
                        !denoise_configs.bayer_network_config.input_gamma_feedback.empty();
    const bool is_vd = !denoise_configs.bayer_network_config.feedback_bayer_channel.empty();

    if (is_hdm && !is_vd)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Pre-ISP denoise requires HDM mode");
        return HailortAsyncDenoiseType::PreISPHdm;
    }
    else if (!is_hdm && is_vd)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Pre-ISP denoise requires VD mode");
        return HailortAsyncDenoiseType::PreISPVd;
    }
    else
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Invalid denoise configuration - defaulting to VD mode");
        return HailortAsyncDenoiseType::PreISPVd;
    }
}

void MediaLibraryPreIspDenoise::ensure_correct_hailort_instance(const denoise_config_t &denoise_configs)
{
    if (is_enabled(denoise_configs) == false)
    {
        return;
    }

    // Determine required mode based on configuration
    HailortAsyncDenoiseType required_mode = determine_hdm_mode(denoise_configs);

    // Check if we need to switch modes
    if (required_mode == m_hailort_denoise->type())
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "HailoRT instance mode already correct ({})",
                              required_mode == HailortAsyncDenoiseType::PreISPHdm ? "HDM" : "VD");
        return;
    }

    // Switch to the required mode
    if (required_mode == HailortAsyncDenoiseType::PreISPHdm)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Switching to HDM mode HailoRT instance");
        m_hailort_denoise = std::make_unique<HailortAsyncDenoisePreISPHdm>(
            [this](NetworkInferenceBindingsPtr bindings) { inference_callback(bindings); });
    }
    else
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Switching to VD mode HailoRT instance");
        m_hailort_denoise = std::make_unique<HailortAsyncDenoisePreISPVd>(
            [this](NetworkInferenceBindingsPtr bindings) { inference_callback(bindings); });
    }
}

media_library_return MediaLibraryPreIspDenoise::create_and_initialize_buffer_pools(
    const denoise_config_t &denoise_configs, [[maybe_unused]] const input_video_config_t &input_video_configs)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Creating and initializing Pre-ISP denoise buffer pools (mode: {})",
                          m_hailort_denoise->type() == HailortAsyncDenoiseType::PreISPHdm ? "HDM" : "VD");

    // Create HDM-specific buffer pools
    if (m_hailort_denoise->type() == HailortAsyncDenoiseType::PreISPHdm)
    {
        auto fusion_shape =
            m_hailort_denoise->get_input_frame_shape(denoise_configs.bayer_network_config.input_fusion_feedback);
        auto result = initialize_buffer_pool(FUSION_BUFFER_POOL_NAME, fusion_shape.width * fusion_shape.features,
                                             fusion_shape.height, denoise_configs.pool_max_buffers, HAILO_FORMAT_GRAY16,
                                             m_fusion_buffer_pool);
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            return result;
        }
        auto gamma_shape =
            m_hailort_denoise->get_input_frame_shape(denoise_configs.bayer_network_config.input_gamma_feedback);
        result =
            initialize_buffer_pool(GAMMA_BUFFER_POOL_NAME, gamma_shape.width * gamma_shape.features, gamma_shape.height,
                                   denoise_configs.pool_max_buffers, HAILO_FORMAT_GRAY16, m_gamma_buffer_pool);
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            return result;
        }
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Pre-ISP denoise buffer pools created and initialized successfully");
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryPreIspDenoise::free_buffer_pools()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Closing Pre-ISP denoise buffer pools");

    if (m_fusion_buffer_pool == nullptr && m_gamma_buffer_pool == nullptr)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Pre-ISP buffer pools already closed or not initialized");
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    // Free HDM-specific buffer pools
    auto result = free_buffer_pool(m_fusion_buffer_pool, FUSION_BUFFER_POOL_NAME);
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        return result;
    }
    result = free_buffer_pool(m_gamma_buffer_pool, GAMMA_BUFFER_POOL_NAME);
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        return result;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Pre-ISP denoise buffer pools closed successfully");
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryPreIspDenoise::acquire_output_buffers(NetworkInferenceBindingsPtr bindings)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Acquiring output buffer for Pre-ISP denoise");

    std::optional<HailoMediaLibraryBufferPtr> output_frame_opt = m_isp_manager.get_isp_input_buffer(is_packed_output());
    if (!output_frame_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get ISP input buffer for denoise processing");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    HailoMediaLibraryBufferPtr output_buffer = output_frame_opt.value();
    copy_meta(get_output_buffer(bindings, get_denoised_output_index()), output_buffer);
    bind_output_buffer(bindings, get_denoised_output_index(), output_buffer);

    // HDM mode: acquire fusion and gamma buffers
    if (m_hailort_denoise->type() == HailortAsyncDenoiseType::PreISPHdm)
    {
        HailoMediaLibraryBufferPtr dummy_output_fusion_buffer = std::make_shared<hailo_media_library_buffer>();
        auto result = m_fusion_buffer_pool->acquire_buffer(dummy_output_fusion_buffer);
        if (result != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "failed to acquire buffer for pre-isp denoise fusion");
            return result;
        }
        bind_output_buffer(bindings, HailortAsyncDenoisePreISPHdm::OutputIndex::OUTPUT_FUSION_CHANNEL,
                           dummy_output_fusion_buffer);
        if (HailortAsyncDenoisePreISPHdm::is_using_fusion_skips(
                output_buffer->get_attached_profile()->iq_settings.denoise))
        {
            // Output buffers are the inputs for the fusion skips
            bind_skip_input_buffer(bindings, HailortAsyncDenoisePreISPHdm::SkipIndex::SKIP0_FUSION_CHANNEL,
                                   dummy_output_fusion_buffer);
            bind_skip_input_buffer(bindings, HailortAsyncDenoisePreISPHdm::SkipIndex::SKIP1_FUSION_CHANNEL,
                                   dummy_output_fusion_buffer);
        }

        HailoMediaLibraryBufferPtr dummy_output_gamma_buffer = std::make_shared<hailo_media_library_buffer>();
        result = m_gamma_buffer_pool->acquire_buffer(dummy_output_gamma_buffer);
        if (result != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "failed to acquire buffer for pre-isp denoise gamma");
            return result;
        }
        bind_output_buffer(bindings, HailortAsyncDenoisePreISPHdm::OutputIndex::OUTPUT_GAMMA_CHANNEL,
                           dummy_output_gamma_buffer);
    }

    LOGGER__MODULE__TRACE(MODULE_NAME, "Output buffer acquired successfully for Pre-ISP denoise");
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}
