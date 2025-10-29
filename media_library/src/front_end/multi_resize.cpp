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

#include "opencv2/core/core.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"

#include "buffer_pool.hpp"
#include "config_manager.hpp"
#include "dsp_utils.hpp"
#include "media_library_logger.hpp"
#include "media_library_utils.hpp"
#include "motion_detection.hpp"
#include "multi_resize.hpp"
#include "privacy_mask.hpp"
#include "snapshot.hpp"

#include "dsp_image_enhancement.hpp"
#include <iostream>
#include <optional>
#include <shared_mutex>
#include <stdint.h>
#include <string>
#include <time.h>
#include <tl/expected.hpp>
#include <vector>
#include <shared_mutex>
#include <optional>
#include "env_vars.hpp"
#include "common.hpp"
#include "hailo_media_library_perfetto.hpp"

#define MODULE_NAME LoggerType::Resize

#define MAKE_EVEN(value) ((value) % 2 != 0 ? (value) + 1 : (value))
#define MAX_NUM_OF_OUTPUTS 8
#define GET_NUM_OF_OUTPUTS(multi_resize_config)                                                                        \
    ((multi_resize_config.application_input_streams_config.resolutions.size()) +                                       \
     (multi_resize_config.motion_detection_config.enabled ? 1 : 0))

/* Simple struct to aggregate output data and configuration together */
struct output_data_and_config
{
    hailo_dsp_buffer_data_t data;
    output_resolution_t *config;
};
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
    application_input_streams_config_t get_application_input_streams_config();

    // set the input video configurations object
    media_library_return set_input_video_config(uint32_t width, uint32_t height, uint32_t framerate);

    // set flip rotate status
    media_library_return set_do_flip_rotate(bool do_flip_rotate);

    // set the output video flip
    media_library_return set_output_flip(flip_direction_t flip);

    // set the output video rotation
    media_library_return set_output_rotation(rotation_angle_t rotation);

    // set the image enhancement status
    media_library_return set_image_enhancement_status(bool status);

    // set the callbacks object
    media_library_return observe(const MediaLibraryMultiResize::callbacks_t &callbacks);

  private:
    static constexpr int max_frames_jitter_multiplier = 3;
    static constexpr int max_frames_latency_multiplier = 20;
    static constexpr std::chrono::milliseconds wait_for_pools_timeout = std::chrono::milliseconds(1000);

    // flip-rotate flag
    bool m_do_flip_rotate;
    bool m_do_flip_rotate_override;

    // Multi-resize frame control logic
    bool m_use_div_framerate_logic;

    // flip direction
    flip_config_t m_flip_config = {false, FLIP_DIRECTION_NONE};

    // configured flag - to determine if first configuration was done
    bool m_configured;
    // frame counter - used internally for matching requested framerate
    uint m_frame_counter;
    // configuration manager
    std::shared_ptr<ConfigManager> m_config_manager;
    // operation configurations
    multi_resize_config_t m_multi_resize_config;
    // callbacks
    std::vector<MediaLibraryMultiResize::callbacks_t> m_callbacks;
    // output buffer pools
    std::vector<MediaLibraryBufferPoolPtr> m_buffer_pools;
    // Timestamps in ms.
    std::vector<timestamp_metadata> m_timestamps;
    // read/write lock for configuration manipulation/reading
    std::shared_mutex rw_lock;
    uint32_t m_max_buffer_pool_size;

    MotionDetection m_motion_detection;
    std::unique_ptr<DspImageEnhancement> m_dsp_image_enhancement;

    media_library_return decode_config_json_string(multi_resize_config_t &mresize_config, std::string config_string);
    media_library_return acquire_output_buffers(HailoMediaLibraryBufferPtr input_buffer,
                                                std::vector<HailoMediaLibraryBufferPtr> &buffers);
    bool should_push_frame_logic(uint32_t output_framerate, uint8_t output_index, uint64_t isp_timestamp_ns);
    bool should_push_frame_dividable_logic(uint32_t input_framerate, uint32_t output_framerate, uint32_t frame_counter,
                                           uint8_t output_index);
    bool should_push_frame_timestamp_logic(uint32_t output_framerate, uint8_t output_index, uint64_t isp_timestamp_ns,
                                           std::vector<timestamp_metadata> &timestamps);
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
    media_library_return status;
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN("MediaLibraryMultiResize::handle_frame", DSP_THREADED_TRACK);
    status = m_impl->handle_frame(input_frame, output_frames);
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(DSP_THREADED_TRACK);
    return status;
}

multi_resize_config_t MediaLibraryMultiResize::get_multi_resize_configs()
{
    return m_impl->get_multi_resize_configs();
}

application_input_streams_config_t MediaLibraryMultiResize::get_application_input_streams_config()
{
    return m_impl->get_application_input_streams_config();
}

media_library_return MediaLibraryMultiResize::set_input_video_config(uint32_t width, uint32_t height,
                                                                     uint32_t framerate)
{
    return m_impl->set_input_video_config(width, height, framerate);
}

media_library_return MediaLibraryMultiResize::set_do_flip_rotate(bool do_flip_rotate)
{
    return m_impl->set_do_flip_rotate(do_flip_rotate);
}

media_library_return MediaLibraryMultiResize::set_output_flip(flip_direction_t flip)
{
    return m_impl->set_output_flip(flip);
}

media_library_return MediaLibraryMultiResize::set_output_rotation(rotation_angle_t rotation)
{
    return m_impl->set_output_rotation(rotation);
}

media_library_return MediaLibraryMultiResize::set_image_enhancement_status(bool status)
{
    return m_impl->set_image_enhancement_status(status);
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
    : m_dsp_image_enhancement(std::make_unique<DspImageEnhancement>())
{
    m_configured = false;
    m_do_flip_rotate = !is_env_variable_on(MEDIALIB_DEWARP_DSP_OPTIMIZATION_ENV_VAR);
    m_do_flip_rotate_override = m_do_flip_rotate;

    m_use_div_framerate_logic = is_env_variable_on(MEDIALIB_USE_DIV_FRAMERATE_LOGIC_ENV_VAR);

    // Start frame count from 0 - to make sure we always handle the first frame even if framerate is set to 0
    m_frame_counter = 0;
    m_buffer_pools.reserve(MAX_NUM_OF_OUTPUTS);
    m_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_MULTI_RESIZE);
    m_multi_resize_config.application_input_streams_config.resolutions.reserve(MAX_NUM_OF_OUTPUTS);
    if (decode_config_json_string(m_multi_resize_config, config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to decode json string");
        status = MEDIA_LIBRARY_INVALID_ARGUMENT;
        return;
    }

    dsp_status dsp_ret = dsp_utils::acquire_device();
    if (dsp_ret != DSP_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to acquire DSP device, status: {}", dsp_ret);
        status = MEDIA_LIBRARY_OUT_OF_RESOURCES;
        return;
    }

    multi_resize_config_t mresize_config;
    mresize_config = m_multi_resize_config;
    m_motion_detection = MotionDetection(m_multi_resize_config.motion_detection_config);

    // revert the rotation to 0, so when we update the configuration we will correctly detect 90 degree rotation
    // and flip the output dimensions
    m_multi_resize_config.rotation_config.angle = ROTATION_ANGLE_0;
    if (configure(mresize_config) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure multi-resize");
        status = MEDIA_LIBRARY_CONFIGURATION_ERROR;
        return;
    }

    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryMultiResize::Impl::~Impl()
{
    m_multi_resize_config.application_input_streams_config.resolutions.clear();
    dsp_status status = dsp_utils::release_device();
    if (status != DSP_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to release DSP device, status: {}", status);
    }

    // Wait for all buffers to return to the pool before destruction.
    // We use a timeout to avoid hanging if some buffers are still in use by clients.
    // After timeout, destruction will proceed, potentially causing memory issues if buffers are accessed later.
    for (auto &buffer_pool : m_buffer_pools)
    {
        if (buffer_pool->wait_for_used_buffers(wait_for_pools_timeout) != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "Buffer pool {} failed to wait for used buffers, the buffer is probably in use",
                                  buffer_pool->get_name());
        }
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
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to decode json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return configure(mresize_config);
}

media_library_return MediaLibraryMultiResize::Impl::set_do_flip_rotate(bool do_flip_rotate)
{
    std::unique_lock<std::shared_mutex> lock(rw_lock);
    if (!m_do_flip_rotate_override)
        m_do_flip_rotate = do_flip_rotate;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryMultiResize::Impl::set_output_flip(flip_direction_t direction)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Setting output flip from {} to {}", m_flip_config.direction, direction);
    flip_config_t new_flip = {true, direction};
    std::unique_lock<std::shared_mutex> lock(rw_lock);
    m_flip_config = new_flip;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryMultiResize::Impl::set_output_rotation(rotation_angle_t angle)
{
    rotation_config_t new_rotation = {true, angle};
    rotation_config_t &current_rotation = m_multi_resize_config.rotation_config;
    if (current_rotation == new_rotation)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Output rotation is already set to {}", current_rotation.angle);
        return MEDIA_LIBRARY_SUCCESS;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Setting output rotation from {} to {}", current_rotation.angle,
                         new_rotation.angle);

    std::unique_lock<std::shared_mutex> lock(rw_lock);

    m_multi_resize_config.set_output_dimensions_rotation(new_rotation);
    auto output_res_expected = m_multi_resize_config.get_output_resolution_by_index(0);
    if (!output_res_expected.has_value())
    {
        return output_res_expected.error();
    }
    output_resolution_t &output_res = output_res_expected.value().get();
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Output rotation dims are now width {} height {}",
                          output_res.dimensions.destination_width, output_res.dimensions.destination_height);

    // recreate buffer pools if needed
    media_library_return ret = create_and_initialize_buffer_pools();
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to recreate buffer pool after setting output rotation");
        return ret;
    }

    lock.unlock();

    for (auto &callbacks : m_callbacks)
    {
        if (callbacks.on_output_resolutions_change)
            callbacks.on_output_resolutions_change(m_multi_resize_config.application_input_streams_config.resolutions);
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryMultiResize::Impl::set_image_enhancement_status(bool status)
{
    std::unique_lock<std::shared_mutex> lock(rw_lock);
    m_dsp_image_enhancement->m_denoise_element_enabled = status;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryMultiResize::Impl::configure(multi_resize_config_t &mresize_config)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Configuring multi-resize with new configurations");
    std::unique_lock<std::shared_mutex> lock(rw_lock);

    // Create and initialize buffer pools
    auto ret = m_multi_resize_config.update(mresize_config);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to update multi-resize configurations (prohibited) {}", ret);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Create and initialize buffer pools
    ret = create_and_initialize_buffer_pools();
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    ret = m_motion_detection.allocate_motion_detection(m_max_buffer_pool_size);
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    m_timestamps.clear();
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
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to update multi-resize configurations (prohibited) {}", ret);
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
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Buffer pool already exists, skipping creation");
            continue;
        }

        auto bytes_per_line = dsp_utils::get_dsp_desired_stride_from_width(width);
        LOGGER__MODULE__INFO(
            MODULE_NAME,
            "Creating buffer pool named {} for output resolution: width {} height {} in buffers size of {} "
            "and bytes per line {}",
            name, width, height, output_res.pool_max_buffers, bytes_per_line);
        MediaLibraryBufferPoolPtr buffer_pool = std::make_shared<MediaLibraryBufferPool>(
            width, height, m_multi_resize_config.application_input_streams_config.format, output_res.pool_max_buffers,
            HAILO_MEMORY_TYPE_DMABUF, bytes_per_line, name);
        if (buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init buffer pool");
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
    LOGGER__MODULE__DEBUG(MODULE_NAME, "multi-resize holding {} buffer pools", m_buffer_pools.size());

    return MEDIA_LIBRARY_SUCCESS;
}

/**
 * @brief Helper function that determines whether to push a frame for evenly dividable framerates.
 *
 * @param[in] input_framerate The framerate of the input video.
 * @param[in] output_framerate The desired framerate for the output.
 * @param[in] frame_counter Current frame counter.
 * @param[in] output_index The index of the output buffer to check.
 *
 * @return `true` if the frame should be pushed to the buffer, `false` otherwise.
 */
bool MediaLibraryMultiResize::Impl::should_push_frame_dividable_logic(uint32_t input_framerate,
                                                                      uint32_t output_framerate, uint32_t frame_counter,
                                                                      uint8_t output_index)
{
    uint32_t divisor = input_framerate / output_framerate;

    // Using the frame counter to determine if this frame should be pushed or dropped
    // Example: for divisor 2 (30fps -> 15fps), push frames 1,3,5..., drop frames 2,4,6...
    // Example: for divisor 3 (30fps -> 10fps), push frames 1,4,7..., drop frames 2,3,5,6,8,9...
    bool should_push = (input_framerate == output_framerate) || (frame_counter % divisor == 1);
    if (should_push)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME,
                              "Pushing frame for output {} (dividable case). Frame counter: {}, Input fps: {}, "
                              "Output fps: {}, Divisor: {}",
                              output_index, frame_counter, input_framerate, output_framerate, divisor);
        return true;
    }
    else
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME,
                              "Dropping frame for output {} (dividable case). Frame counter: {}, Input fps: {}, "
                              "Output fps: {}, Divisor: {}",
                              output_index, frame_counter, input_framerate, output_framerate, divisor);
        return false;
    }
}

/**
 * @brief Helper function that determines whether to push a frame using timestamp-based approach.
 *
 * @param[in] output_framerate The desired framerate for the output.
 * @param[in] output_index The index of the output buffer to check.
 * @param[in] isp_timestamp_ns The timestamp of the frame from the ISP in nanoseconds.
 * @param[in,out] timestamps Vector of timestamp metadata for tracking output timing.
 *
 * @return `true` if the frame should be pushed to the buffer, `false` otherwise.
 */
bool MediaLibraryMultiResize::Impl::should_push_frame_timestamp_logic(uint32_t output_framerate, uint8_t output_index,
                                                                      uint64_t isp_timestamp_ns,
                                                                      std::vector<timestamp_metadata> &timestamps)
{
    // Fallback to the timestamp-based approach for non-dividable framerates
    float expected_frame_latency = 1000 / output_framerate;
    float latency_since_last_frame = (isp_timestamp_ns - timestamps[output_index].last_timestamp) / pow(10, 6);

    if (timestamps[output_index].last_timestamp == 0)
    {
        // We can't save `latency_since_last_frame` in the first frame, because the isp timestamp is not starting
        // from zero
        timestamps[output_index].accumulated_diff = expected_frame_latency;
    }
    else
    {
        // In case of jitter, limit the accumulated diff to `max_frames_jitter_multiplier` frames
        timestamps[output_index].accumulated_diff +=
            std::min(latency_since_last_frame, expected_frame_latency * max_frames_jitter_multiplier);

        timestamps[output_index].accumulated_diff =
            std::min(timestamps[output_index].accumulated_diff, expected_frame_latency * max_frames_latency_multiplier);
    }

    timestamps[output_index].last_timestamp = isp_timestamp_ns;

    if (timestamps[output_index].accumulated_diff >= expected_frame_latency)
    {
        LOGGER__MODULE__DEBUG(
            MODULE_NAME, "Should push frame (timestamp case), accumulated diff is {} and expected frame latency is {}",
            timestamps[output_index].accumulated_diff, expected_frame_latency);
        timestamps[output_index].accumulated_diff -= expected_frame_latency;
        return true;
    }

    return false;
}

/**
 * @brief Determines whether a frame should be pushed based on the output framerate.
 *
 * This function uses two different approaches to determine if a frame should be pushed:
 *
 * 1. For evenly dividable framerates (input_framerate % output_framerate == 0):
 *    Uses a simple pattern-based approach that relies on the frame counter to determine
 *    whether to pass or drop a frame. This ensures perfect consistency in framerate reduction.
 *
 * 2. For non-dividable framerates:
 *    Falls back to a timestamp-based approach that calculates the latency since the last frame
 *    and compares it to the expected frame latency based on the output framerate.
 *
 * @param[in] output_framerate The desired framerate for the output in frames per second (fps).
 * @param[in] output_index The index of the output buffer to check.
 * @param[in] isp_timestamp_ns The timestamp of the frame from the ISP in nanoseconds.
 *
 * @return `true` if the frame should be pushed to the buffer, `false` otherwise.
 *
 * @note If `output_framerate` is 0, the function skips the current frame.
 *
 * Example for dividable framerate:
 * @code
 * Input Framerate: 30 fps, Output Framerate: 15 fps (divisor = 2)
 * Frame 1: Push (counter % 2 = 1)
 * Frame 2: Drop (counter % 2 = 0)
 * Frame 3: Push (counter % 2 = 1)
 * Frame 4: Drop (counter % 2 = 0)
 *
 * Input Framerate: 30 fps, Output Framerate: 10 fps (divisor = 3)
 * Frame 1: Push (counter % 3 = 1)
 * Frame 2: Drop (counter % 3 = 2)
 * Frame 3: Drop (counter % 3 = 0)
 * Frame 4: Push (counter % 3 = 1)
 * @endcode
 *
 * Example for non-dividable framerate (timestamp-based approach):
 * @code
 * Output Framerate: 25 fps (expected latency: 40 ms)
 * Frame 1: [0 ms]      (Initial frame, push frame)
 * Frame 2: [33 ms]     (Latency: 33 ms, accumulated_diff: 33 ms       -> Drop frame)
 * Frame 3: [66 ms]     (Latency: 33 ms, accumulated_diff: 66 ms -> Push frame, accumulated_diff -= 40 ms)
 * Frame 4: [99 ms]     (Latency: 33 ms, accumulated_diff: 59 ms -> Push frame, accumulated_diff -= 40 ms)
 * @endcode
 */
bool MediaLibraryMultiResize::Impl::should_push_frame_logic(uint32_t output_framerate, uint8_t output_index,
                                                            uint64_t isp_timestamp_ns)
{
    if (output_framerate == 0)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME,
                              "Skipping current frame because output framerate is 0, no need to acquire buffer {}",
                              output_index);
        return false;
    }

    uint32_t input_framerate = m_multi_resize_config.input_video_config.framerate;

    // Check if the output framerate is dividable by the input framerate
    // For example: input 30 fps, output 15 fps (30/15 = 2, which is an integer)
    // or input 30 fps, output 10 fps (30/10 = 3, which is an integer)
    if (m_use_div_framerate_logic && input_framerate > 0 && input_framerate % output_framerate == 0)
    {
        return should_push_frame_dividable_logic(input_framerate, output_framerate, m_frame_counter, output_index);
    }
    else
    {
        return should_push_frame_timestamp_logic(output_framerate, output_index, isp_timestamp_ns, m_timestamps);
    }
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

        LOGGER__MODULE__DEBUG(MODULE_NAME, "Acquiring buffer {}, target framerate is {}", i, output_res.framerate);
        if (!should_acquire_buffer)
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME,
                                  "Skipping current frame [framerate {}], no need to acquire buffer {}, counter is {}",
                                  output_res.framerate, i, m_frame_counter);
            buffers.emplace_back(buffer);
            continue;
        }

        if (m_buffer_pools[i]->acquire_buffer(buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__WARNING(MODULE_NAME, "Failed to acquire buffer, skipping buffer");
            buffers.emplace_back(buffer);
            continue;
        }

        buffer->copy_metadata_from(input_buffer);
        buffers.emplace_back(buffer);
        LOGGER__MODULE__DEBUG(MODULE_NAME, "buffer acquired successfully");
    }

    return MEDIA_LIBRARY_SUCCESS;
};

cv::Size expand_to_aspect_ratio(float target_aspect_ratio, const cv::Size &size)
{
    float aspect_ratio = static_cast<float>(size.width) / size.height;

    size_t new_width, new_height;
    if (aspect_ratio > target_aspect_ratio)
    {
        // adjust height to maintain aspect ratio
        new_width = size.width;
        new_height = std::ceil(size.width / target_aspect_ratio);
    }
    else
    {
        // adjust width to maintain aspect ratio
        new_width = std::ceil(size.height * target_aspect_ratio);
        new_height = size.height;
    }

    // NV12 requires even resolutions
    new_width += new_width % 2;
    new_height += new_height % 2;

    return cv::Size(new_width, new_height);
}

cv::Size shrink_to_aspect_ratio(float target_aspect_ratio, const cv::Size &size)
{
    float aspect_ratio = static_cast<float>(size.width) / size.height;

    size_t new_width, new_height;
    if (aspect_ratio < target_aspect_ratio)
    {
        // truncate height to maintain aspect ratio
        new_width = size.width;
        new_height = std::ceil(size.width / target_aspect_ratio);
    }
    else
    {
        // truncate width to maintain aspect ratio
        new_width = std::ceil(size.height * target_aspect_ratio);
        new_height = size.height;
    }

    // NV12 requires even resolutions
    new_width += new_width % 2;
    new_height += new_height % 2;

    return cv::Size(new_width, new_height);
}

cv::Size adjust_to_aspect_ratio(float target_aspect_ratio, const cv::Size &size, dsp_scaling_mode_t scaling_mode)
{
    if (scaling_mode == DSP_SCALING_MODE_SCALE_AND_CROP)
    {
        return expand_to_aspect_ratio(target_aspect_ratio, size);
    }
    else if (scaling_mode == DSP_SCALING_MODE_LETTERBOX_MIDDLE || scaling_mode == DSP_SCALING_MODE_LETTERBOX_UP_LEFT)
    {
        return shrink_to_aspect_ratio(target_aspect_ratio, size);
    }
    else
    {
        return size;
    }
}

/* The telescopic multi-resize function in the DSP requires that the resolutions on each dsp_crop_resize_params_t
 * will be in descending order (for both width and height).
 * This function splits the output resolutions into groups of resolutions that can be resized together.
 */
static std::vector<dsp_crop_resize_params_t> split_to_crop_resize_params(std::vector<output_data_and_config> &outputs,
                                                                         const dsp_roi_t &input_roi)
{
    std::vector<dsp_crop_resize_params_t> params;
    auto src_width = input_roi.end_x - input_roi.start_x;
    auto src_height = input_roi.end_y - input_roi.start_y;
    float src_aspect_ratio = static_cast<float>(src_width) / src_height;

    // Sort output resolutions (by width) from largest to smallest - after adjusting to aspect ratio
    std::sort(outputs.begin(), outputs.end(),
              [src_aspect_ratio](const output_data_and_config &a, const output_data_and_config &b) {
                  cv::Size size_a(a.config->dimensions.destination_width, a.config->dimensions.destination_height);
                  cv::Size size_b(b.config->dimensions.destination_width, b.config->dimensions.destination_height);
                  auto scaling_mode_a = a.config->scaling_mode;
                  auto scaling_mode_b = b.config->scaling_mode;
                  cv::Size scaled_size_a = adjust_to_aspect_ratio(src_aspect_ratio, size_a, scaling_mode_a);
                  cv::Size scaled_size_b = adjust_to_aspect_ratio(src_aspect_ratio, size_b, scaling_mode_b);
                  return scaled_size_a.width > scaled_size_b.width;
              });

    for (auto &[output, output_config] : outputs)
    {
        cv::Size curr_size(output_config->dimensions.destination_width, output_config->dimensions.destination_height);
        auto curr_scaling_mode = output_config->scaling_mode;
        cv::Size curr_scaled_size = adjust_to_aspect_ratio(src_aspect_ratio, curr_size, curr_scaling_mode);

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
            cv::Size prev_size(crop_resize_param.dst[i - 1]->width, crop_resize_param.dst[i - 1]->height);
            auto prev_scaling_mode = crop_resize_param.scaling_params[i - 1].scaling_mode;
            cv::Size prev_scaled_size = adjust_to_aspect_ratio(src_aspect_ratio, prev_size, prev_scaling_mode);

            if (prev_scaled_size.width >= curr_scaled_size.width && prev_scaled_size.height >= curr_scaled_size.height)
            {
                crop_resize_param.dst[i] = &output.properties;
                crop_resize_param.scaling_params[i].scaling_mode = curr_scaling_mode;
                crop_resize_param.scaling_params[i].color.y = 0;
                crop_resize_param.scaling_params[i].color.u = 128;
                crop_resize_param.scaling_params[i].color.v = 128;
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
            new_param.scaling_params[0].scaling_mode = curr_scaling_mode;
            new_param.scaling_params[0].color.y = 0;
            new_param.scaling_params[0].color.u = 128;
            new_param.scaling_params[0].color.v = 128;
            params.push_back(new_param);
        }
    }

    return params;
}

tl::expected<dsp_roi_t, media_library_return> MediaLibraryMultiResize::Impl::get_input_roi()
{
    auto input_width = m_multi_resize_config.input_video_config.dimensions.destination_width;
    auto input_height = m_multi_resize_config.input_video_config.dimensions.destination_height;
    if (m_do_flip_rotate && (m_multi_resize_config.rotation_config.effective_value() == ROTATION_ANGLE_90 ||
                             m_multi_resize_config.rotation_config.effective_value() == ROTATION_ANGLE_270))
    {
        std::swap(input_width, input_height);
    }

    uint start_x = 0;
    uint start_y = 0;
    uint end_x = input_width;
    uint end_y = input_height;

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
            if (end_x > input_width)
            {
                LOGGER__MODULE__ERROR(
                    MODULE_NAME,
                    "Invalid digital zoom ROI. X ({}) and width ({}) coordinates exceed input frame width ({})",
                    start_x, digital_zoom_roi.width, input_width);
                return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
            }

            if (end_y > input_height)
            {
                LOGGER__MODULE__ERROR(
                    MODULE_NAME,
                    "Invalid digital zoom ROI. Y ({}) and height ({}) coordinates exceed input frame height ({})",
                    start_y, digital_zoom_roi.height, input_height);
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
        LOGGER__MODULE__ERROR(MODULE_NAME,
                              "Number of output resolutions ({}) does not match number of output frames ({})",
                              num_of_output_resolutions, output_frames_size);
        return MEDIA_LIBRARY_ERROR;
    }

    hailo_dsp_buffer_data_t src_dsp_buffer_data = input_buffer->buffer_data->As<hailo_dsp_buffer_data_t>();

    std::vector<output_data_and_config> outputs_data_and_config;
    outputs_data_and_config.reserve(num_of_output_resolutions);

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
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Skipping resize for output frame {} to match target framerate ({})", i,
                                  output_res.framerate);
            continue;
        }

        hailo_buffer_data_t *output_frame = output_frames[i]->buffer_data.get();
        outputs_data_and_config.emplace_back(std::move(output_frame->As<hailo_dsp_buffer_data_t>()), &output_res);

        if (output_res != *output_frame)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Invalid output frame width {} output frame height {}",
                                  output_frame->width, output_frame->height);
            return MEDIA_LIBRARY_ERROR;
        }

        LOGGER__MODULE__DEBUG(
            MODULE_NAME,
            "Multi resize output frame ({}) - y_ptr = {}, uv_ptr = {}. dims: width = {}, output frame height "
            "= {}, y plane fd = {}",
            i, fmt::ptr(output_frame->planes[0].userptr), fmt::ptr(output_frame->planes[1].userptr),
            output_frame->width, output_frame->height, output_frame->planes[0].fd);
        num_bufs_to_resize++;
    }

    if (num_bufs_to_resize == 0)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "No need to perform multi resize");
        return MEDIA_LIBRARY_SUCCESS;
    }

    auto input_roi = get_input_roi();
    if (!input_roi.has_value())
    {
        return input_roi.error();
    }

    auto crop_resize_params = split_to_crop_resize_params(outputs_data_and_config, input_roi.value());

    dsp_multi_crop_resize_params_t multi_crop_resize_params = {
        .src = &src_dsp_buffer_data.properties,
        .crop_resize_params = crop_resize_params.data(),
        .crop_resize_params_count = crop_resize_params.size(),
        .interpolation = m_multi_resize_config.application_input_streams_config.interpolation_type,
    };

    // Apply the input ROI to all crop_resize_params
    for (auto &p : crop_resize_params)
    {
        p.crop = &input_roi.value();
    }

    // Perform multi resize
    clock_gettime(CLOCK_MONOTONIC, &start_resize);

    std::optional<dsp_image_enhancement_params_t> dsp_image_enhancement_params;
    if (m_dsp_image_enhancement->is_enabled())
    {
        /* If denoise is disabled only histogram equalization can be applied */
        dsp_image_enhancement_params = m_dsp_image_enhancement->m_denoise_element_enabled
                                           ? m_dsp_image_enhancement->get_dsp_params()
                                           : m_dsp_image_enhancement->get_default_disabled_dsp_params();

        LOGGER__MODULE__DEBUG(
            MODULE_NAME,
            "Image enhancement params: "
            "contrast {} brightness {} saturation_u_a {} saturation_u_b {} saturation_v_a {} saturation_v_b {} "
            "blur level {} "
            "sharpness level {} amount {} threshold {}",
            dsp_image_enhancement_params->color.contrast, dsp_image_enhancement_params->color.brightness,
            dsp_image_enhancement_params->color.saturation_u_a, dsp_image_enhancement_params->color.saturation_u_b,
            dsp_image_enhancement_params->color.saturation_v_a, dsp_image_enhancement_params->color.saturation_v_b,
            dsp_image_enhancement_params->blur.level, dsp_image_enhancement_params->sharpness.level,
            dsp_image_enhancement_params->sharpness.amount, dsp_image_enhancement_params->sharpness.threshold);

        if (dsp_image_enhancement_params->histogram_params)
        {
            auto frame_size =
                std::make_pair(input_roi->end_x - input_roi->start_x, input_roi->end_y - input_roi->start_y);
            auto [x_sample_step, y_sample_step] = DspImageEnhancement::histogram_sample_step_for_frame(frame_size);
            dsp_image_enhancement_params->histogram_params->x_sample_step = x_sample_step;
            dsp_image_enhancement_params->histogram_params->y_sample_step = y_sample_step;
            LOGGER__MODULE__DEBUG(MODULE_NAME,
                                  "Image enhancement histogram params: "
                                  "histogram x_sample_step {} y_sample_step {} ",
                                  dsp_image_enhancement_params->histogram_params->x_sample_step,
                                  dsp_image_enhancement_params->histogram_params->y_sample_step);
        }
    }

    LOGGER__MODULE__DEBUG(
        MODULE_NAME,
        "Performing multi resize on the DSP with digital zoom ROI: start_x {} start_y {} end_x {} end_y "
        "{} and post denoise filter",
        input_roi->start_x, input_roi->start_y, input_roi->end_x, input_roi->end_y);

    // enable only if not already done in dewarp
    std::optional<dsp_flip_rotate_params_t> dsp_flip_rotate_params;
    if (m_do_flip_rotate)
    {
        dsp_flip_rotate_params = dsp_flip_rotate_params_t{
            .flip_dir = static_cast<dsp_flip_direction_t>(m_flip_config.effective_value()),
            .rot_ang = static_cast<dsp_rotation_angle_t>(m_multi_resize_config.rotation_config.effective_value()),
        };
    }

    const dsp_frontend_params_t dsp_frontend_params = {
        .multi_crop_resize_params = &multi_crop_resize_params,
        .privacy_mask_params = nullptr,
        .image_enhancement_params = dsp_image_enhancement_params ? &dsp_image_enhancement_params.value() : nullptr,
        .flip_rotate_params = dsp_flip_rotate_params ? &dsp_flip_rotate_params.value() : nullptr,
    };
    dsp_status ret = dsp_utils::perform_dsp_frontend_process(dsp_frontend_params);

    clock_gettime(CLOCK_MONOTONIC, &end_resize);
    [[maybe_unused]] long ms = (long)media_library_difftimespec_ms(end_resize, start_resize);
    LOGGER__MODULE__TRACE(MODULE_NAME, "perform_multi_resize took {} milliseconds ({} fps)", ms, 1000 / ms);

    if (ret != DSP_SUCCESS)
        return MEDIA_LIBRARY_DSP_OPERATION_ERROR;

    if (m_dsp_image_enhancement->is_enabled() && dsp_image_enhancement_params->histogram_params)
    {
        m_dsp_image_enhancement->update_dsp_params_from_histogram(
            m_dsp_image_enhancement->m_denoise_element_enabled,
            dsp_image_enhancement_params->histogram_params->histogram);
    }

    return MEDIA_LIBRARY_SUCCESS;
}

void MediaLibraryMultiResize::Impl::stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle)
{
    clock_gettime(CLOCK_MONOTONIC, &end_handle);
    long ms = (long)media_library_difftimespec_ms(end_handle, start_handle);
    uint framerate = 1000 / ms;
    LOGGER__MODULE__DEBUG(MODULE_NAME, "multi-resize handle_frame took {} milliseconds ({} fps)", ms, framerate);
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
        LOGGER__MODULE__ERROR(MODULE_NAME, "output_frames vector is not empty - an empty vector is required");
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    // Check that caps match between incoming frames and output frames?

    if (m_multi_resize_config.application_input_streams_config.grayscale)
    {
        if (m_multi_resize_config.application_input_streams_config.format != HAILO_FORMAT_NV12)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Saturating to grayscale is enabled only for NV12 format");
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
    if (m_multi_resize_config.application_input_streams_config.grayscale)
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
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN("perform_multi_resize", DSP_THREADED_TRACK);
    media_lib_ret = perform_multi_resize(input_frame, output_frames);
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(DSP_THREADED_TRACK);

    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
        return media_lib_ret;

    if (m_multi_resize_config.motion_detection_config.enabled)
    {
        media_lib_ret = m_motion_detection.perform_motion_detection(output_frames);

        if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
            return media_lib_ret;
    }

    for (size_t i = 0; i < output_frames.size(); i++)
    {
        // In cases where we have multiple fps outputs, the frame might be null if the buffer was shouldn't be pushed
        if (output_frames[i]->buffer_data == nullptr)
        {
            LOGGER__MODULE__TRACE(MODULE_NAME, "Output frame at index {} is empty, skipping snapshot", i);
            continue;
        }

        SnapshotManager::get_instance().take_snapshot("multiresize" + std::to_string(i), output_frames[i]);
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

application_input_streams_config_t MediaLibraryMultiResize::Impl::get_application_input_streams_config()
{
    std::shared_lock<std::shared_mutex> lock(rw_lock);
    return m_multi_resize_config.application_input_streams_config;
}

media_library_return MediaLibraryMultiResize::Impl::set_input_video_config(uint32_t width, uint32_t height,
                                                                           uint32_t framerate)
{
    std::unique_lock<std::shared_mutex> lock(rw_lock);
    m_multi_resize_config.input_video_config.dimensions.destination_width = width;
    m_multi_resize_config.input_video_config.dimensions.destination_height = height;
    m_multi_resize_config.input_video_config.framerate = framerate;

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryMultiResize::Impl::observe(const MediaLibraryMultiResize::callbacks_t &callbacks)
{
    m_callbacks.push_back(callbacks);
    return MEDIA_LIBRARY_SUCCESS;
}
