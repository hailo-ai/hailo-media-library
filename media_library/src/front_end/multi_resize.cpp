/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
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

#include "multi_resize.hpp"
#include "buffer_pool.hpp"
#include "config_manager.hpp"
#include "dsp_utils.hpp"
#include "media_library_logger.hpp"
#include "media_library_utils.hpp"
#include <iostream>
#include <stdint.h>
#include <string>
#include <time.h>
#include <tl/expected.hpp>
#include <vector>

class MediaLibraryMultiResize::Impl final
{
public:
    static tl::expected<std::shared_ptr<MediaLibraryMultiResize::Impl>, media_library_return>
    create(std::string config_string);
    // Constructor
    Impl(media_library_return &status, std::string config_string);
    // Destructor
    ~Impl();
    // Move constructor
    Impl(Impl &&) = delete;
    // Move assignment
    Impl &operator=(Impl &&) = delete;

    // Configure the multi-resize module with new json string
    media_library_return configure(std::string config_string);

    // Configure the multi-resize module with multi_resize_config_t object
    media_library_return configure(multi_resize_config_t &mresize_config);

    // Perform multi-resize on the input frame and return the output frames
    media_library_return handle_frame(hailo_media_library_buffer &input_frame, std::vector<hailo_media_library_buffer> &output_frames);

    // get the multi-resize configurations object
    multi_resize_config_t &get_multi_resize_configs();

    // get the output video configurations object
    output_video_config_t &get_output_video_config();

    // set the input video configurations object
    media_library_return set_input_video_config(uint32_t width, uint32_t height, uint32_t framerate);

private:
    // configured flag - to determine if first configuration was done
    bool m_configured;
    // frame counter - used internally for matching requested framerate
    uint m_frame_counter;
    // configuration manager
    std::shared_ptr<ConfigManager> m_config_manager;
    // operation configurations
    multi_resize_config_t m_multi_resize_config;
    // input buffer pools
    MediaLibraryBufferPoolPtr m_input_buffer_pool;
    // output buffer pools
    std::vector<MediaLibraryBufferPoolPtr> m_buffer_pools;
    media_library_return validate_configurations(multi_resize_config_t &mresize_config);
    media_library_return decode_config_json_string(multi_resize_config_t &mresize_config, std::string config_string);
    media_library_return acquire_output_buffers(std::vector<hailo_media_library_buffer> &buffers, output_video_config_t &output_video_config);
    media_library_return create_and_initialize_buffer_pools();
    media_library_return validate_input_and_output_frames(hailo_media_library_buffer &input_frame, std::vector<hailo_media_library_buffer> &output_frames);
    media_library_return perform_multi_resize(hailo_media_library_buffer &input_buffer, std::vector<hailo_media_library_buffer> &output_frames);
    void stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle);
    void increase_frame_counter();
};

//------------------------ MediaLibraryMultiResize ------------------------
tl::expected<std::shared_ptr<MediaLibraryMultiResize>, media_library_return> MediaLibraryMultiResize::create(std::string config_string)
{
    auto impl_expected = Impl::create(config_string);
    if (impl_expected.has_value())
        return std::make_shared<MediaLibraryMultiResize>(impl_expected.value());
    else
        return tl::make_unexpected(impl_expected.error());
}

MediaLibraryMultiResize::MediaLibraryMultiResize(std::shared_ptr<MediaLibraryMultiResize::Impl> impl) : m_impl(impl) {}

MediaLibraryMultiResize::~MediaLibraryMultiResize() = default;

media_library_return MediaLibraryMultiResize::configure(std::string config_string)
{
    return m_impl->configure(config_string);
}

media_library_return MediaLibraryMultiResize::configure(multi_resize_config_t &mresize_config)
{
    return m_impl->configure(mresize_config);
}

media_library_return MediaLibraryMultiResize::handle_frame(hailo_media_library_buffer &input_frame, std::vector<hailo_media_library_buffer> &output_frames)
{
    return m_impl->handle_frame(input_frame, output_frames);
}

multi_resize_config_t &MediaLibraryMultiResize::get_multi_resize_configs()
{
    return m_impl->get_multi_resize_configs();
}

output_video_config_t &MediaLibraryMultiResize::get_output_video_config()
{
    return m_impl->get_output_video_config();
}

media_library_return MediaLibraryMultiResize::set_input_video_config(uint32_t width, uint32_t height, uint32_t framerate)
{
    return m_impl->set_input_video_config(width, height, framerate);
}

//------------------------ MediaLibraryMultiResize::Impl ------------------------

tl::expected<std::shared_ptr<MediaLibraryMultiResize::Impl>, media_library_return> MediaLibraryMultiResize::Impl::create(std::string config_string)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    std::shared_ptr<MediaLibraryMultiResize::Impl> multi_resize = std::make_shared<MediaLibraryMultiResize::Impl>(status, config_string);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return multi_resize;
}

MediaLibraryMultiResize::Impl::Impl(media_library_return &status, std::string config_string)
{
    m_configured = false;

    // Start frame count from 0 - to make sure we always handle the first frame even if framerate is set to 0
    m_frame_counter = 0;
    m_buffer_pools.reserve(5);
    m_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_MULTI_RESIZE);
    m_multi_resize_config.output_video_config.resolutions.reserve(5);
    if (decode_config_json_string(m_multi_resize_config, config_string) != MEDIA_LIBRARY_SUCCESS)
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

    if (configure(m_multi_resize_config) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to configure multi-resize");
        status = MEDIA_LIBRARY_CONFIGURATION_ERROR;
        return;
    }

    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryMultiResize::Impl::~Impl()
{
    m_multi_resize_config.output_video_config.resolutions.clear();
    dsp_status status = dsp_utils::release_device();
    if (status != DSP_SUCCESS)
    {
        LOGGER__ERROR("Failed to acquire DSP device, status: {}", status);
    }
}

media_library_return MediaLibraryMultiResize::Impl::decode_config_json_string(multi_resize_config_t &mresize_config, std::string config_string)
{
    return m_config_manager->config_string_to_struct<multi_resize_config_t>(config_string, mresize_config);
}

media_library_return MediaLibraryMultiResize::Impl::configure(std::string config_string)
{
    multi_resize_config_t mresize_config;
    if (decode_config_json_string(mresize_config, config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return configure(mresize_config);
}

media_library_return MediaLibraryMultiResize::Impl::validate_configurations(multi_resize_config_t &mresize_config)
{
    // Make sure that the output fps of each stream is divided by the input fps with no reminder
    output_resolution_t &input_res = mresize_config.input_video_config;
    for (output_resolution_t &output_res : mresize_config.output_video_config.resolutions)
    {
        if (output_res.framerate != 0 && input_res.framerate % output_res.framerate != 0)
        {
            LOGGER__ERROR("Invalid output framerate {} - must be a divider of the input framerate {}", output_res.framerate, input_res.framerate);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryMultiResize::Impl::configure(multi_resize_config_t &mresize_config)
{
    if (validate_configurations(mresize_config) != MEDIA_LIBRARY_SUCCESS)
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;

    media_library_return ret = m_multi_resize_config.update(mresize_config);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to update multi-resize configurations (prohibited) {}", ret);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Create and initialize buffer pools
    ret = create_and_initialize_buffer_pools();
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    m_configured = true;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryMultiResize::Impl::create_and_initialize_buffer_pools()
{
    m_buffer_pools.clear();
    m_buffer_pools.reserve(5);
    for (output_resolution_t &output_res : m_multi_resize_config.output_video_config.resolutions)
    {
        LOGGER__INFO("Creating buffer pool for output resolution: width {} height {} in buffers size of {}", output_res.dimensions.destination_width, output_res.dimensions.destination_height, output_res.pool_max_buffers);
        MediaLibraryBufferPoolPtr buffer_pool = std::make_shared<MediaLibraryBufferPool>((uint)output_res.dimensions.destination_width, (uint)output_res.dimensions.destination_height, m_multi_resize_config.output_video_config.format, output_res.pool_max_buffers, CMA);
        if (buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to init buffer pool");
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
        m_buffer_pools.emplace_back(buffer_pool);
    }
    LOGGER__DEBUG("multi-resize holding {} buffer pools", m_buffer_pools.size());

    return MEDIA_LIBRARY_SUCCESS;
}

/**
 * @brief Acquire output buffers from buffer pools
 *
 * @param[in] buffers - vector of output buffers
 * @param[in] output_video_config - output video configuration
 */
media_library_return MediaLibraryMultiResize::Impl::acquire_output_buffers(std::vector<hailo_media_library_buffer> &buffers, output_video_config_t &output_video_config)
{
    // Acquire output buffers
    uint8_t output_size = output_video_config.resolutions.size();
    for (uint8_t i = 0; i < output_size; i++)
    {
        uint32_t framerate = output_video_config.resolutions[i].framerate;
        LOGGER__DEBUG("Acquiring buffer {}, target framerate is {}", i, framerate);

        // TODO (MSW-4092): Change from const 30 to the configurable value
        uint stream_period = (30 / framerate);
        bool should_acquire_buffer = (m_frame_counter % stream_period == 0);
        LOGGER__DEBUG("frame counter is {}, stream period is {}, should acquire buffer is {}", m_frame_counter, stream_period, should_acquire_buffer);

        hailo_media_library_buffer buffer;

        if (!should_acquire_buffer)
        {
            LOGGER__DEBUG("Skipping current frame to match framerate {}, no need to acquire buffer {}, counter is {}", framerate, i, m_frame_counter);
            buffers.emplace_back(std::move(buffer));

            continue;
        }

        if (m_buffer_pools[i]->acquire_buffer(buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to acquire buffer");
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
        buffers.emplace_back(std::move(buffer));
        LOGGER__DEBUG("buffer acquired successfully");
    }

    return MEDIA_LIBRARY_SUCCESS;
};

/**
 * @brief Perform multi resize on the DSP
 *
 * @param[in] input_frame - pointer to the input frame
 * @param[out] output_frames - vector of output frames
 */
media_library_return MediaLibraryMultiResize::Impl::perform_multi_resize(hailo_media_library_buffer &input_buffer, std::vector<hailo_media_library_buffer> &output_frames)
{
    struct timespec start_resize, end_resize;
    size_t output_frames_size = output_frames.size();
    size_t num_of_output_resolutions = m_multi_resize_config.output_video_config.resolutions.size();
    if (num_of_output_resolutions != output_frames_size)
    {
        LOGGER__ERROR("Number of output resolutions ({}) does not match number of output frames ({})", num_of_output_resolutions, output_frames_size);
        return MEDIA_LIBRARY_ERROR;
    }

    dsp_multi_resize_params_t multi_resize_params = {
        .src = input_buffer.hailo_pix_buffer.get(),
        .interpolation = m_multi_resize_config.output_video_config.interpolation_type,
    };

    uint num_bufs_to_resize = 0;
    for (size_t i = 0; i < num_of_output_resolutions; i++)
    {
        // TODO: Handle cases where its nullptr
        if (output_frames[i].hailo_pix_buffer == nullptr)
        {
            LOGGER__DEBUG("Skipping resize for output frame {} to match target framerate", i);
            continue;
        }
        dsp_image_properties_t *output_frame = output_frames[i].hailo_pix_buffer.get();
        output_resolution_t &output_res = m_multi_resize_config.output_video_config.resolutions[i];

        if (output_res != *output_frame)
        {
            LOGGER__ERROR("Invalid output frame width {} output frame height {}", output_frame->width, output_frame->height);
            return MEDIA_LIBRARY_ERROR;
        }

        multi_resize_params.dst[num_bufs_to_resize] = output_frame;
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
    uint end_x = m_multi_resize_config.input_video_config.dimensions.destination_width;
    uint end_y = m_multi_resize_config.input_video_config.dimensions.destination_height;

    if (m_multi_resize_config.digital_zoom_config.enabled)
    {
        if (m_multi_resize_config.digital_zoom_config.mode == DIGITAL_ZOOM_MODE_MAGNIFICATION)
        {
            uint center_x = end_x / 2;
            uint center_y = end_y / 2;
            uint zoom_width = center_x / m_multi_resize_config.digital_zoom_config.magnification;
            uint zoom_height = center_y / m_multi_resize_config.digital_zoom_config.magnification;
            start_x = center_x - zoom_width;
            start_y = center_y - zoom_height;
            end_x = center_x + zoom_width;
            end_y = center_y + zoom_height;
        }
        else
        {
            roi_t &digital_zoom_roi = m_multi_resize_config.digital_zoom_config.roi;
            start_x = digital_zoom_roi.x;
            start_y = digital_zoom_roi.y;
            end_x = start_x + digital_zoom_roi.width;
            end_y = start_y + digital_zoom_roi.height;

            // Validate digital zoom ROI values with the input frame dimensions
            if (end_x > m_multi_resize_config.input_video_config.dimensions.destination_width)
            {
                LOGGER__ERROR("Invalid digital zoom ROI. X ({}) and width ({}) coordinates exceed input frame width ({})", start_x, digital_zoom_roi.width, m_multi_resize_config.input_video_config.dimensions.destination_width);
                return MEDIA_LIBRARY_ERROR;
            }

            if (end_y > m_multi_resize_config.input_video_config.dimensions.destination_height)
            {
                LOGGER__ERROR("Invalid digital zoom ROI. Y ({}) and height ({}) coordinates exceed input frame height ({})", start_y, digital_zoom_roi.height, m_multi_resize_config.input_video_config.dimensions.destination_height);
                return MEDIA_LIBRARY_ERROR;
            }
        }
    }

    // Perform multi resize
    LOGGER__DEBUG("Performing multi resize on the DSP with digital zoom ROI: start_x {} start_y {} end_x {} end_y {}", start_x, start_y, end_x, end_y);
    clock_gettime(CLOCK_MONOTONIC, &start_resize);
    dsp_status ret = dsp_utils::perform_dsp_multi_resize(&multi_resize_params, start_x, start_y, end_x, end_y);
    clock_gettime(CLOCK_MONOTONIC, &end_resize);
    [[maybe_unused]] long ms = (long)media_library_difftimespec_ms(end_resize, start_resize);
    LOGGER__TRACE("perform_multi_resize took {} milliseconds ({} fps)", ms, 1000 / ms);

    if (ret != DSP_SUCCESS)
        return MEDIA_LIBRARY_DSP_OPERATION_ERROR;

    return MEDIA_LIBRARY_SUCCESS;
}

void MediaLibraryMultiResize::Impl::stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle)
{
    clock_gettime(CLOCK_MONOTONIC, &end_handle);
    long ms = (long)media_library_difftimespec_ms(end_handle, start_handle);
    uint framerate = 1000 / ms;
    LOGGER__DEBUG("multi-resize handle_frame took {} milliseconds ({} fps)", ms, framerate);
}

void MediaLibraryMultiResize::Impl::increase_frame_counter()
{
    // Increase frame counter or reset it to 1
    m_frame_counter = (m_frame_counter == 60) ? 1 : m_frame_counter + 1;
}

media_library_return
MediaLibraryMultiResize::Impl::validate_input_and_output_frames(
    hailo_media_library_buffer &input_frame,
    std::vector<hailo_media_library_buffer> &output_frames)
{
    // Check if vector of output buffers is not empty
    if (!output_frames.empty())
    {
        LOGGER__ERROR("output_frames vector is not empty - an empty vector is required");
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    // Check that caps match between incoming frames and output frames?

    if (m_multi_resize_config.output_video_config.grayscale)
    {
        if (m_multi_resize_config.output_video_config.format != DSP_IMAGE_FORMAT_NV12)
        {
            LOGGER__ERROR("Saturating to grayscale is enabled only for NV12 format");
            return MEDIA_LIBRARY_INVALID_ARGUMENT;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryMultiResize::Impl::handle_frame(hailo_media_library_buffer &input_frame, std::vector<hailo_media_library_buffer> &output_frames)
{
    // Stamp start time
    struct timespec start_handle, end_handle;
    clock_gettime(CLOCK_MONOTONIC, &start_handle);

    if (validate_input_and_output_frames(input_frame, output_frames) != MEDIA_LIBRARY_SUCCESS)
    {
        input_frame.decrease_ref_count();
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    // Acquire output buffers
    media_library_return media_lib_ret = MEDIA_LIBRARY_SUCCESS;
    media_lib_ret = acquire_output_buffers(output_frames, m_multi_resize_config.output_video_config);
    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
    {
        input_frame.decrease_ref_count();
        return media_lib_ret;
    }

    // Handle grayscaling
    if (m_multi_resize_config.output_video_config.grayscale)
    {
        // Saturate UV plane to value of 128 - to get a grayscale image
        dsp_data_plane_t &uv_plane = input_frame.hailo_pix_buffer->planes[1];
        memset(uv_plane.userptr, 128, uv_plane.bytesused);
    }

    // Perform multi resize
    media_lib_ret = perform_multi_resize(input_frame, output_frames);

    // Unref the input frame
    input_frame.decrease_ref_count();

    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
        return media_lib_ret;

    increase_frame_counter();

    stamp_time_and_log_fps(start_handle, end_handle);
    return MEDIA_LIBRARY_SUCCESS;
}

multi_resize_config_t &MediaLibraryMultiResize::Impl::get_multi_resize_configs()
{
    return m_multi_resize_config;
}

output_video_config_t &MediaLibraryMultiResize::Impl::get_output_video_config()
{
    return m_multi_resize_config.output_video_config;
}

media_library_return MediaLibraryMultiResize::Impl::set_input_video_config(uint32_t width, uint32_t height, uint32_t framerate)
{
    m_multi_resize_config.input_video_config.dimensions.destination_width = width;
    m_multi_resize_config.input_video_config.dimensions.destination_height = height;
    m_multi_resize_config.input_video_config.framerate = framerate;

    // Check if the new framerate is a multiple of each output framerate
    for (auto &output_config : m_multi_resize_config.output_video_config.resolutions)
    {
        if (framerate % output_config.framerate != 0)
        {
            LOGGER__ERROR("The new input framerate {} is not a multiple of the output framerate {}", framerate, output_config.framerate);
            return MEDIA_LIBRARY_INVALID_ARGUMENT;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}
