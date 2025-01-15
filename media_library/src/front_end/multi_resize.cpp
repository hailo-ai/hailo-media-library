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

#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/core/core.hpp"

#include "multi_resize.hpp"
#include "buffer_pool.hpp"
#include "config_manager.hpp"
#include "dsp_utils.hpp"
#include "media_library_logger.hpp"
#include "media_library_utils.hpp"
#include "privacy_mask.hpp"
#include "motion_detection.hpp"
#include "post_denoise_filter.hpp"
#include <iostream>
#include <stdint.h>
#include <string>
#include <time.h>
#include <tl/expected.hpp>
#include <vector>
#include <shared_mutex>
#include <optional>
#define MAKE_EVEN(value) ((value) % 2 != 0 ? (value) + 1 : (value))
#define MAX_NUM_OF_OUTPUTS 8
#define GET_NUM_OF_OUTPUTS(multi_resize_config)                                                                        \
    ((multi_resize_config.output_video_config.resolutions.size()) +                                                    \
     (multi_resize_config.motion_detection_config.enabled ? 1 : 0))

struct timestamp_metadata
{
    uint64_t last_timestamp;
    float accumulated_diff;
};

class MediaLibraryMultiResize::Impl final
{
  public:
    static tl::expected<std::shared_ptr<MediaLibraryMultiResize::Impl>, media_library_return> create(
        std::string config_string);
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
    media_library_return handle_frame(HailoMediaLibraryBufferPtr input_frame,
                                      std::vector<HailoMediaLibraryBufferPtr> &output_frames);

    // get the multi-resize configurations object
    multi_resize_config_t get_multi_resize_configs();

    // get the output video configurations object
    output_video_config_t get_output_video_config();

    // set the input video configurations object
    media_library_return set_input_video_config(uint32_t width, uint32_t height, uint32_t framerate);

    PrivacyMaskBlenderPtr get_privacy_mask_blender();

    // set the output video rotation
    media_library_return set_output_rotation(const rotation_angle_t &rotation);

    // set the denoise status
    media_library_return set_denoise_status(bool status);

    // set the callbacks object
    media_library_return observe(const MediaLibraryMultiResize::callbacks_t &callbacks);

  private:
    static constexpr int max_frames_jitter_multiplier = 3;
    static constexpr int max_frames_latency_multiplier = 20;

    // configured flag - to determine if first configuration was done
    bool m_configured;
    // frame counter - used internally for matching requested framerate
    uint m_frame_counter;
    // configuration manager
    std::shared_ptr<ConfigManager> m_config_manager;
    // operation configurations
    multi_resize_config_t m_multi_resize_config;
    // privacy mask blender
    PrivacyMaskBlenderPtr m_privacy_mask_blender;
    // callbacks
    std::vector<MediaLibraryMultiResize::callbacks_t> m_callbacks;
    // output buffer pools
    std::vector<MediaLibraryBufferPoolPtr> m_buffer_pools;
    // Timestamps in ms.
    std::vector<timestamp_metadata> m_timestamps;
    bool m_strict_framerate = true;
    // read/write lock for configuration manipulation/reading
    std::shared_mutex rw_lock;
    uint32_t m_max_buffer_pool_size;

    MotionDetection m_motion_detection;
    std::unique_ptr<PostDenoiseFilter> m_post_denoise_filter;

    media_library_return validate_configurations(multi_resize_config_t &mresize_config);
    media_library_return decode_config_json_string(multi_resize_config_t &mresize_config, std::string config_string);
    media_library_return acquire_output_buffers(HailoMediaLibraryBufferPtr input_buffer,
                                                std::vector<HailoMediaLibraryBufferPtr> &buffers);
    bool should_push_frame_logic(uint32_t output_framerate, uint8_t output_index, uint64_t isp_timestamp_ns);
    media_library_return create_and_initialize_buffer_pools();
    media_library_return validate_output_frames(std::vector<HailoMediaLibraryBufferPtr> &output_frames);
    media_library_return perform_multi_resize(HailoMediaLibraryBufferPtr input_buffer,
                                              std::vector<HailoMediaLibraryBufferPtr> &output_frames);
    media_library_return configure_internal(multi_resize_config_t &mresize_config);
    void stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle);
    void increase_frame_counter();
    tl::expected<dsp_roi_t, media_library_return> get_input_roi();
};

//------------------------ MediaLibraryMultiResize ------------------------
tl::expected<std::shared_ptr<MediaLibraryMultiResize>, media_library_return> MediaLibraryMultiResize::create(
    std::string config_string)
{
    auto impl_expected = Impl::create(config_string);
    if (impl_expected.has_value())
        return std::make_shared<MediaLibraryMultiResize>(impl_expected.value());
    else
        return tl::make_unexpected(impl_expected.error());
}

MediaLibraryMultiResize::MediaLibraryMultiResize(std::shared_ptr<MediaLibraryMultiResize::Impl> impl) : m_impl(impl)
{
}

MediaLibraryMultiResize::~MediaLibraryMultiResize() = default;

media_library_return MediaLibraryMultiResize::configure(std::string config_string)
{
    return m_impl->configure(config_string);
}

media_library_return MediaLibraryMultiResize::configure(multi_resize_config_t &mresize_config)
{
    return m_impl->configure(mresize_config);
}

media_library_return MediaLibraryMultiResize::handle_frame(HailoMediaLibraryBufferPtr input_frame,
                                                           std::vector<HailoMediaLibraryBufferPtr> &output_frames)
{
    return m_impl->handle_frame(input_frame, output_frames);
}

multi_resize_config_t MediaLibraryMultiResize::get_multi_resize_configs()
{
    return m_impl->get_multi_resize_configs();
}

output_video_config_t MediaLibraryMultiResize::get_output_video_config()
{
    return m_impl->get_output_video_config();
}

PrivacyMaskBlenderPtr MediaLibraryMultiResize::get_privacy_mask_blender()
{
    return m_impl->get_privacy_mask_blender();
}

media_library_return MediaLibraryMultiResize::set_input_video_config(uint32_t width, uint32_t height,
                                                                     uint32_t framerate)
{
    return m_impl->set_input_video_config(width, height, framerate);
}

media_library_return MediaLibraryMultiResize::set_output_rotation(const rotation_angle_t &rotation)
{
    return m_impl->set_output_rotation(rotation);
}

media_library_return MediaLibraryMultiResize::set_denoise_status(bool status)
{
    return m_impl->set_denoise_status(status);
}

media_library_return MediaLibraryMultiResize::observe(const MediaLibraryMultiResize::callbacks_t &callbacks)
{
    return m_impl->observe(callbacks);
}

//------------------------ MediaLibraryMultiResize::Impl ------------------------

tl::expected<std::shared_ptr<MediaLibraryMultiResize::Impl>, media_library_return> MediaLibraryMultiResize::Impl::
    create(std::string config_string)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    std::shared_ptr<MediaLibraryMultiResize::Impl> multi_resize =
        std::make_shared<MediaLibraryMultiResize::Impl>(status, config_string);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return multi_resize;
}

MediaLibraryMultiResize::Impl::Impl(media_library_return &status, std::string config_string)
    : m_post_denoise_filter(std::make_unique<PostDenoiseFilter>())
{
    m_configured = false;
    // Start frame count from 0 - to make sure we always handle the first frame even if framerate is set to 0
    m_frame_counter = 0;
    m_buffer_pools.reserve(MAX_NUM_OF_OUTPUTS);
    m_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_MULTI_RESIZE);
    m_multi_resize_config.output_video_config.resolutions.reserve(MAX_NUM_OF_OUTPUTS);
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

    multi_resize_config_t mresize_config;
    mresize_config = m_multi_resize_config;
    m_multi_resize_config.rotation_config.angle = ROTATION_ANGLE_0;
    m_motion_detection = MotionDetection(m_multi_resize_config.motion_detection_config);

    if (configure(mresize_config) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to configure multi-resize");
        status = MEDIA_LIBRARY_CONFIGURATION_ERROR;
        return;
    }

    tl::expected<std::shared_ptr<PrivacyMaskBlender>, media_library_return> blender_expected =
        PrivacyMaskBlender::create();
    if (blender_expected.has_value())
    {
        m_privacy_mask_blender = blender_expected.value();
    }
    else
    {
        LOGGER__ERROR("Failed to create privacy mask blender");
        status = blender_expected.error();
    }

    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryMultiResize::Impl::~Impl()
{

    m_multi_resize_config.output_video_config.resolutions.clear();
    dsp_status status = dsp_utils::release_device();
    if (status != DSP_SUCCESS)
    {
        LOGGER__ERROR("Failed to release DSP device, status: {}", status);
    }
}

media_library_return MediaLibraryMultiResize::Impl::decode_config_json_string(multi_resize_config_t &mresize_config,
                                                                              std::string config_string)
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
            LOGGER__ERROR("Invalid output framerate {} - must be a divider of the input framerate {}",
                          output_res.framerate, input_res.framerate);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryMultiResize::Impl::set_output_rotation(const rotation_angle_t &angle)
{
    rotation_config_t new_rotation = {true, angle};
    rotation_config_t &current_rotation = m_multi_resize_config.rotation_config;
    if (current_rotation == new_rotation)
    {
        LOGGER__INFO("Output rotation is already set to {}", current_rotation.angle);
        return MEDIA_LIBRARY_SUCCESS;
    }
    LOGGER__INFO("Setting output rotation from {} to {}", current_rotation.angle, new_rotation.angle);

    std::unique_lock<std::shared_mutex> lock(rw_lock);

    m_multi_resize_config.set_output_dimensions_rotation(new_rotation);
    auto output_res_expected = m_multi_resize_config.get_output_resolution_by_index(0);
    if (!output_res_expected.has_value())
    {
        return output_res_expected.error();
    }
    output_resolution_t &output_res = output_res_expected.value().get();
    LOGGER__DEBUG("Output rotation dims are now width {} height {}", output_res.dimensions.destination_width,
                  output_res.dimensions.destination_height);

    // recreate buffer pools if needed
    media_library_return ret = create_and_initialize_buffer_pools();
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to recreate buffer pool after setting output rotation");
        return ret;
    }

    lock.unlock();

    for (auto &callbacks : m_callbacks)
    {
        if (callbacks.on_output_resolutions_change)
            callbacks.on_output_resolutions_change(m_multi_resize_config.output_video_config.resolutions);
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryMultiResize::Impl::set_denoise_status(bool status)
{
    m_post_denoise_filter->m_denoise_element_enabled = status;
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

    ret = m_motion_detection.allocate_motion_detection(m_max_buffer_pool_size);
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    for (uint8_t i = 0; i < m_buffer_pools.size(); i++)
    {
        m_timestamps.push_back({0, 0});
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

    ret = m_motion_detection.allocate_motion_detection(m_max_buffer_pool_size);
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryMultiResize::Impl::create_and_initialize_buffer_pools()
{
    uint num_of_outputs = GET_NUM_OF_OUTPUTS(m_multi_resize_config);
    bool first = false;
    m_max_buffer_pool_size = 0;
    if (m_buffer_pools.empty())
    {
        m_buffer_pools.reserve(num_of_outputs);
        first = true;
    }

    for (uint i = 0; i < num_of_outputs; i++)
    {
        auto output_res_expected = m_multi_resize_config.get_output_resolution_by_index(i);
        if (!output_res_expected.has_value())
        {
            return output_res_expected.error();
        }
        output_resolution_t &output_res = output_res_expected.value().get();
        uint width, height;
        width = output_res.dimensions.destination_width;
        height = output_res.dimensions.destination_height;
        std::string name = "multi_resize_output_" + std::to_string(i);
        if (output_res.pool_max_buffers > m_max_buffer_pool_size)
        {
            m_max_buffer_pool_size = output_res.pool_max_buffers;
        }
        if (m_multi_resize_config.motion_detection_config.enabled && output_res.pool_max_buffers == 0)
        {
            output_res.pool_max_buffers = m_max_buffer_pool_size;
        }

        if (!first && m_buffer_pools[i] != nullptr && width == m_buffer_pools[i]->get_width() &&
            height == m_buffer_pools[i]->get_height())
        {
            LOGGER__DEBUG("Buffer pool already exists, skipping creation");
            return MEDIA_LIBRARY_SUCCESS;
        }

        auto bytes_per_line = dsp_utils::get_dsp_desired_stride_from_width(width);
        LOGGER__INFO("Creating buffer pool named {} for output resolution: width {} height {} in buffers size of {} "
                     "and bytes per line {}",
                     name, width, height, output_res.pool_max_buffers, bytes_per_line);
        MediaLibraryBufferPoolPtr buffer_pool = std::make_shared<MediaLibraryBufferPool>(
            width, height, m_multi_resize_config.output_video_config.format, output_res.pool_max_buffers,
            HAILO_MEMORY_TYPE_DMABUF, bytes_per_line, name);
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
 * @brief Determines whether a frame should be pushed based on the output framerate and timestamp.
 *
 * This function calculates the latency since the last frame for the given output index and
 * compares it to the expected frame latency based on the output framerate. If the accumulated
 * latency difference exceeds or meets the expected latency, the function returns true,
 * indicating that the frame should be pushed. Otherwise, it returns false.
 *
 * @param[in] output_framerate The desired framerate for the output in frames per second (fps).
 * @param[in] output_index The index of the output buffer to check.
 * @param[in] isp_timestamp_ns The timestamp of the frame from the ISP in nanoseconds.
 *
 * @return `true` if the frame should be pushed to the buffer, `false` otherwise.
 *
 * @note If `output_framerate` is 0, the function skips the current frame.
 *
 * @note This method is particularly robust in scenarios where the denoise element operates
 *       with a batch-size, as it can handle irregular frame intervals caused by processing
 *       delays and frame drops.
 *
 * Example Timeline:
 * @code
 * Output Framerate: 25 fps (expected latency: 40 ms)
 *
 * Frame 1: [0 ms]      (Initial frame, push frame)
 * Frame 2: [33 ms]     (Latency since last frame: 33 ms, accumulated_diff: 33 ms       -> Drop frame)
 * Frame 3: [66 ms]     (Latency since last frame: 33 ms, accumulated_diff: 33+33=66 ms -> Push frame, accumulated_diff
 * -= 40 ms) Frame 4: [99 ms]     (Latency since last frame: 33 ms, accumulated_diff: 26+33=59 ms -> Push frame,
 * accumulated_diff -= 40 ms) Frame 5: [132 ms]    (Latency since last frame: 33 ms, accumulated_diff: 19+33=52 ms ->
 * Push frame, accumulated_diff -= 40 ms) Frame 6: [165 ms]    (Latency since last frame: 33 ms, accumulated_diff:
 * 12+33=45 ms -> Push frame, accumulated_diff -= 40 ms) Frame 7: [198 ms]    (Latency since last frame: 33 ms,
 * accumulated_diff: 5+33=38 ms  -> Drop frame)
 * @endcode
 */
bool MediaLibraryMultiResize::Impl::should_push_frame_logic(uint32_t output_framerate, uint8_t output_index,
                                                            uint64_t isp_timestamp_ns)
{
    if (output_framerate == 0)
    {
        LOGGER__DEBUG("Skipping current frame because output framerate is 0, no need to acquire buffer {}",
                      output_index);
        return false;
    }

    float expected_frame_latency = 1000 / output_framerate;
    float latency_since_last_frame = (isp_timestamp_ns - m_timestamps[output_index].last_timestamp) / pow(10, 6);

    if (m_timestamps[output_index].last_timestamp == 0)
    {
        // We can't save `latency_since_last_frame` in the first frame, because the isp timestamp is not starting from
        // zero
        m_timestamps[output_index].accumulated_diff = expected_frame_latency;
    }
    else
    {
        // In case of jitter, limit the accumulated diff to `max_frames_jitter_multiplier` frames
        m_timestamps[output_index].accumulated_diff +=
            std::min(latency_since_last_frame, expected_frame_latency * max_frames_jitter_multiplier);

        m_timestamps[output_index].accumulated_diff = std::min(m_timestamps[output_index].accumulated_diff,
                                                               expected_frame_latency * max_frames_latency_multiplier);
    }

    m_timestamps[output_index].last_timestamp = isp_timestamp_ns;

    if (m_timestamps[output_index].accumulated_diff >= expected_frame_latency)
    {
        LOGGER__DEBUG("Should push frame, accumulated diff is {} and expected frame latency is {}",
                      m_timestamps[output_index].accumulated_diff, expected_frame_latency);
        m_timestamps[output_index].accumulated_diff -= expected_frame_latency;
        return true;
    }

    return false;
}

/**
 * @brief Acquire output buffers from buffer pools
 *
 * @param[in] input_frame - pointer to the input frame
 * @param[in] buffers - vector of output buffers
 */
media_library_return MediaLibraryMultiResize::Impl::acquire_output_buffers(
    HailoMediaLibraryBufferPtr input_buffer, std::vector<HailoMediaLibraryBufferPtr> &buffers)
{
    uint8_t num_of_outputs = GET_NUM_OF_OUTPUTS(m_multi_resize_config);

    for (uint8_t i = 0; i < num_of_outputs; i++)
    {
        HailoMediaLibraryBufferPtr buffer = std::make_shared<hailo_media_library_buffer>();
        auto output_res_expected = m_multi_resize_config.get_output_resolution_by_index(i);
        if (!output_res_expected.has_value())
        {
            return output_res_expected.error();
        }
        output_resolution_t &output_res = output_res_expected.value().get();
        bool should_acquire_buffer = should_push_frame_logic(output_res.framerate, i, input_buffer->isp_timestamp_ns);

        LOGGER__DEBUG("Acquiring buffer {}, target framerate is {}", i, output_res.framerate);
        if (!should_acquire_buffer)
        {
            LOGGER__DEBUG("Skipping current frame [framerate {}], no need to acquire buffer {}, counter is {}",
                          output_res.framerate, i, m_frame_counter);
            buffers.emplace_back(buffer);
            continue;
        }

        if (m_buffer_pools[i]->acquire_buffer(buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__WARNING("Failed to acquire buffer, skipping buffer");
            buffers.emplace_back(buffer);
            continue;
        }

        buffer->copy_metadata_from(input_buffer);
        buffers.emplace_back(buffer);
        LOGGER__DEBUG("buffer acquired successfully");
    }

    return MEDIA_LIBRARY_SUCCESS;
};

/* The telescopic multi-resize function in the DSP requires that the resolutions on each dsp_crop_resize_params_t
 * will be in descending order (for both width and height).
 * This function splits the output resolutions into groups of resolutions that can be resized together.
 */
static std::vector<dsp_crop_resize_params_t> split_to_crop_resize_params(std::vector<hailo_dsp_buffer_data_t> &outputs)
{
    std::vector<dsp_crop_resize_params_t> params;

    // Sort output resolutions (by width) from largest to smallest
    std::sort(outputs.begin(), outputs.end(), [](const hailo_dsp_buffer_data_t &a, const hailo_dsp_buffer_data_t &b) {
        return a.properties.width >= b.properties.width;
    });

    for (auto &output : outputs)
    {

        // Try to find a suitable crop_resize_param for the current output
        bool found = false;
        for (auto &crop_resize_param : params)
        {

            // Find the first empty slot (the first slot can be skipped since it's always initialized)
            size_t i = 1;
            while (i < DSP_MULTI_RESIZE_OUTPUTS_COUNT && crop_resize_param.dst[i] != nullptr)
            {
                i++;
            }

            // If no empty slot is found, continue to the next crop_resize_param
            if (i == DSP_MULTI_RESIZE_OUTPUTS_COUNT)
            {
                continue;
            }

            // Check if the current buffer can be added based on the width and height of the previous buffer
            // (a previous buffer exists since crop_resize_param is never added with an empty dst)
            if (crop_resize_param.dst[i - 1]->width >= output.properties.width &&
                crop_resize_param.dst[i - 1]->height >= output.properties.height)
            {
                crop_resize_param.dst[i] = &output.properties;
                found = true;
                break;
            }

            // the current buffer can't be added to the current crop_resize_param, continue to the next one
        }

        // If no suitable crop_resize_param was found, create a new one
        if (!found)
        {
            dsp_crop_resize_params_t new_param = {};
            new_param.dst[0] = &output.properties;
            params.push_back(new_param);
        }
    }

    return params;
}

tl::expected<dsp_roi_t, media_library_return> MediaLibraryMultiResize::Impl::get_input_roi()
{
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
                LOGGER__ERROR(
                    "Invalid digital zoom ROI. X ({}) and width ({}) coordinates exceed input frame width ({})",
                    start_x, digital_zoom_roi.width,
                    m_multi_resize_config.input_video_config.dimensions.destination_width);
                return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
            }

            if (end_y > m_multi_resize_config.input_video_config.dimensions.destination_height)
            {
                LOGGER__ERROR(
                    "Invalid digital zoom ROI. Y ({}) and height ({}) coordinates exceed input frame height ({})",
                    start_y, digital_zoom_roi.height,
                    m_multi_resize_config.input_video_config.dimensions.destination_height);
                return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
            }
        }
    }

    return dsp_roi_t{
        .start_x = start_x,
        .start_y = start_y,
        .end_x = end_x,
        .end_y = end_y,
    };
}

/**
 * @brief Perform multi resize on the DSP
 *
 * @param[in] input_frame - pointer to the input frame
 * @param[out] output_frames - vector of output frames
 */
media_library_return MediaLibraryMultiResize::Impl::perform_multi_resize(
    HailoMediaLibraryBufferPtr input_buffer, std::vector<HailoMediaLibraryBufferPtr> &output_frames)
{
    struct timespec start_resize, end_resize;
    size_t output_frames_size = output_frames.size();
    size_t num_of_output_resolutions = GET_NUM_OF_OUTPUTS(m_multi_resize_config);
    if (num_of_output_resolutions != output_frames_size)
    {
        LOGGER__ERROR("Number of output resolutions ({}) does not match number of output frames ({})",
                      num_of_output_resolutions, output_frames_size);
        return MEDIA_LIBRARY_ERROR;
    }

    hailo_dsp_buffer_data_t dsp_buffer_data = input_buffer->buffer_data->As<hailo_dsp_buffer_data_t>();

    std::vector<hailo_dsp_buffer_data_t> output_dsp_buffers;
    output_dsp_buffers.reserve(num_of_output_resolutions);

    uint num_bufs_to_resize = 0;
    for (size_t i = 0; i < num_of_output_resolutions; i++)
    {
        auto output_res_expected = m_multi_resize_config.get_output_resolution_by_index(i);
        if (!output_res_expected.has_value())
        {
            return output_res_expected.error();
        }
        output_resolution_t &output_res = output_res_expected.value().get();
        // TODO: Handle cases where its nullptr
        if (output_frames[i]->buffer_data == nullptr)
        {
            LOGGER__DEBUG("Skipping resize for output frame {} to match target framerate ({})", i,
                          output_res.framerate);
            continue;
        }

        hailo_buffer_data_t *output_frame = output_frames[i]->buffer_data.get();
        output_dsp_buffers.emplace_back(std::move(output_frame->As<hailo_dsp_buffer_data_t>()));

        if (output_res != *output_frame)
        {
            LOGGER__ERROR("Invalid output frame width {} output frame height {}", output_frame->width,
                          output_frame->height);
            return MEDIA_LIBRARY_ERROR;
        }

        LOGGER__DEBUG("Multi resize output frame ({}) - y_ptr = {}, uv_ptr = {}. dims: width = {}, output frame height "
                      "= {}, y plane fd = {}",
                      i, fmt::ptr(output_frame->planes[0].userptr), fmt::ptr(output_frame->planes[1].userptr),
                      output_frame->width, output_frame->height, output_frame->planes[0].fd);
        num_bufs_to_resize++;
    }

    if (num_bufs_to_resize == 0)
    {
        LOGGER__DEBUG("No need to perform multi resize");
        return MEDIA_LIBRARY_SUCCESS;
    }

    auto crop_resize_params = split_to_crop_resize_params(output_dsp_buffers);

    dsp_multi_crop_resize_params_t multi_crop_resize_params = {
        .src = &dsp_buffer_data.properties,
        .crop_resize_params = crop_resize_params.data(),
        .crop_resize_params_count = crop_resize_params.size(),
        .interpolation = m_multi_resize_config.output_video_config.interpolation_type,
    };

    auto input_roi = get_input_roi();
    if (!input_roi.has_value())
    {
        input_roi.error();
    }
    // Apply the input ROI to all crop_resize_params
    for (auto &p : crop_resize_params)
    {
        p.crop = &input_roi.value();
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
    std::optional<dsp_privacy_mask_t> dsp_privacy_mask;
    std::vector<dsp_roi_t> dsp_rois;

    if (privacy_mask_data->rois_count != 0)
    {
        dsp_privacy_mask.emplace();
        dsp_rois.resize(privacy_mask_data->rois_count);

        dsp_privacy_mask->bitmask = (uint8_t *)privacy_mask_data->bitmask->get_plane_ptr(0);
        dsp_privacy_mask->y_color = privacy_mask_data->color.y;
        dsp_privacy_mask->u_color = privacy_mask_data->color.u;
        dsp_privacy_mask->v_color = privacy_mask_data->color.v;
        dsp_privacy_mask->rois = dsp_rois.data();
        dsp_privacy_mask->rois_count = privacy_mask_data->rois_count;

        for (uint i = 0; i < privacy_mask_data->rois_count; i++)
        {
            dsp_rois[i] = {.start_x = privacy_mask_data->rois[i].x,
                           .start_y = privacy_mask_data->rois[i].y,
                           .end_x = privacy_mask_data->rois[i].x + privacy_mask_data->rois[i].width,
                           .end_y = privacy_mask_data->rois[i].y + privacy_mask_data->rois[i].height};
        }
    }

    // Using std::optional to manage the denoise parameters
    std::optional<dsp_image_enhancement_params_t> dsp_denoise_params;
    if (m_post_denoise_filter->m_denoise_element_enabled && m_post_denoise_filter->is_enabled())
    {
        dsp_denoise_params =
            std::make_optional<dsp_image_enhancement_params_t>(m_post_denoise_filter->get_dsp_denoise_params());

        if (dsp_denoise_params->histogram_params)
        {
            auto frame_size =
                std::make_pair(input_roi->end_x - input_roi->start_x, input_roi->end_y - input_roi->start_y);
            auto [x_sample_step, y_sample_step] = PostDenoiseFilter::histogram_sample_step_for_frame(frame_size);
            dsp_denoise_params->histogram_params->x_sample_step = x_sample_step;
            dsp_denoise_params->histogram_params->y_sample_step = y_sample_step;
            LOGGER__DEBUG("Denoise params: sharpness {} contrast {} brightness {} saturation_u_a {} saturation_u_b {} "
                          "saturation_v_a {} saturation_v_b {} histogram x_sample_step {} y_sample_step {} ",
                          dsp_denoise_params->sharpness, dsp_denoise_params->contrast, dsp_denoise_params->brightness,
                          dsp_denoise_params->saturation_u_a, dsp_denoise_params->saturation_u_b,
                          dsp_denoise_params->saturation_v_a, dsp_denoise_params->saturation_v_b,
                          dsp_denoise_params->histogram_params->x_sample_step,
                          dsp_denoise_params->histogram_params->y_sample_step);
        }
        else
        {
            LOGGER__DEBUG("Denoise params: sharpness {} contrast {} brightness {} saturation_u_a {} saturation_u_b {} "
                          "saturation_v_a {} saturation_v_b {}",
                          dsp_denoise_params->sharpness, dsp_denoise_params->contrast, dsp_denoise_params->brightness,
                          dsp_denoise_params->saturation_u_a, dsp_denoise_params->saturation_u_b,
                          dsp_denoise_params->saturation_v_a, dsp_denoise_params->saturation_v_b);
        }
    }

    LOGGER__DEBUG("Performing multi resize on the DSP with digital zoom ROI: start_x {} start_y {} end_x {} end_y "
                  "{} and {} privacy masks and post denoise filter",
                  input_roi->start_x, input_roi->start_y, input_roi->end_x, input_roi->end_y,
                  privacy_mask_data->rois_count);

    dsp_status ret = dsp_utils::perform_dsp_telescopic_multi_resize(
        &multi_crop_resize_params, dsp_privacy_mask ? &dsp_privacy_mask.value() : nullptr,
        dsp_denoise_params ? &dsp_denoise_params.value() : nullptr);

    if (m_post_denoise_filter->m_denoise_element_enabled && m_post_denoise_filter->is_enabled() &&
        dsp_denoise_params->histogram_params)
    {
        m_post_denoise_filter->set_dsp_denoise_params_from_histogram(dsp_denoise_params->histogram_params->histogram);
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

media_library_return MediaLibraryMultiResize::Impl::validate_output_frames(
    std::vector<HailoMediaLibraryBufferPtr> &output_frames)
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
        if (m_multi_resize_config.output_video_config.format != HAILO_FORMAT_NV12)
        {
            LOGGER__ERROR("Saturating to grayscale is enabled only for NV12 format");
            return MEDIA_LIBRARY_INVALID_ARGUMENT;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryMultiResize::Impl::handle_frame(HailoMediaLibraryBufferPtr input_frame,
                                                                 std::vector<HailoMediaLibraryBufferPtr> &output_frames)
{
    // Stamp start time
    struct timespec start_handle, end_handle;
    clock_gettime(CLOCK_MONOTONIC, &start_handle);

    if (validate_output_frames(output_frames) != MEDIA_LIBRARY_SUCCESS)
    {
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    std::shared_lock<std::shared_mutex> lock(rw_lock);

    // Acquire output buffers
    media_library_return media_lib_ret = MEDIA_LIBRARY_SUCCESS;
    media_lib_ret = acquire_output_buffers(input_frame, output_frames);
    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
    {
        return media_lib_ret;
    }

    // Handle grayscaling
    if (m_multi_resize_config.output_video_config.grayscale)
    {
        // Saturate UV plane to value of 128 - to get a grayscale image
        if (input_frame->is_dmabuf())
        {
            input_frame->sync_start(1);
            memset(input_frame->get_plane_ptr(1), 128, input_frame->get_plane_size(1));
            input_frame->sync_end(1);
        }
        else
        {
            memset(input_frame->get_plane_ptr(1), 128, input_frame->get_plane_size(1));
        }
    }

    // Perform multi resize
    media_lib_ret = perform_multi_resize(input_frame, output_frames);

    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
        return media_lib_ret;

    if (m_multi_resize_config.motion_detection_config.enabled)
    {
        media_lib_ret = m_motion_detection.perform_motion_detection(output_frames);

        if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
            return media_lib_ret;
    }

    increase_frame_counter();

    stamp_time_and_log_fps(start_handle, end_handle);
    return MEDIA_LIBRARY_SUCCESS;
}

multi_resize_config_t MediaLibraryMultiResize::Impl::get_multi_resize_configs()
{
    std::shared_lock<std::shared_mutex> lock(rw_lock);
    return m_multi_resize_config;
}

output_video_config_t MediaLibraryMultiResize::Impl::get_output_video_config()
{
    std::shared_lock<std::shared_mutex> lock(rw_lock);
    return m_multi_resize_config.output_video_config;
}

PrivacyMaskBlenderPtr MediaLibraryMultiResize::Impl::get_privacy_mask_blender()
{
    return m_privacy_mask_blender;
}

media_library_return MediaLibraryMultiResize::Impl::set_input_video_config(uint32_t width, uint32_t height,
                                                                           uint32_t framerate)
{
    std::unique_lock<std::shared_mutex> lock(rw_lock);
    m_multi_resize_config.input_video_config.dimensions.destination_width = width;
    m_multi_resize_config.input_video_config.dimensions.destination_height = height;
    m_multi_resize_config.input_video_config.framerate = framerate;

    media_library_return blender_config_status =
        m_privacy_mask_blender->set_frame_size(m_multi_resize_config.input_video_config.dimensions.destination_width,
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
