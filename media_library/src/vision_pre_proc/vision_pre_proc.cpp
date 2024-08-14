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

#include "vision_pre_proc.hpp"
#include "buffer_pool.hpp"
#include "config_manager.hpp"
#include "dewarp_mesh_context.hpp"
#include "dsp_utils.hpp"
#include "media_library_logger.hpp"
#include "media_library_utils.hpp"
#include <iostream>
#include <linux/v4l2-controls.h>
#include <linux/v4l2-subdev.h>
#include <stdint.h>
#include <string>
#include <sys/ioctl.h>
#include <time.h>
#include <tl/expected.hpp>
#include <vector>

#define HAILO15_ISP_CID_LSC_BASE (V4L2_CID_USER_BASE + 0x3200)
#define HAILO15_ISP_CID_LSC_OPTICAL_ZOOM (HAILO15_ISP_CID_LSC_BASE + 0x0009)
#define MAKE_EVEN(value) ((value) % 2 != 0 ? (value) + 1 : (value))

class MediaLibraryVisionPreProc::Impl final
{
public:
    static tl::expected<std::shared_ptr<MediaLibraryVisionPreProc::Impl>, media_library_return>
    create(std::string config_string);
    // Constructor
    Impl(media_library_return &status, std::string config_string);
    // Destructor
    ~Impl();
    // Move constructor
    Impl(Impl &&) = delete;
    // Move assignment
    Impl &operator=(Impl &&) = delete;

    // Configure the pre-processing module with new json string
    media_library_return configure(std::string config_string);

    // Configure the pre-processing module with pre_proc_op_configurations object
    media_library_return configure(pre_proc_op_configurations &pre_proc_op_configs);

    // Perform pre-processing on the input frame and return the output frames
    media_library_return handle_frame(HailoMediaLibraryBufferPtr input_frame, std::vector<HailoMediaLibraryBufferPtr> &output_frames);

    // get the pre-processing configurations object
    pre_proc_op_configurations &get_pre_proc_configs();

    // get the output video configurations object
    output_video_config_t &get_output_video_config();

    // set magnification level of optical zoom
    media_library_return set_optical_zoom(float magnification);

private:
    std::unique_ptr<DewarpMeshContext> m_dewarp_mesh_ctx;
    // configured flag - to determine if first configuration was done
    bool m_configured;
    // frame counter - used internally for matching requested framerate
    uint m_frame_counter;
    // configuration manager
    std::shared_ptr<ConfigManager> m_config_manager;
    // operation configurations
    pre_proc_op_configurations m_pre_proc_configs;
    // input buffer pools
    MediaLibraryBufferPoolPtr m_input_buffer_pool;
    // output buffer pools
    std::vector<MediaLibraryBufferPoolPtr> m_buffer_pools;
    // video fd
    int m_video_fd;
    // configuration mutex
    std::shared_ptr<std::mutex> m_configuration_mutex;

    media_library_return validate_configurations(pre_proc_op_configurations &pre_proc_configs);
    media_library_return decode_config_json_string(pre_proc_op_configurations &pre_proc_configs, std::string config_string);
    media_library_return acquire_output_buffers(HailoMediaLibraryBufferPtr input_buffer, std::vector<HailoMediaLibraryBufferPtr> &buffers);
    media_library_return create_and_initialize_buffer_pools();
    media_library_return validate_input_and_output_frames(HailoMediaLibraryBufferPtr input_frame, std::vector<HailoMediaLibraryBufferPtr> &output_frames);
    media_library_return perform_dewarp(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr dewarp_output_buffer);
    media_library_return perform_multi_resize(HailoMediaLibraryBufferPtr input_buffer, std::vector<HailoMediaLibraryBufferPtr> &output_frames);
    media_library_return perform_dewarp_and_multi_resize(HailoMediaLibraryBufferPtr input_frame, std::vector<HailoMediaLibraryBufferPtr> &output_frames);
    void stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle);
    void increase_frame_counter();
};

//------------------------ MediaLibraryVisionPreProc ------------------------
tl::expected<std::shared_ptr<MediaLibraryVisionPreProc>, media_library_return> MediaLibraryVisionPreProc::create(std::string config_string)
{
    auto impl_expected = Impl::create(config_string);
    if (impl_expected.has_value())
        return std::make_shared<MediaLibraryVisionPreProc>(impl_expected.value());
    else
        return tl::make_unexpected(impl_expected.error());
}

MediaLibraryVisionPreProc::MediaLibraryVisionPreProc(std::shared_ptr<MediaLibraryVisionPreProc::Impl> impl) : m_impl(impl) {}

MediaLibraryVisionPreProc::~MediaLibraryVisionPreProc() = default;

media_library_return MediaLibraryVisionPreProc::configure(std::string config_string)
{
    return m_impl->configure(config_string);
}

media_library_return MediaLibraryVisionPreProc::configure(pre_proc_op_configurations &pre_proc_op_configs)
{
    return m_impl->configure(pre_proc_op_configs);
}

media_library_return MediaLibraryVisionPreProc::handle_frame(HailoMediaLibraryBufferPtr input_frame, std::vector<HailoMediaLibraryBufferPtr> &output_frames)
{
    return m_impl->handle_frame(input_frame, output_frames);
}

pre_proc_op_configurations &MediaLibraryVisionPreProc::get_pre_proc_configs()
{
    return m_impl->get_pre_proc_configs();
}

output_video_config_t &MediaLibraryVisionPreProc::get_output_video_config()
{
    return m_impl->get_output_video_config();
}

media_library_return MediaLibraryVisionPreProc::set_optical_zoom(float magnification)
{
    return m_impl->set_optical_zoom(magnification);
}

//------------------------ MediaLibraryVisionPreProc::Impl ------------------------

tl::expected<std::shared_ptr<MediaLibraryVisionPreProc::Impl>, media_library_return> MediaLibraryVisionPreProc::Impl::create(std::string config_string)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    std::shared_ptr<MediaLibraryVisionPreProc::Impl> vision_preproc = std::make_shared<MediaLibraryVisionPreProc::Impl>(status, config_string);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return vision_preproc;
}

MediaLibraryVisionPreProc::Impl::Impl(media_library_return &status, std::string config_string)
{
    m_configured = false;
    m_video_fd = -1;
    m_configuration_mutex = std::make_shared<std::mutex>();

    // Start frame count from 0 - to make sure we always handle the first frame even if framerate is set to 0
    m_frame_counter = 0;
    m_buffer_pools.reserve(5);
    m_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_VISION);
    m_pre_proc_configs.output_video_config.resolutions.reserve(5);
    if (decode_config_json_string(m_pre_proc_configs, config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode json string");
        status = MEDIA_LIBRARY_INVALID_ARGUMENT;
        return;
    }

    dsp_status dsp_ret = dsp_utils::acquire_device();
    if (dsp_ret != DSP_SUCCESS)
    {
        LOGGER__ERROR("Failed to acquire DSP device, status: {}", dsp_ret);
        status = MEDIA_LIBRARY_OUT_OF_RESOURCES;
        return;
    }

    m_dewarp_mesh_ctx = std::make_unique<DewarpMeshContext>(m_pre_proc_configs);
    if (configure(m_pre_proc_configs) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to configure vision pre proc");
        status = MEDIA_LIBRARY_CONFIGURATION_ERROR;
        return;
    }
    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryVisionPreProc::Impl::~Impl()
{
    m_pre_proc_configs.output_video_config.resolutions.clear();
    m_dewarp_mesh_ctx = nullptr;
    dsp_status status = dsp_utils::release_device();
    if (status != DSP_SUCCESS)
    {
        LOGGER__ERROR("Failed to release DSP device, status: {}", status);
    }
}

media_library_return MediaLibraryVisionPreProc::Impl::decode_config_json_string(pre_proc_op_configurations &pre_proc_configs, std::string config_string)
{
    return m_config_manager->config_string_to_struct<pre_proc_op_configurations>(config_string, pre_proc_configs);
}

media_library_return MediaLibraryVisionPreProc::Impl::configure(std::string config_string)
{
    pre_proc_op_configurations pre_proc_configs;
    LOGGER__INFO("Configuring vision pre proc Decoding json string");
    if (decode_config_json_string(pre_proc_configs, config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return configure(pre_proc_configs);
}

media_library_return MediaLibraryVisionPreProc::Impl::validate_configurations(pre_proc_op_configurations &pre_proc_op_configs)
{
    // Make sure that the output fps of each stream is divided by the input fps with no reminder
    output_resolution_t &input_res = pre_proc_op_configs.input_video_config.resolution;
    for (output_resolution_t &output_res : pre_proc_op_configs.output_video_config.resolutions)
    {
        if (output_res.framerate != 0 && input_res.framerate % output_res.framerate != 0)
        {
            LOGGER__ERROR("Invalid output framerate {} - must be a divider of the input framerate {}", output_res.framerate, input_res.framerate);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    if (!m_pre_proc_configs.dewarp_config.enabled)
    {
        if (m_pre_proc_configs.dis_config.enabled)
            LOGGER__WARNING("DIS feature is enabled in the configuration, but dewarp is disabled. DIS will not be performed");
        if (m_pre_proc_configs.flip_config.enabled)
            LOGGER__WARNING("Flip feature is enabled in the configuration, but dewarp is disabled. Flip will not be performed");
        if (m_pre_proc_configs.rotation_config.enabled)
            LOGGER__WARNING("Rotation feature is enabled in the configuration, but dewarp is disabled. Rotation will not be performed");
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryVisionPreProc::Impl::configure(pre_proc_op_configurations &pre_proc_op_configs)
{
    LOGGER__INFO("Configuring vision pre proc");
    if (validate_configurations(pre_proc_op_configs) != MEDIA_LIBRARY_SUCCESS)
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;

    std::unique_lock<std::mutex> lock(*m_configuration_mutex);

    media_library_return ret = m_pre_proc_configs.update(pre_proc_op_configs);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to update pre proc configurations (prohibited) {}", ret);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    m_dewarp_mesh_ctx->configure(pre_proc_op_configs);
    if (m_pre_proc_configs.dewarp_config.enabled &&
        m_pre_proc_configs.rotation_config.enabled &&
        (m_pre_proc_configs.rotation_config.angle == ROTATION_ANGLE_90 ||
         m_pre_proc_configs.rotation_config.angle == ROTATION_ANGLE_270))
    {
        for (output_resolution_t &output_res : pre_proc_op_configs.output_video_config.resolutions)
        {
            auto w = output_res.dimensions.destination_width;
            auto h = output_res.dimensions.destination_height;
            output_res.dimensions.destination_height = w;
            output_res.dimensions.destination_width = h;
        }
    }

    // Create and initialize buffer pools
    ret = create_and_initialize_buffer_pools();
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    m_configured = true;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryVisionPreProc::Impl::create_and_initialize_buffer_pools()
{
    uint width, height;
    if (m_pre_proc_configs.dewarp_config.enabled) // if dewarp is enabled, use dewarp output dimensions
    {
        width = (uint)m_dewarp_mesh_ctx->m_dewarp_output_width;
        height = (uint)m_dewarp_mesh_ctx->m_dewarp_output_height;
    }
    else // else use input dimensions
    {
        width = (uint)m_pre_proc_configs.input_video_config.resolution.dimensions.destination_width;
        height = (uint)m_pre_proc_configs.input_video_config.resolution.dimensions.destination_height;
    }

    bool configured_already = m_buffer_pools.size() > 0;

    if (configured_already)
    {
        if (m_buffer_pools[0]->get_width() != width ||
            m_buffer_pools[0]->get_height() != height)
        {
            for (MediaLibraryBufferPoolPtr &buffer_pool : m_buffer_pools)
            {
                buffer_pool->swap_width_and_height();   
            }
        }
        
        return MEDIA_LIBRARY_SUCCESS;
    }

    m_buffer_pools.clear();
    m_buffer_pools.reserve(5);

    auto bytes_per_line = dsp_utils::get_dsp_desired_stride_from_width(width);
    m_input_buffer_pool = std::make_shared<MediaLibraryBufferPool>(width, height, m_pre_proc_configs.input_video_config.format, (uint)m_pre_proc_configs.input_video_config.resolution.pool_max_buffers, CMA, bytes_per_line);
    if (m_input_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to init buffer pool");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }
    for (output_resolution_t &output_res : m_pre_proc_configs.output_video_config.resolutions)
    {
        LOGGER__INFO("Creating buffer pool for output resolution: width {} height {} in buffers size of {}", output_res.dimensions.destination_width, output_res.dimensions.destination_height, output_res.pool_max_buffers);
        width = (uint)output_res.dimensions.destination_width;
        bytes_per_line = dsp_utils::get_dsp_desired_stride_from_width(width);
        MediaLibraryBufferPoolPtr buffer_pool = std::make_shared<MediaLibraryBufferPool>(width, (uint)output_res.dimensions.destination_height, m_pre_proc_configs.output_video_config.format, output_res.pool_max_buffers, CMA, bytes_per_line);
        if (buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to init buffer pool");
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
        m_buffer_pools.emplace_back(buffer_pool);
    }
    LOGGER__DEBUG("vision_pre_proc holding {} buffer pools", m_buffer_pools.size());

    return MEDIA_LIBRARY_SUCCESS;
}

/**
 * @brief Acquire output buffers from buffer pools
 *
 * @param[in] input_frame - pointer to the input frame
 * @param[in] buffers - vector of output buffers
 */
media_library_return MediaLibraryVisionPreProc::Impl::acquire_output_buffers(HailoMediaLibraryBufferPtr input_buffer, std::vector<HailoMediaLibraryBufferPtr> &buffers)
{
    // Acquire output buffers
    int32_t isp_ae_fps = input_buffer->isp_ae_fps;
    uint8_t output_size = m_pre_proc_configs.output_video_config.resolutions.size();
    for (uint8_t i = 0; i < output_size; i++)
    {
        uint32_t input_framerate = m_pre_proc_configs.input_video_config.resolution.framerate;
        uint32_t output_framerate = m_pre_proc_configs.output_video_config.resolutions[i].framerate;
        LOGGER__DEBUG("Acquiring buffer {}, target framerate is {}", i, output_framerate);

        uint stream_period = output_framerate == 0 ? 0 : input_framerate / output_framerate;
        bool should_acquire_buffer = stream_period == 0 ? false : (m_frame_counter % stream_period == 0) || (isp_ae_fps != -1 && output_framerate >= static_cast<uint32_t>(isp_ae_fps));
        LOGGER__DEBUG("frame counter is {}, stream period is {}, should acquire buffer is {}", m_frame_counter, stream_period, should_acquire_buffer);

        HailoMediaLibraryBufferPtr buffer = std::make_shared<hailo_media_library_buffer>();

        if (!should_acquire_buffer)
        {
            LOGGER__DEBUG("Skipping current frame to match framerate {}, no need to acquire buffer {}, counter is {}", output_framerate, i, m_frame_counter);
            buffers.emplace_back(buffer);

            continue;
        }

        if (m_buffer_pools[i]->acquire_buffer(buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to acquire buffer");
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
        buffers.emplace_back(buffer);
        LOGGER__DEBUG("buffer acquired successfully");
    }

    return MEDIA_LIBRARY_SUCCESS;
};

/**
 * @brief Perform dewarp
 * Generate dewarp mesh, acquire buffer for dewarp output and perform dewarp on
 * the DSP
 *
 * @param[in] input_frame - pointer to the input frame
 * @param[out] dewarp_output_buffer - dewarp output buffer
 * @param[in] vsm - pointer to the vsm object
 */
media_library_return MediaLibraryVisionPreProc::Impl::perform_dewarp(
    HailoMediaLibraryBufferPtr input_buffer,
    HailoMediaLibraryBufferPtr dewarp_output_buffer)
{
    struct timespec start_dewarp, end_dewarp;

    // Acquire buffer for dewarp output
    if (m_input_buffer_pool->acquire_buffer(dewarp_output_buffer) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        // log: failed to acquire buffer for dewarp output
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    // Perform dewarp
    dsp_dewarp_mesh_t *mesh = m_dewarp_mesh_ctx->get();
    LOGGER__TRACE("Performing dewarp with mesh (w={}, h={}) interpolation type {}", mesh->mesh_width, mesh->mesh_height, m_pre_proc_configs.dewarp_config.interpolation_type);
    clock_gettime(CLOCK_MONOTONIC, &start_dewarp);
    dsp_status ret = dsp_utils::perform_dsp_dewarp(
        input_buffer->hailo_pix_buffer.get(),
        dewarp_output_buffer->hailo_pix_buffer.get(),
        mesh,
        m_pre_proc_configs.dewarp_config.interpolation_type);
    clock_gettime(CLOCK_MONOTONIC, &end_dewarp);
    [[maybe_unused]] long ms = (long)media_library_difftimespec_ms(end_dewarp, start_dewarp);
    LOGGER__TRACE("perform_dsp_dewarp took {} milliseconds ({} fps)", ms, (1000 / ms));

    if (ret != DSP_SUCCESS)
        return MEDIA_LIBRARY_DSP_OPERATION_ERROR;

    return MEDIA_LIBRARY_SUCCESS;
}

/**
 * @brief Perform multi resize on the DSP
 *
 * @param[in] input_frame - pointer to the input frame
 * @param[out] output_frames - vector of output frames
 */
media_library_return MediaLibraryVisionPreProc::Impl::perform_multi_resize(
    HailoMediaLibraryBufferPtr input_buffer,
    std::vector<HailoMediaLibraryBufferPtr> &output_frames)
{
    struct timespec start_resize, end_resize;
    size_t output_frames_size = output_frames.size();
    size_t num_of_output_resolutions = m_pre_proc_configs.output_video_config.resolutions.size();
    if (num_of_output_resolutions != output_frames_size)
    {
        LOGGER__ERROR("Number of output resolutions ({}) does not match number of output frames ({})", num_of_output_resolutions, output_frames_size);
        return MEDIA_LIBRARY_ERROR;
    }

    dsp_crop_resize_params_t crop_resize_params;
    dsp_multi_crop_resize_params_t multi_crop_resize_params = {
        .src = input_buffer->hailo_pix_buffer.get(),
        .crop_resize_params = &crop_resize_params,
        .crop_resize_params_count = 1,
        .interpolation = m_pre_proc_configs.output_video_config.interpolation_type,
    };

    uint num_bufs_to_resize = 0;
    for (size_t i = 0; i < num_of_output_resolutions; i++)
    {
        // TODO: Handle cases where its nullptr
        if (output_frames[i]->hailo_pix_buffer == nullptr)
        {
            LOGGER__DEBUG("Skipping resize for output frame {} to match target framerate", i);
            continue;
        }
        dsp_image_properties_t *output_frame = output_frames[i]->hailo_pix_buffer.get();
        output_resolution_t &output_res = m_pre_proc_configs.output_video_config.resolutions[i];

        if (output_res != *output_frame)
        {
            LOGGER__ERROR("Invalid output frame width {} output frame height {}", output_frame->width, output_frame->height);
            return MEDIA_LIBRARY_ERROR;
        }

        crop_resize_params.dst[num_bufs_to_resize] = output_frame;
        LOGGER__DEBUG("Multi resize output frame ({}) - y_ptr = {}, uv_ptr = {}. dims: width {} output frame height {}", i, fmt::ptr(output_frame->planes[0].userptr), fmt::ptr(output_frame->planes[1].userptr), output_frame->width, output_frame->height);
        num_bufs_to_resize++;
    }

    if (num_bufs_to_resize == 0)
    {
        LOGGER__DEBUG("No need to perform multi resize");
        return MEDIA_LIBRARY_SUCCESS;
    }

    uint start_x = 0;
    uint start_y = 0;
    uint end_x = (uint)m_pre_proc_configs.input_video_config.resolution.dimensions.destination_width;
    uint end_y = (uint)m_pre_proc_configs.input_video_config.resolution.dimensions.destination_height;

    // if dewarp is enabled, resulotion may change, use dewarp output dimensions
    if (m_pre_proc_configs.dewarp_config.enabled)
    {
        end_x = (uint)m_dewarp_mesh_ctx->m_dewarp_output_width;
        end_y = (uint)m_dewarp_mesh_ctx->m_dewarp_output_height;
    }

    if (m_pre_proc_configs.digital_zoom_config.enabled)
    {
        if (m_pre_proc_configs.digital_zoom_config.mode ==
            DIGITAL_ZOOM_MODE_MAGNIFICATION)
        {
            uint center_x = end_x / 2;
            uint center_y = end_y / 2;
            uint zoom_width = center_x / m_pre_proc_configs.digital_zoom_config.magnification;
            uint zoom_height = center_y / m_pre_proc_configs.digital_zoom_config.magnification;
            start_x = MAKE_EVEN(center_x - zoom_width);
            start_y = MAKE_EVEN(center_y - zoom_height);
            end_x = MAKE_EVEN(center_x + zoom_width);
            end_y = MAKE_EVEN(center_y + zoom_height);
        }
        else
        {
            roi_t &digital_zoom_roi = m_pre_proc_configs.digital_zoom_config.roi;
            start_x = MAKE_EVEN(digital_zoom_roi.x);
            start_y = MAKE_EVEN(digital_zoom_roi.y);
            end_x = MAKE_EVEN(start_x + digital_zoom_roi.width);
            end_y = MAKE_EVEN(start_y + digital_zoom_roi.height);

            // Validate digital zoom ROI values with the input frame dimensions
            if (end_x > m_dewarp_mesh_ctx->m_dewarp_output_width)
            {
                LOGGER__ERROR("Invalid digital zoom ROI. X ({}) and width ({}) coordinates exceed input frame width ({})", start_x, digital_zoom_roi.width, m_dewarp_mesh_ctx->m_dewarp_output_width);
                return MEDIA_LIBRARY_ERROR;
            }

            if (end_y > m_dewarp_mesh_ctx->m_dewarp_output_height)
            {
                LOGGER__ERROR("Invalid digital zoom ROI. Y ({}) and height ({}) coordinates exceed input frame height ({})", start_y, digital_zoom_roi.height, m_dewarp_mesh_ctx->m_dewarp_output_height);
                return MEDIA_LIBRARY_ERROR;
            }
        }
    }

    // Perform multi resize
    LOGGER__DEBUG("Performing multi resize on the DSP with digital zoom ROI: start_x {} start_y {} end_x {} end_y {}", start_x, start_y, end_x, end_y);
    dsp_roi_t crop = {
        .start_x = start_x,
        .start_y = start_y,
        .end_x = end_x,
        .end_y = end_y
    };
    crop_resize_params.crop = &crop;
    clock_gettime(CLOCK_MONOTONIC, &start_resize);
    dsp_status ret = dsp_utils::perform_dsp_multi_resize(&multi_crop_resize_params);
    clock_gettime(CLOCK_MONOTONIC, &end_resize);
    [[maybe_unused]] long ms = (long)media_library_difftimespec_ms(end_resize, start_resize);
    LOGGER__TRACE("perform_multi_resize took {} milliseconds ({} fps)", ms, 1000 / ms);

    if (ret != DSP_SUCCESS)
        return MEDIA_LIBRARY_DSP_OPERATION_ERROR;

    return MEDIA_LIBRARY_SUCCESS;
}

void MediaLibraryVisionPreProc::Impl::stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle)
{
    clock_gettime(CLOCK_MONOTONIC, &end_handle);
    long ms = (long)media_library_difftimespec_ms(end_handle, start_handle);
    uint framerate = 1000 / ms;
    LOGGER__DEBUG("handle_frame took {} milliseconds ({} fps)", ms, framerate);
}

void MediaLibraryVisionPreProc::Impl::increase_frame_counter()
{
    // Increase frame counter or reset it to 1
    m_frame_counter = (m_frame_counter == 60) ? 1 : m_frame_counter + 1;
}

media_library_return
MediaLibraryVisionPreProc::Impl::perform_dewarp_and_multi_resize(
    HailoMediaLibraryBufferPtr input_frame,
    std::vector<HailoMediaLibraryBufferPtr> &output_frames)
{
    // Perform dewarp and multi resize
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    HailoMediaLibraryBufferPtr dewarp_output_buffer = std::make_shared<hailo_media_library_buffer>();

    ret = perform_dewarp(input_frame, dewarp_output_buffer);

    if (m_pre_proc_configs.output_video_config.grayscale)
    {
        // Saturate UV plane to value of 128 - to get a grayscale image
        dsp_data_plane_t &uv_plane = dewarp_output_buffer->hailo_pix_buffer->planes[1];
        memset(uv_plane.userptr, 128, uv_plane.bytesused);
    }

    if (ret == MEDIA_LIBRARY_SUCCESS)
        ret = perform_multi_resize(dewarp_output_buffer, output_frames);

    return ret;
}

media_library_return
MediaLibraryVisionPreProc::Impl::validate_input_and_output_frames(
    HailoMediaLibraryBufferPtr input_frame,
    std::vector<HailoMediaLibraryBufferPtr> &output_frames)
{
    output_resolution_t &input_res =
        m_pre_proc_configs.input_video_config.resolution;
    dsp_image_properties_t *input_image_properties =
        input_frame->hailo_pix_buffer.get();

    // Check if vector of output buffers is not empty
    if (!output_frames.empty())
    {
        LOGGER__ERROR("output_frames vector is not empty - an empty vector is required");
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    if (m_pre_proc_configs.output_video_config.format != m_pre_proc_configs.input_video_config.format)
    {
        LOGGER__ERROR("Input format {} must be the same as output format {}", m_pre_proc_configs.input_video_config.format, m_pre_proc_configs.output_video_config.format);
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    if (input_res != *input_image_properties)
    {
        LOGGER__ERROR("Invalid input frame width {} input frame height {}", input_image_properties->width, input_image_properties->height);
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    if (m_pre_proc_configs.output_video_config.grayscale)
    {
        if (m_pre_proc_configs.output_video_config.format != DSP_IMAGE_FORMAT_NV12)
        {
            LOGGER__ERROR("Saturate to gray is enabled only for NV12 format");
            return MEDIA_LIBRARY_INVALID_ARGUMENT;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryVisionPreProc::Impl::handle_frame(HailoMediaLibraryBufferPtr input_frame, std::vector<HailoMediaLibraryBufferPtr> &output_frames)
{
    std::unique_lock<std::mutex> lock(*m_configuration_mutex);

    // Stamp start time
    struct timespec start_handle, end_handle;
    clock_gettime(CLOCK_MONOTONIC, &start_handle);

    if (validate_input_and_output_frames(input_frame, output_frames) != MEDIA_LIBRARY_SUCCESS)
    {
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    // Acquire output buffers
    media_library_return media_lib_ret = MEDIA_LIBRARY_SUCCESS;
    media_lib_ret = acquire_output_buffers(input_frame, output_frames);
    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
    {
        return media_lib_ret;
    }

    m_video_fd = input_frame->video_fd;

    // Dewarp and multi resize
    if (m_pre_proc_configs.dewarp_config.enabled)
    {
        if (m_pre_proc_configs.dis_config.enabled && (input_frame->isp_ae_fps > MIN_ISP_AE_FPS_FOR_DIS || input_frame->isp_ae_fps == -1))
            m_dewarp_mesh_ctx->on_frame_vsm_update(input_frame->vsm);
        media_lib_ret = perform_dewarp_and_multi_resize(input_frame, output_frames);
    }
    else
    {
        if (m_pre_proc_configs.output_video_config.grayscale)
        {
            // Saturate UV plane to value of 128 - to get a grayscale image
            dsp_data_plane_t &uv_plane = input_frame->hailo_pix_buffer->planes[1];
            memset(uv_plane.userptr, 128, uv_plane.bytesused);
        }
        media_lib_ret = perform_multi_resize(input_frame, output_frames);
    }

    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
        return media_lib_ret;

    increase_frame_counter();

    stamp_time_and_log_fps(start_handle, end_handle);

    return MEDIA_LIBRARY_SUCCESS;
}

pre_proc_op_configurations &MediaLibraryVisionPreProc::Impl::get_pre_proc_configs()
{
    return m_pre_proc_configs;
}

output_video_config_t &MediaLibraryVisionPreProc::Impl::get_output_video_config()
{
    return m_pre_proc_configs.output_video_config;
}

media_library_return MediaLibraryVisionPreProc::Impl::set_optical_zoom(float magnification)
{
    struct v4l2_control ctrl;

    if (!m_pre_proc_configs.optical_zoom_config.enabled)
    {
        LOGGER__ERROR("optical zoom is disabled in configuration");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    m_dewarp_mesh_ctx->set_optical_zoom(magnification);

    if (m_video_fd != -1)
    {
        memset(&ctrl, 0, sizeof(ctrl));
        ctrl.id = HAILO15_ISP_CID_LSC_OPTICAL_ZOOM;
        ctrl.value = static_cast<int>(magnification * 100);
        if (ioctl(m_video_fd, VIDIOC_S_CTRL, &ctrl))
        {
            LOGGER__ERROR("Could not update v4l2-ctl about new optical zoom");
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }
    else
    {
        LOGGER__WARNING("video fd is not initialized, skipping v4l2-ctl update");
    }

    return MEDIA_LIBRARY_SUCCESS;
}