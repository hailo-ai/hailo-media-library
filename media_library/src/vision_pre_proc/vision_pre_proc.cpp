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

#include <iostream>
#include <stdint.h>
#include <string>
#include <time.h>
#include <tl/expected.hpp>
#include <vector>

#include "buffer_pool.hpp"
#include "config_manager.hpp"
#include "dsp_utils.hpp"
#include "generate_mesh.hpp"
#include "media_library_logger.hpp"
#include "vision_pre_proc.hpp"

/**
 * Gets the time difference between 2 time specs in milliseconds.
 * @param[in] after     The second time spec.
 * @param[in] before    The first time spec.\
 * @returns The time differnece in milliseconds.
 */
int64_t media_library_difftimespec_ms(const struct timespec after,
                                      const struct timespec before)
{
    return ((int64_t)after.tv_sec - (int64_t)before.tv_sec) * (int64_t)1000 +
           ((int64_t)after.tv_nsec - (int64_t)before.tv_nsec) / 1000000;
}

class MediaLibraryVisionPreProc::Impl final
{
public:
    static tl::expected<std::shared_ptr<MediaLibraryVisionPreProc::Impl>,
                        media_library_return>
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

    // Configure the pre-processing module with pre_proc_op_configurations
    // object
    media_library_return
    configure(pre_proc_op_configurations &pre_proc_op_configs);

    // Perform pre-processing on the input frame and return the output frames
    media_library_return
    handle_frame(hailo_media_library_buffer &input_frame,
                 std::vector<hailo_media_library_buffer> &output_frames);

    // get the pre-processing configurations object
    pre_proc_op_configurations &get_pre_proc_configs();

    // get the output video configurations object
    output_video_config_t &get_output_video_config();

private:
    // Pointer to internally allocated DIS instance. used for DIS library mesh
    // generation
    void *m_dis_ctx;
    // dewarp mesh object
    dsp_dewarp_mesh_t m_dewarp_mesh;
    // configured flag - to determine if first configuration was done
    bool m_configured;
    // frame counter - used internally for matching requested framerate
    uint m_frame_counter;
    size_t m_dewarp_output_width;
    size_t m_dewarp_output_height;
    // configuration manager
    std::shared_ptr<ConfigManager> m_config_manager;
    // operation configurations
    pre_proc_op_configurations m_pre_proc_configs;
    // input buffer pools
    MediaLibraryBufferPoolPtr m_input_buffer_pool;
    // output buffer pools
    std::vector<MediaLibraryBufferPoolPtr> m_buffer_pools;
    media_library_return
    validate_configurations(pre_proc_op_configurations &pre_proc_configs);
    media_library_return
    decode_config_json_string(pre_proc_op_configurations &pre_proc_configs,
                              std::string config_string);
    media_library_return
    acquire_output_buffers(std::vector<hailo_media_library_buffer> &buffers,
                           output_video_config_t &output_video_config);
    media_library_return create_and_initialize_buffer_pools();
    media_library_return initialize_dewarp_mesh();
    media_library_return validate_input_and_output_frames(
        hailo_media_library_buffer &input_frame,
        std::vector<hailo_media_library_buffer> &output_frames);
    media_library_return
    perform_dewarp(hailo_media_library_buffer &input_buffer,
                   hailo_media_library_buffer &dewarp_output_buffer);
    media_library_return perform_multi_resize(
        hailo_media_library_buffer &input_buffer,
        std::vector<hailo_media_library_buffer> &output_frames);
    media_library_return perform_dewarp_and_multi_resize(
        hailo_media_library_buffer &input_frame,
        std::vector<hailo_media_library_buffer> &output_frames);
    void stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle);
    void increase_frame_counter();
};

//------------------------ MediaLibraryVisionPreProc ------------------------
tl::expected<std::shared_ptr<MediaLibraryVisionPreProc>, media_library_return>
MediaLibraryVisionPreProc::create(std::string config_string)
{
    auto impl_expected = Impl::create(config_string);
    if (impl_expected.has_value())
        return std::make_shared<MediaLibraryVisionPreProc>(
            impl_expected.value());
    else
        return tl::make_unexpected(impl_expected.error());
}

MediaLibraryVisionPreProc::MediaLibraryVisionPreProc(
    std::shared_ptr<MediaLibraryVisionPreProc::Impl> impl)
    : m_impl(impl)
{
}

MediaLibraryVisionPreProc::~MediaLibraryVisionPreProc() = default;

media_library_return
MediaLibraryVisionPreProc::configure(std::string config_string)
{
    return m_impl->configure(config_string);
}

media_library_return MediaLibraryVisionPreProc::configure(
    pre_proc_op_configurations &pre_proc_op_configs)
{
    return m_impl->configure(pre_proc_op_configs);
}

media_library_return MediaLibraryVisionPreProc::handle_frame(
    hailo_media_library_buffer &input_frame,
    std::vector<hailo_media_library_buffer> &output_frames)
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

//------------------------ MediaLibraryVisionPreProc::Impl
//------------------------

tl::expected<std::shared_ptr<MediaLibraryVisionPreProc::Impl>,
             media_library_return>
MediaLibraryVisionPreProc::Impl::create(std::string config_string)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    // MediaLibraryVisionPreProc::Impl vision_preproc(status, config_string);
    std::shared_ptr<MediaLibraryVisionPreProc::Impl> vision_preproc =
        std::make_shared<MediaLibraryVisionPreProc::Impl>(status,
                                                          config_string);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return vision_preproc;
}

MediaLibraryVisionPreProc::Impl::Impl(media_library_return &status,
                                      std::string config_string)
{
    m_dis_ctx = nullptr;
    m_configured = false;

    // Start frame count from 0 - to make sure we always handle the first frame
    // even if framerate is set to 0
    m_frame_counter = 0;
    m_buffer_pools.reserve(5);
    m_config_manager =
        std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_VISION);
    m_pre_proc_configs.output_video_config.resolutions.reserve(5);
    if (decode_config_json_string(m_pre_proc_configs, config_string) !=
        MEDIA_LIBRARY_SUCCESS)
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
    if (m_configured && m_pre_proc_configs.dewarp_config.enabled)
    {
        media_library_return ret = free_mesh(&m_dis_ctx, m_dewarp_mesh);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to free mesh, status: {}", ret);
        }
    }

    dsp_status status = dsp_utils::release_device();
    if (status != DSP_SUCCESS)
    {
        LOGGER__ERROR("Failed to acquire DSP device, status: {}", status);
    }
}

media_library_return MediaLibraryVisionPreProc::Impl::decode_config_json_string(
    pre_proc_op_configurations &pre_proc_configs, std::string config_string)
{
    return m_config_manager
        ->config_string_to_struct<pre_proc_op_configurations>(config_string,
                                                              pre_proc_configs);
}

media_library_return
MediaLibraryVisionPreProc::Impl::configure(std::string config_string)
{
    pre_proc_op_configurations pre_proc_configs;
    if (decode_config_json_string(pre_proc_configs, config_string) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return configure(pre_proc_configs);
}

media_library_return MediaLibraryVisionPreProc::Impl::validate_configurations(
    pre_proc_op_configurations &pre_proc_op_configs)
{
    // Make sure that the output fps of each stream is divided by the input fps
    // with no reminder
    output_resolution_t &input_res =
        pre_proc_op_configs.input_video_config.resolution;
    for (output_resolution_t &output_res :
         pre_proc_op_configs.output_video_config.resolutions)
    {
        if (output_res.framerate != 0 &&
            input_res.framerate % output_res.framerate != 0)
        {
            LOGGER__ERROR("Invalid output framerate {} - must be a divider of "
                          "the input framerate {}",
                          output_res.framerate, input_res.framerate);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    if (m_pre_proc_configs.dewarp_config.enabled)
    {
        // Validate dewarp configurations
        if (m_pre_proc_configs.dewarp_config.camera_fov > 160.f &&
            m_pre_proc_configs.dewarp_config.camera_type == CAMERA_TYPE_PINHOLE)
        {
            LOGGER__ERROR("Invalid value for camera_fov ({}) for a pin-hole "
                          "camera type, must be lower than 160",
                          m_pre_proc_configs.dewarp_config.camera_fov);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }
    else
    {
        if (m_pre_proc_configs.dis_config.enabled)
            LOGGER__WARNING("DIS feature is enabled in the configuration, but "
                            "dewarp is disabled. DIS will not be performed");
        if (m_pre_proc_configs.flip_config.enabled)
            LOGGER__WARNING("Flip feature is enabled in the configuration, but "
                            "dewarp is disabled. Flip will not be performed");
        if (m_pre_proc_configs.rotation_config.enabled)
            LOGGER__WARNING(
                "Rotation feature is enabled in the configuration, but dewarp "
                "is disabled. Rotation will not be performed");
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryVisionPreProc::Impl::configure(
    pre_proc_op_configurations &pre_proc_op_configs)
{
    if (validate_configurations(pre_proc_op_configs) != MEDIA_LIBRARY_SUCCESS)
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;

    if (m_pre_proc_configs.dewarp_config.enabled &&
        m_pre_proc_configs.rotation_config.enabled &&
        (m_pre_proc_configs.rotation_config.angle == ROTATION_ANGLE_90 ||
         m_pre_proc_configs.rotation_config.angle == ROTATION_ANGLE_270))
    {
        // Swap width and height for rotation 90 or 270
        m_dewarp_output_width = pre_proc_op_configs.input_video_config
                                    .resolution.dimensions.destination_height;
        m_dewarp_output_height = pre_proc_op_configs.input_video_config
                                     .resolution.dimensions.destination_width;

        for (output_resolution_t &output_res :
             pre_proc_op_configs.output_video_config.resolutions)
        {
            size_t outp_height = output_res.dimensions.destination_height;
            output_res.dimensions.destination_height =
                output_res.dimensions.destination_width;
            output_res.dimensions.destination_width = outp_height;
        }
    }
    else
    {
        m_dewarp_output_width = pre_proc_op_configs.input_video_config
                                    .resolution.dimensions.destination_width;
        m_dewarp_output_height = pre_proc_op_configs.input_video_config
                                     .resolution.dimensions.destination_height;
    }

    // Is it first time configuration?
    if (m_configured)
    {
        // No - update configurations and validate that no prohibited changes
        // were made
        return m_pre_proc_configs.update(pre_proc_op_configs);
    }

    // Create and initialize buffer pools
    media_library_return ret = create_and_initialize_buffer_pools();
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    if (m_pre_proc_configs.dewarp_config.enabled)
    {
        // Yes - initialize mesh
        ret = initialize_dewarp_mesh();
        if (ret != MEDIA_LIBRARY_SUCCESS)
            return ret;
    }

    m_configured = true;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryVisionPreProc::Impl::initialize_dewarp_mesh()
{
    output_resolution_t &input_res =
        m_pre_proc_configs.input_video_config.resolution;
    media_library_return ret = init_mesh(
        &m_dis_ctx, m_dewarp_mesh, m_pre_proc_configs.dewarp_config,
        m_pre_proc_configs.dis_config, input_res.dimensions.destination_width,
        input_res.dimensions.destination_height);

    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    LOGGER__INFO("Generating mesh");
    flip_direction_t flip_direction = FLIP_DIRECTION_NONE;
    if (m_pre_proc_configs.flip_config.enabled)
        flip_direction = m_pre_proc_configs.flip_config.direction;

    rotation_angle_t rotation_angle = ROTATION_ANGLE_0;
    if (m_pre_proc_configs.rotation_config.enabled)
        rotation_angle = m_pre_proc_configs.rotation_config.angle;

    // Generate dewarp mesh
    ret = generate_dewarp_only_mesh(m_dis_ctx, m_dewarp_mesh,
                                    input_res.dimensions.destination_width,
                                    input_res.dimensions.destination_height,
                                    flip_direction, rotation_angle);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to generate mesh, status: {}", ret);
        return ret;
    }

    return ret;
}

media_library_return
MediaLibraryVisionPreProc::Impl::create_and_initialize_buffer_pools()
{
    output_resolution_t &input_res =
        m_pre_proc_configs.input_video_config.resolution;
    m_input_buffer_pool = std::make_shared<MediaLibraryBufferPool>(
        (uint)m_dewarp_output_width, (uint)m_dewarp_output_height,
        m_pre_proc_configs.input_video_config.format,
        (uint)input_res.pool_max_buffers, CMA);
    if (m_input_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to init buffer pool");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    for (output_resolution_t &output_res :
         m_pre_proc_configs.output_video_config.resolutions)
    {
        LOGGER__INFO("Creating buffer pool for output resolution: width {} "
                     "height {} in buffers size of {}",
                     output_res.dimensions.destination_width,
                     output_res.dimensions.destination_height,
                     output_res.pool_max_buffers);
        MediaLibraryBufferPoolPtr buffer_pool =
            std::make_shared<MediaLibraryBufferPool>(
                (uint)output_res.dimensions.destination_width,
                (uint)output_res.dimensions.destination_height,
                m_pre_proc_configs.output_video_config.format,
                output_res.pool_max_buffers, CMA);
        if (buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to init buffer pool");
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
        m_buffer_pools.emplace_back(buffer_pool);
    }

    return MEDIA_LIBRARY_SUCCESS;
}

/**
 * @brief Acquire output buffers from buffer pools
 *
 * @param[in] buffers - vector of output buffers
 * @param[in] output_video_config - output video configuration
 */
media_library_return MediaLibraryVisionPreProc::Impl::acquire_output_buffers(
    std::vector<hailo_media_library_buffer> &buffers,
    output_video_config_t &output_video_config)
{
    // Acquire output buffers
    uint8_t output_size = output_video_config.resolutions.size();
    for (uint8_t i = 0; i < output_size; i++)
    {
        uint32_t framerate = output_video_config.resolutions[i].framerate;
        LOGGER__INFO("Acquiring buffer {}, target framerate is {}", i,
                     framerate);

        // TODO: Change from const 30 to the configurable value
        uint stream_period = (30 / framerate);
        bool should_acquire_buffer = (m_frame_counter % stream_period == 0);
        LOGGER__DEBUG("frame counter is {}, stream period is {}, should "
                      "acquire buffer is {}",
                      m_frame_counter, stream_period, should_acquire_buffer);

        hailo_media_library_buffer buffer;

        if (!should_acquire_buffer)
        {
            LOGGER__INFO("Skipping current frame to match framerate {}, no "
                         "need to acquire buffer {}, counter is {}",
                         framerate, i, m_frame_counter);
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
 * @brief Perform dewarp
 * Generate dewarp mesh, acquire buffer for dewarp output and perform dewarp on
 * the DSP
 *
 * @param[in] input_frame - pointer to the input frame
 * @param[out] dewarp_output_buffer - dewarp output buffer
 * @param[in] vsm - pointer to the vsm object
 */
media_library_return MediaLibraryVisionPreProc::Impl::perform_dewarp(
    hailo_media_library_buffer &input_buffer,
    hailo_media_library_buffer &dewarp_output_buffer)
{
    struct timespec start_dewarp, end_dewarp;
    if (m_pre_proc_configs.dis_config.enabled)
    {
        // Update dewarp mesh with the VSM data to perform DIS
        LOGGER__INFO("Updating mesh with VSM");
        flip_direction_t flip_direction = FLIP_DIRECTION_NONE;
        if (m_pre_proc_configs.flip_config.enabled)
            flip_direction = m_pre_proc_configs.flip_config.direction;

        rotation_angle_t rotation_angle = ROTATION_ANGLE_0;
        if (m_pre_proc_configs.rotation_config.enabled)
            rotation_angle = m_pre_proc_configs.rotation_config.angle;

        media_library_return media_lib_ret = generate_mesh(
            m_dis_ctx, m_dewarp_mesh, input_buffer.hailo_pix_buffer->width,
            input_buffer.hailo_pix_buffer->height, input_buffer.vsm,
            flip_direction, rotation_angle);
        if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to update mesh with VSM, status: {}",
                          media_lib_ret);
            return media_lib_ret;
        }
    }

    // Acquire buffer for dewarp output
    if (m_input_buffer_pool->acquire_buffer(dewarp_output_buffer) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        // log: failed to acquire buffer for dewarp output
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    // Perform dewarp
    LOGGER__INFO("Performing dewarp with interpolation type {}",
                 m_pre_proc_configs.dewarp_config.interpolation_type);
    clock_gettime(CLOCK_MONOTONIC, &start_dewarp);
    dsp_status ret = dsp_utils::perform_dsp_dewarp(
        input_buffer.hailo_pix_buffer.get(),
        dewarp_output_buffer.hailo_pix_buffer.get(), &m_dewarp_mesh,
        m_pre_proc_configs.dewarp_config.interpolation_type);
    clock_gettime(CLOCK_MONOTONIC, &end_dewarp);
    long ms = (long)media_library_difftimespec_ms(end_dewarp, start_dewarp);
    uint framerate = 1000 / ms;
    LOGGER__DEBUG("perform_dsp_dewarp took {} milliseconds ({} fps)", ms,
                  framerate);

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
    hailo_media_library_buffer &input_buffer,
    std::vector<hailo_media_library_buffer> &output_frames)
{
    struct timespec start_resize, end_resize;
    size_t output_frames_size = output_frames.size();
    size_t num_of_output_resolutions =
        m_pre_proc_configs.output_video_config.resolutions.size();
    if (num_of_output_resolutions != output_frames_size)
    {
        LOGGER__ERROR("Number of output resolutions ({}) does not match number "
                      "of output frames ({})",
                      num_of_output_resolutions, output_frames_size);
        return MEDIA_LIBRARY_ERROR;
    }

    dsp_multi_resize_params_t multi_resize_params = {
        .src = input_buffer.hailo_pix_buffer.get(),
        .interpolation =
            m_pre_proc_configs.output_video_config.interpolation_type,
    };

    uint num_bufs_to_resize = 0;
    for (size_t i = 0; i < num_of_output_resolutions; i++)
    {
        // TODO: Handle cases where its nullptr
        if (output_frames[i].hailo_pix_buffer == nullptr)
        {
            LOGGER__INFO(
                "Skipping resize for output frame {} to match target framerate",
                i);
            continue;
        }
        dsp_image_properties_t *output_frame =
            output_frames[i].hailo_pix_buffer.get();
        output_resolution_t &output_res =
            m_pre_proc_configs.output_video_config.resolutions[i];

        if (output_res != *output_frame)
        {
            LOGGER__ERROR(
                "Invalid output frame width {} output frame height {}",
                output_frame->width, output_frame->height);
            return MEDIA_LIBRARY_ERROR;
        }

        multi_resize_params.dst[num_bufs_to_resize] = output_frame;
        LOGGER__DEBUG("Multi resize output frame ({}) - y_ptr = {}, uv_ptr = "
                      "{}. dims: width {} output frame height {}",
                      i, fmt::ptr(output_frame->planes[0].userptr),
                      fmt::ptr(output_frame->planes[1].userptr),
                      output_frame->width, output_frame->height);
        num_bufs_to_resize++;
    }

    if (num_bufs_to_resize == 0)
    {
        LOGGER__INFO("No need to perform multi resize");
        return MEDIA_LIBRARY_SUCCESS;
    }

    uint start_x = 0;
    uint start_y = 0;
    uint end_x = m_dewarp_output_width;
    uint end_y = m_dewarp_output_height;

    if (m_pre_proc_configs.digital_zoom_config.enabled)
    {
        if (m_pre_proc_configs.digital_zoom_config.mode ==
            DIGITAL_ZOOM_MODE_MAGNIFICATION)
        {
            uint center_x = m_dewarp_output_width / 2;
            uint center_y = m_dewarp_output_height / 2;
            uint zoom_width =
                center_x / m_pre_proc_configs.digital_zoom_config.magnification;
            uint zoom_height =
                center_y / m_pre_proc_configs.digital_zoom_config.magnification;
            start_x = center_x - zoom_width;
            start_y = center_y - zoom_height;
            end_x = center_x + zoom_width;
            end_y = center_y + zoom_height;
        }
        else
        {
            roi_t &digital_zoom_roi =
                m_pre_proc_configs.digital_zoom_config.roi;
            start_x = digital_zoom_roi.x;
            start_y = digital_zoom_roi.y;
            end_x = start_x + digital_zoom_roi.width;
            end_y = start_y + digital_zoom_roi.height;

            // Validate digital zoom ROI values with the input frame dimensions
            if (end_x > m_dewarp_output_width)
            {
                LOGGER__ERROR("Invalid digital zoom ROI. X ({}) and width ({}) "
                              "coordinates exceed input frame width ({})",
                              start_x, digital_zoom_roi.width,
                              m_dewarp_output_width);
                return MEDIA_LIBRARY_ERROR;
            }

            if (end_y > m_dewarp_output_height)
            {
                LOGGER__ERROR("Invalid digital zoom ROI. Y ({}) and height "
                              "({}) coordinates exceed input frame height ({})",
                              start_y, digital_zoom_roi.height,
                              m_dewarp_output_height);
                return MEDIA_LIBRARY_ERROR;
            }
        }
    }

    // Perform multi resize
    LOGGER__INFO("Performing multi resize on the DSP with digital zoom ROI: "
                 "start_x {} start_y {} end_x {} end_y {}",
                 start_x, start_y, end_x, end_y);
    clock_gettime(CLOCK_MONOTONIC, &start_resize);
    dsp_status ret = dsp_utils::perform_dsp_multi_resize(
        &multi_resize_params, start_x, start_y, end_x, end_y);
    clock_gettime(CLOCK_MONOTONIC, &end_resize);
    long ms = (long)media_library_difftimespec_ms(end_resize, start_resize);
    uint framerate = 1000 / ms;
    LOGGER__DEBUG("perform_multi_resize took {} milliseconds ({} fps)", ms, framerate);

    if (ret != DSP_SUCCESS)
        return MEDIA_LIBRARY_DSP_OPERATION_ERROR;

    return MEDIA_LIBRARY_SUCCESS;
}

void MediaLibraryVisionPreProc::Impl::stamp_time_and_log_fps(
    timespec &start_handle, timespec &end_handle)
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
    hailo_media_library_buffer &input_frame,
    std::vector<hailo_media_library_buffer> &output_frames)
{
    // Perform dewarp and multi resize
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    hailo_media_library_buffer dewarp_output_buffer;

    ret = perform_dewarp(input_frame, dewarp_output_buffer);

    if (m_pre_proc_configs.output_video_config.grayscale)
    {
        // Saturate UV plane to value of 128 - to get a grayscale image
        dsp_data_plane_t &uv_plane = dewarp_output_buffer.hailo_pix_buffer->planes[1];
        memset(uv_plane.userptr, 128, uv_plane.bytesused);
    }

    if (ret == MEDIA_LIBRARY_SUCCESS)
        ret = perform_multi_resize(dewarp_output_buffer, output_frames);

    LOGGER__DEBUG("decrease ref dewarp output buffer");
    dewarp_output_buffer.decrease_ref_count();
    return ret;
}

media_library_return
MediaLibraryVisionPreProc::Impl::validate_input_and_output_frames(
    hailo_media_library_buffer &input_frame,
    std::vector<hailo_media_library_buffer> &output_frames)
{
    output_resolution_t &input_res =
        m_pre_proc_configs.input_video_config.resolution;
    dsp_image_properties_t *input_image_properties =
        input_frame.hailo_pix_buffer.get();

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
        LOGGER__ERROR("Invalid input frame width {} input frame height {}",
                      input_image_properties->width,
                      input_image_properties->height);
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    if (m_pre_proc_configs.output_video_config.grayscale)
    {
        if (m_pre_proc_configs.output_video_config.format != DSP_IMAGE_FORMAT_NV12)
        {
            LOGGER__ERROR("Saturate to gray is enabled only for NV12 format");
            return MEDIA_LIBRARY_INVALID_ARGUMENT;
        }
        if (!m_pre_proc_configs.dewarp_config.enabled)
        {
            LOGGER__ERROR("Saturate to gray is not supported without dewarp");
            return MEDIA_LIBRARY_INVALID_ARGUMENT;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryVisionPreProc::Impl::handle_frame(
    hailo_media_library_buffer &input_frame,
    std::vector<hailo_media_library_buffer> &output_frames)
{
    // Stamp start time
    struct timespec start_handle, end_handle;
    clock_gettime(CLOCK_MONOTONIC, &start_handle);

    if (validate_input_and_output_frames(input_frame, output_frames) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        input_frame.decrease_ref_count();
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    // Acquire output buffers
    media_library_return media_lib_ret = MEDIA_LIBRARY_SUCCESS;
    media_lib_ret = acquire_output_buffers(
        output_frames, m_pre_proc_configs.output_video_config);
    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
    {
        input_frame.decrease_ref_count();
        return media_lib_ret;
    }

    // Dewarp and multi resize
    if (m_pre_proc_configs.dewarp_config.enabled)
        media_lib_ret = perform_dewarp_and_multi_resize(input_frame, output_frames);
    else
        media_lib_ret = perform_multi_resize(input_frame, output_frames);

    // Unref the input frame
    input_frame.decrease_ref_count();

    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
        return media_lib_ret;

    increase_frame_counter();

    stamp_time_and_log_fps(start_handle, end_handle);

    return MEDIA_LIBRARY_SUCCESS;
}

pre_proc_op_configurations &
MediaLibraryVisionPreProc::Impl::get_pre_proc_configs()
{
    return m_pre_proc_configs;
}

output_video_config_t &
MediaLibraryVisionPreProc::Impl::get_output_video_config()
{
    return m_pre_proc_configs.output_video_config;
}