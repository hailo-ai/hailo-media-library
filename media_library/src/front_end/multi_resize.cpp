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

#include "multi_resize.hpp"
#include "buffer_pool.hpp"
#include "config_manager.hpp"
#include "dsp_utils.hpp"
#include "media_library_logger.hpp"
#include "media_library_utils.hpp"
#include "privacy_mask.hpp"
#include <iostream>
#include <stdint.h>
#include <string>
#include <time.h>
#include <tl/expected.hpp>
#include <vector>
#include <shared_mutex>
#define MAKE_EVEN(value) ((value) % 2 != 0 ? (value) + 1 : (value))

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

    PrivacyMaskBlenderPtr get_privacy_mask_blender();

    // set the output video rotation
    media_library_return set_output_rotation(const rotation_angle_t &rotation);

    // set the callbacks object
    media_library_return observe(const MediaLibraryMultiResize::callbacks_t &callbacks);

private:
    // configured flag - to determine if first configuration was done
    bool m_configured;
    // frame counter - used internally for matching requested framerate
    uint m_frame_counter;
    // configuration manager
    std::shared_ptr<ConfigManager> m_config_manager;
    // operation configurations
    multi_resize_config_t m_multi_resize_config;
    // dsp internal helper buffer pool
    MediaLibraryBufferPoolPtr m_dsp_helper_buffer_pool;
    // helper buffer for multi-resize (constantly in use)
    hailo_media_library_buffer m_resize_helper_buffer;
    // privacy mask blender
    PrivacyMaskBlenderPtr m_privacy_mask_blender;
    // callbacks
    std::vector<MediaLibraryMultiResize::callbacks_t> m_callbacks;
    // output buffer pools
    std::vector<MediaLibraryBufferPoolPtr> m_buffer_pools;
    // Timestamps in ms.
    std::vector<int64_t> m_timestamps;
    bool m_strict_framerate = true;
    // read/write lock for configuration manipulation/reading
    std::shared_mutex rw_lock;

    media_library_return validate_configurations(multi_resize_config_t &mresize_config);
    media_library_return decode_config_json_string(multi_resize_config_t &mresize_config, std::string config_string);
    media_library_return acquire_output_buffers(hailo_media_library_buffer &input_buffer, std::vector<hailo_media_library_buffer> &buffers);
    media_library_return create_and_initialize_buffer_pools();
    media_library_return validate_input_and_output_frames(hailo_media_library_buffer &input_frame, std::vector<hailo_media_library_buffer> &output_frames);
    media_library_return perform_multi_resize(hailo_media_library_buffer &input_buffer, std::vector<hailo_media_library_buffer> &output_frames);
    media_library_return configure_internal(multi_resize_config_t &mresize_config);
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

PrivacyMaskBlenderPtr MediaLibraryMultiResize::get_privacy_mask_blender()
{
    return m_impl->get_privacy_mask_blender();
}

media_library_return MediaLibraryMultiResize::set_input_video_config(uint32_t width, uint32_t height, uint32_t framerate)
{
    return m_impl->set_input_video_config(width, height, framerate);
}

media_library_return MediaLibraryMultiResize::set_output_rotation(const rotation_angle_t &rotation)
{
    return m_impl->set_output_rotation(rotation);
}

media_library_return MediaLibraryMultiResize::observe(const MediaLibraryMultiResize::callbacks_t &callbacks)
{
    return m_impl->observe(callbacks);
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

    tl::expected<std::shared_ptr<PrivacyMaskBlender>, media_library_return> blender_expected = PrivacyMaskBlender::create();
    if (blender_expected.has_value())
    {
        m_privacy_mask_blender = blender_expected.value();
    }
    else
    {
        LOGGER__ERROR("Failed to create privacy mask blender");
        status = blender_expected.error();
    }

    m_dsp_helper_buffer_pool = std::make_shared<MediaLibraryBufferPool>(1280, 720, DSP_IMAGE_FORMAT_GRAY8, 1, CMA, dsp_utils::get_dsp_desired_stride_from_width(1280), "multi_resize_input");
    if (m_dsp_helper_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to init internal helper buffer pool");
        status = MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    m_dsp_helper_buffer_pool->acquire_buffer(m_resize_helper_buffer);

    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryMultiResize::Impl::~Impl()
{
    m_resize_helper_buffer.decrease_ref_count();
    m_multi_resize_config.output_video_config.resolutions.clear();
    dsp_status status = dsp_utils::release_device();
    if (status != DSP_SUCCESS)
    {
        LOGGER__ERROR("Failed to release DSP device, status: {}", status);
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

media_library_return MediaLibraryMultiResize::Impl::set_output_rotation(const rotation_angle_t &rotation)
{
    rotation_angle_t current_rotation = m_multi_resize_config.rotation_config;
    if (current_rotation == rotation)
    {
        LOGGER__INFO("Output rotation is already set to {}", rotation);
        return MEDIA_LIBRARY_SUCCESS;
    }
    LOGGER__INFO("Setting output rotation to {} from {}", rotation, m_multi_resize_config.rotation_config);

    std::unique_lock<std::shared_mutex> lock(rw_lock);

    m_multi_resize_config.set_output_dimensions_rotation(rotation);

    // recreate buffer pools if needed
    media_library_return ret = create_and_initialize_buffer_pools();
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    lock.unlock();

    for (auto &callbacks : m_callbacks)
    {
        if (callbacks.on_output_resolutions_change)
            callbacks.on_output_resolutions_change(m_multi_resize_config.output_video_config.resolutions);
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryMultiResize::Impl::configure(multi_resize_config_t &mresize_config)
{
    auto ret = validate_configurations(mresize_config);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to configure multi-resize {}", ret);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    LOGGER__INFO("Configuring multi-resize with new configurations");
    std::unique_lock<std::shared_mutex> lock(rw_lock);

    // Create and initialize buffer pools
    ret = m_multi_resize_config.update(mresize_config);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to update multi-resize configurations (prohibited) {}", ret);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Create and initialize buffer pools
    ret = create_and_initialize_buffer_pools();
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    auto timestamp = media_library_get_timespec_ms();
    for (uint8_t i = 0; i < m_buffer_pools.size(); i++)
    {
        m_timestamps.push_back(timestamp);
    }
    m_configured = true;

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryMultiResize::Impl::configure_internal(multi_resize_config_t &mresize_config)
{
    media_library_return ret = m_multi_resize_config.update(mresize_config);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to update multi-resize configurations (prohibited) {}", ret);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // recreate buffer pools if needed
    ret = create_and_initialize_buffer_pools();
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryMultiResize::Impl::create_and_initialize_buffer_pools()
{
    bool first = false;
    if (m_buffer_pools.empty())
    {
        m_buffer_pools.reserve(m_multi_resize_config.output_video_config.resolutions.size());
        first = true;
    }

    for (uint i = 0; i < m_multi_resize_config.output_video_config.resolutions.size(); i++)
    {
        output_resolution_t &output_res = m_multi_resize_config.output_video_config.resolutions[i];
        uint width, height;
        width = output_res.dimensions.destination_width;
        height = output_res.dimensions.destination_height;
        std::string name = "multi_resize_output_" + std::to_string(i);

        if (!first && m_buffer_pools[i] != nullptr && width == m_buffer_pools[i]->get_width() && height == m_buffer_pools[i]->get_height())
        {
            LOGGER__DEBUG("Buffer pool already exists, skipping creation");
            return MEDIA_LIBRARY_SUCCESS;
        }

        auto bytes_per_line = dsp_utils::get_dsp_desired_stride_from_width(width);
        LOGGER__INFO("Creating buffer pool named {} for output resolution: width {} height {} in buffers size of {} and bytes per line {}", name, width, height, output_res.pool_max_buffers, bytes_per_line);
        MediaLibraryBufferPoolPtr buffer_pool = std::make_shared<MediaLibraryBufferPool>(width, height, m_multi_resize_config.output_video_config.format, output_res.pool_max_buffers, CMA, bytes_per_line, name);
        if (buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to init buffer pool");
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
        if (first)
        {
            m_buffer_pools.emplace_back(buffer_pool);
        }
        else
        {
            m_buffer_pools[i] = buffer_pool;
        }
    }
    LOGGER__DEBUG("multi-resize holding {} buffer pools", m_buffer_pools.size());

    return MEDIA_LIBRARY_SUCCESS;
}

/**
 * @brief Acquire output buffers from buffer pools
 *
 * @param[in] input_frame - pointer to the input frame
 * @param[in] buffers - vector of output buffers
 */
media_library_return MediaLibraryMultiResize::Impl::acquire_output_buffers(hailo_media_library_buffer &input_buffer, std::vector<hailo_media_library_buffer> &buffers)
{
    // Acquire output buffers
    int32_t isp_ae_fps = input_buffer.isp_ae_fps;
    bool should_acquire_buffer;
    uint8_t num_of_outputs = m_multi_resize_config.output_video_config.resolutions.size();
    int64_t timestamp = media_library_get_timespec_ms();
    for (uint8_t i = 0; i < num_of_outputs; i++)
    {
        uint32_t input_framerate = m_multi_resize_config.input_video_config.framerate;
        uint32_t output_framerate = m_multi_resize_config.output_video_config.resolutions[i].framerate;
        int64_t frame_latency = 1000 / output_framerate;
        hailo_media_library_buffer buffer;
        bool push_frame = timestamp - m_timestamps[i] >= frame_latency;
        if (push_frame)
        {
            // We want to advance the timestamp to the next frame time at least once.
            // If we are still 2 frames behind, we will advance the timestamp again.
            do
            {
                m_timestamps[i] += frame_latency;
            } while (timestamp - m_timestamps[i] >= frame_latency * 2);
            LOGGER__DEBUG("Skipping current frame to match framerate {}, no need to acquire buffer {}, counter is {}", output_framerate, i, m_frame_counter);
        }
        LOGGER__DEBUG("Acquiring buffer {}, target framerate is {}", i, output_framerate);
        // m_strict_framerate is true, using new frame counter logic
        if (m_strict_framerate)
            should_acquire_buffer = (output_framerate != 0) && push_frame;
        else
        {
            // This is the old logic, using the frame counter, deprecated.
            uint stream_period = (output_framerate == 0) ? 0 : input_framerate / output_framerate;
            should_acquire_buffer = stream_period == 0 ? false : (m_frame_counter % stream_period == 0) || (isp_ae_fps != -1 && output_framerate >= static_cast<uint32_t>(isp_ae_fps));
            LOGGER__DEBUG("frame counter is {}, stream period is {}, should acquire buffer is {}", m_frame_counter, stream_period, should_acquire_buffer);
        }
        if (!should_acquire_buffer)
        {
            LOGGER__DEBUG("Skipping current frame to match framerate {}, no need to acquire buffer {}, counter is {}", output_framerate, i, m_frame_counter);
            buffers.emplace_back(std::move(buffer));

            continue;
        }

        if (m_buffer_pools[i]->acquire_buffer(buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to acquire buffer");
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
        buffer.isp_ae_fps = isp_ae_fps;
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

    dsp_crop_resize_params_t crop_resize_params;
    dsp_multi_crop_resize_params_t multi_crop_resize_params = {
        .src = input_buffer.hailo_pix_buffer.get(),
        .crop_resize_params = &crop_resize_params,
        .crop_resize_params_count = 1,
        .interpolation = m_multi_resize_config.output_video_config.interpolation_type,
        .helper_plane = m_resize_helper_buffer.hailo_pix_buffer.get(),
    };

    uint num_bufs_to_resize = 0;
    for (size_t i = 0; i < num_of_output_resolutions; i++)
    {
        // TODO: Handle cases where its nullptr
        if (output_frames[i].hailo_pix_buffer == nullptr)
        {
            LOGGER__DEBUG("Skipping resize for output frame {} to match target framerate ({})", i, m_multi_resize_config.output_video_config.resolutions[i].framerate);
            continue;
        }
        dsp_image_properties_t *output_frame = output_frames[i].hailo_pix_buffer.get();
        output_resolution_t &output_res = m_multi_resize_config.output_video_config.resolutions[i];

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
            start_x = MAKE_EVEN(center_x - zoom_width);
            start_y = MAKE_EVEN(center_y - zoom_height);
            end_x = MAKE_EVEN(center_x + zoom_width);
            end_y = MAKE_EVEN(center_y + zoom_height);
        }
        else
        {
            roi_t &digital_zoom_roi = m_multi_resize_config.digital_zoom_config.roi;
            start_x = MAKE_EVEN(digital_zoom_roi.x);
            start_y = MAKE_EVEN(digital_zoom_roi.y);
            end_x = MAKE_EVEN(start_x + digital_zoom_roi.width);
            end_y = MAKE_EVEN(start_y + digital_zoom_roi.height);

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

    // Blend privacy mask
    auto blender_expected = m_privacy_mask_blender->blend();
    if (!blender_expected.has_value())
    {
        LOGGER__ERROR("Failed to blend privacy mask");
        return MEDIA_LIBRARY_ERROR;
    }

    PrivacyMaskDataPtr privacy_mask_data = blender_expected.value();

    // Perform multi resize
    clock_gettime(CLOCK_MONOTONIC, &start_resize);
    dsp_status ret = DSP_SUCCESS;
    if (privacy_mask_data->rois_count == 0)
    {
        LOGGER__DEBUG("Performing multi resize on the DSP with digital zoom ROI: start_x {} start_y {} end_x {} end_y {}", start_x, start_y, end_x, end_y);
        dsp_roi_t crop = {
            .start_x = start_x,
            .start_y = start_y,
            .end_x = end_x,
            .end_y = end_y
        };
        crop_resize_params.crop = &crop;
        ret = dsp_utils::perform_dsp_multi_resize(&multi_crop_resize_params);
    }
    else
    {
        dsp_roi_t dsp_rois[privacy_mask_data->rois_count];
        dsp_privacy_mask_t dsp_privacy_mask = {
            .bitmask = (uint8_t *)privacy_mask_data->bitmask.get_plane(0),
            .y_color = privacy_mask_data->color.y,
            .u_color = privacy_mask_data->color.u,
            .v_color = privacy_mask_data->color.v,
            .rois = dsp_rois,
            .rois_count = privacy_mask_data->rois_count,
        };

        for (uint i = 0; i < privacy_mask_data->rois_count; i++)
        {
            dsp_privacy_mask.rois[i] = {
                .start_x = privacy_mask_data->rois[i].x,
                .start_y = privacy_mask_data->rois[i].y,
                .end_x = privacy_mask_data->rois[i].x + privacy_mask_data->rois[i].width,
                .end_y = privacy_mask_data->rois[i].y + privacy_mask_data->rois[i].height};
        }

        LOGGER__DEBUG("Performing multi resize on the DSP with digital zoom ROI: start_x {} start_y {} end_x {} end_y {} and {} privacy masks", start_x, start_y, end_x, end_y, privacy_mask_data->rois_count);
        dsp_roi_t crop = {
            .start_x = start_x,
            .start_y = start_y,
            .end_x = end_x,
            .end_y = end_y
        };
        crop_resize_params.crop = &crop;
        ret = dsp_utils::perform_dsp_multi_resize(&multi_crop_resize_params, &dsp_privacy_mask);
    }

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

media_library_return MediaLibraryMultiResize::Impl::validate_input_and_output_frames(hailo_media_library_buffer &input_frame, std::vector<hailo_media_library_buffer> &output_frames)
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

    std::shared_lock<std::shared_mutex> lock(rw_lock);

    // Acquire output buffers
    media_library_return media_lib_ret = MEDIA_LIBRARY_SUCCESS;
    media_lib_ret = acquire_output_buffers(input_frame, output_frames);
    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
    {
        input_frame.decrease_ref_count();
        return media_lib_ret;
    }

    // Handle grayscaling
    if (m_multi_resize_config.output_video_config.grayscale)
    {
        // Saturate UV plane to value of 128 - to get a grayscale image
        if (input_frame.is_dmabuf())
        {
            DmaMemoryAllocator::get_instance().dmabuf_sync_start(input_frame.get_plane(1));
            memset(input_frame.get_plane(1), 128, input_frame.get_plane_size(1));
            DmaMemoryAllocator::get_instance().dmabuf_sync_end(input_frame.get_plane(1));
        }
        else
        {
            memset(input_frame.get_plane(1), 128, input_frame.get_plane_size(1));
        }
    }

    // Perform multi resize
    media_lib_ret = perform_multi_resize(input_frame, output_frames);

    // Unref the input frame
    input_frame.decrease_ref_count();
    // LOGGER__ERROR("decreased input frame ref count of buffer indexed: {}, plane 0 refcount: {}", input_frame.buffer_index, input_frame.refcount(0) );

    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
        return media_lib_ret;

    increase_frame_counter();

    stamp_time_and_log_fps(start_handle, end_handle);
    return MEDIA_LIBRARY_SUCCESS;
}

multi_resize_config_t &MediaLibraryMultiResize::Impl::get_multi_resize_configs()
{
    std::shared_lock<std::shared_mutex> lock(rw_lock);
    return m_multi_resize_config;
}

output_video_config_t &MediaLibraryMultiResize::Impl::get_output_video_config()
{
    std::shared_lock<std::shared_mutex> lock(rw_lock);
    return m_multi_resize_config.output_video_config;
}

PrivacyMaskBlenderPtr MediaLibraryMultiResize::Impl::get_privacy_mask_blender()
{
    return m_privacy_mask_blender;
}

media_library_return MediaLibraryMultiResize::Impl::set_input_video_config(uint32_t width, uint32_t height, uint32_t framerate)
{
    std::unique_lock<std::shared_mutex> lock(rw_lock);
    m_multi_resize_config.input_video_config.dimensions.destination_width = width;
    m_multi_resize_config.input_video_config.dimensions.destination_height = height;
    m_multi_resize_config.input_video_config.framerate = framerate;

    media_library_return blender_config_status = m_privacy_mask_blender->set_frame_size(m_multi_resize_config.input_video_config.dimensions.destination_width,
                                                                                        m_multi_resize_config.input_video_config.dimensions.destination_height);
    if (blender_config_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to set privacy mask blender frame size");
        return blender_config_status;
    }

    return blender_config_status;
}

media_library_return MediaLibraryMultiResize::Impl::observe(const MediaLibraryMultiResize::callbacks_t &callbacks)
{
    m_callbacks.push_back(callbacks);
    return MEDIA_LIBRARY_SUCCESS;
}