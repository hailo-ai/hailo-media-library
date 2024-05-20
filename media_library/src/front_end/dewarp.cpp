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

#include "dewarp.hpp"
#include "buffer_pool.hpp"
#include "config_manager.hpp"
#include "dsp_utils.hpp"
#include "ldc_mesh_context.hpp"
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
#include <shared_mutex>

#define HAILO15_ISP_CID_LSC_BASE (V4L2_CID_USER_BASE + 0x3200)
#define HAILO15_ISP_CID_LSC_OPTICAL_ZOOM (HAILO15_ISP_CID_LSC_BASE + 0x0009)

class MediaLibraryDewarp::Impl final
{
public:
    static tl::expected<std::shared_ptr<MediaLibraryDewarp::Impl>, media_library_return>
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

    // Configure the pre-processing module with ldc_config_t object
    media_library_return configure(ldc_config_t &ldc_configs);

    bool check_ops_enabled_from_config_string(std::string config_string);
    
    // Perform pre-processing on the input frame and return the output frames
    media_library_return handle_frame(hailo_media_library_buffer &input_frame, hailo_media_library_buffer &output_frame);

    // get the pre-processing configurations object
    ldc_config_t &get_ldc_configs();

    // get the input video configurations object
    input_video_config_t &get_input_video_config();

    // get the output video configurations object
    output_resolution_t &get_output_video_config();

    // set magnification level of optical zoom
    media_library_return set_optical_zoom(float magnification);

    // set the input video configurations
    media_library_return set_input_video_config(uint32_t width, uint32_t height, uint32_t framerate, dsp_image_format_t format);

    // set the callbacks object
    media_library_return observe(const MediaLibraryDewarp::callbacks_t &callbacks);

private:
    std::unique_ptr<LdcMeshContext> m_dewarp_mesh_ctx;
    // configured flag - to determine if first configuration was done
    bool m_configured;
    // frame counter - used internally for matching requested framerate
    uint m_frame_counter;
    // configuration manager
    std::shared_ptr<ConfigManager> m_config_manager;
    // operation configurations
    ldc_config_t m_ldc_configs;
    // last vsm
    struct hailo15_vsm m_last_vsm;
    // output buffer pool
    MediaLibraryBufferPoolPtr m_output_buffer_pool;
    // video fd
    int m_video_fd;
    // configuration mutex
    std::shared_mutex rw_lock;

    std::vector<MediaLibraryDewarp::callbacks_t> m_callbacks;
    media_library_return decode_config_json_string(ldc_config_t &ldc_configs, std::string config_string);
    media_library_return create_and_initialize_buffer_pools();
    media_library_return validate_input_frame(hailo_media_library_buffer &input_frame);
    media_library_return perform_dewarp(hailo_media_library_buffer &input_buffer, hailo_media_library_buffer &dewarp_output_buffer);
    media_library_return perform_angular_dis_dewarp(hailo_media_library_buffer &input_buffer, hailo_media_library_buffer &dewarp_output_buffer, dsp_image_properties_t *image, dsp_dewarp_mesh_t *mesh);
    void stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle);
    void increase_frame_counter();
};

//------------------------ MediaLibraryDewarp ------------------------
tl::expected<std::shared_ptr<MediaLibraryDewarp>, media_library_return> MediaLibraryDewarp::create(std::string config_string)
{
    auto impl_expected = Impl::create(config_string);
    if (impl_expected.has_value())
        return std::make_shared<MediaLibraryDewarp>(impl_expected.value());
    else
        return tl::make_unexpected(impl_expected.error());
}

MediaLibraryDewarp::MediaLibraryDewarp(std::shared_ptr<MediaLibraryDewarp::Impl> impl) : m_impl(impl) {}

MediaLibraryDewarp::~MediaLibraryDewarp() = default;

media_library_return MediaLibraryDewarp::configure(std::string config_string)
{
    return m_impl->configure(config_string);
}

bool MediaLibraryDewarp::check_ops_enabled_from_config_string(std::string config_string)
{
    return m_impl->check_ops_enabled_from_config_string(config_string);
}

media_library_return MediaLibraryDewarp::configure(ldc_config_t &ldc_configs)
{
    return m_impl->configure(ldc_configs);
}

media_library_return MediaLibraryDewarp::handle_frame(hailo_media_library_buffer &input_frame, hailo_media_library_buffer &output_frame)
{
    return m_impl->handle_frame(input_frame, output_frame);
}

ldc_config_t &MediaLibraryDewarp::get_ldc_configs()
{
    return m_impl->get_ldc_configs();
}

input_video_config_t &MediaLibraryDewarp::get_input_video_config()
{
    return m_impl->get_input_video_config();
}

output_resolution_t &MediaLibraryDewarp::get_output_video_config()
{
    return m_impl->get_output_video_config();
}

media_library_return MediaLibraryDewarp::set_optical_zoom(float magnification)
{
    return m_impl->set_optical_zoom(magnification);
}

media_library_return MediaLibraryDewarp::set_input_video_config(uint32_t width, uint32_t height, uint32_t framerate, dsp_image_format_t format)
{
    return m_impl->set_input_video_config(width, height, framerate, format);
}

media_library_return MediaLibraryDewarp::observe(const MediaLibraryDewarp::callbacks_t &callbacks)
{
    return m_impl->observe(callbacks);
}

//------------------------ MediaLibraryDewarp::Impl ------------------------

tl::expected<std::shared_ptr<MediaLibraryDewarp::Impl>, media_library_return> MediaLibraryDewarp::Impl::create(std::string config_string)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    std::shared_ptr<MediaLibraryDewarp::Impl> dewarp = std::make_shared<MediaLibraryDewarp::Impl>(status, config_string);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return dewarp;
}

MediaLibraryDewarp::Impl::Impl(media_library_return &status, std::string config_string)
{
    m_configured = false;
    m_video_fd = -1;
    m_last_vsm.dx = 0;
    m_last_vsm.dy = 0;

    // Start frame count from 0 - to make sure we always handle the first frame even if framerate is set to 0
    m_frame_counter = 0;
    m_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_LDC);
    if (decode_config_json_string(m_ldc_configs, config_string) != MEDIA_LIBRARY_SUCCESS)
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

    m_dewarp_mesh_ctx = std::make_unique<LdcMeshContext>(m_ldc_configs);
    if (configure(m_ldc_configs) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to configure dewarp");
        status = MEDIA_LIBRARY_CONFIGURATION_ERROR;
        return;
    }

    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryDewarp::Impl::~Impl()
{
    m_dewarp_mesh_ctx = nullptr;
    dsp_status status = dsp_utils::release_device();
    if (status != DSP_SUCCESS)
    {
        LOGGER__ERROR("Failed to release DSP device, status: {}", status);
    }
}

media_library_return MediaLibraryDewarp::Impl::decode_config_json_string(ldc_config_t &ldc_configs, std::string config_string)
{
    return m_config_manager->config_string_to_struct<ldc_config_t>(config_string, ldc_configs);
}

bool MediaLibraryDewarp::Impl::check_ops_enabled_from_config_string(std::string config_string)
{
    ldc_config_t ldc_configs;
    LOGGER__INFO("Configuring dewarp Decoding json string");
    if (decode_config_json_string(ldc_configs, config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode json string: {}", config_string);
    }
    if(ldc_configs.dewarp_config.enabled||ldc_configs.dis_config.enabled||ldc_configs.flip_config.enabled||ldc_configs.rotation_config.enabled||ldc_configs.optical_zoom_config.enabled)
    {
        return true;
    }
    else
    {
        return false;
    }
}

media_library_return MediaLibraryDewarp::Impl::configure(std::string config_string)
{
    ldc_config_t ldc_configs;
    LOGGER__INFO("Configuring dewarp Decoding json string");
    if (decode_config_json_string(ldc_configs, config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return configure(ldc_configs);
}

media_library_return MediaLibraryDewarp::Impl::configure(ldc_config_t & ldc_configs)
{
    LOGGER__INFO("Configuring dewarp");

    std::unique_lock<std::shared_mutex> lock(rw_lock);

    auto prev_out_config = m_ldc_configs.output_video_config;
    auto prev_rot_config = m_ldc_configs.rotation_config;
    // update if requested
    media_library_return ret = m_ldc_configs.update(ldc_configs);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to update dewarp configurations (prohibited) {}", ret);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // skip mesh context configure if caps not set yet
    if (m_ldc_configs.output_video_config.dimensions.destination_width == 0 || m_ldc_configs.output_video_config.dimensions.destination_height == 0)
    {
        LOGGER__INFO("Skipping dewarp mesh configuration since input_video_config not set yet");
        return MEDIA_LIBRARY_SUCCESS;
    }

    if (m_ldc_configs.dewarp_config.enabled)
    {
        // Validate dewarp configurations
        if (m_ldc_configs.dewarp_config.camera_fov > 160.f && m_ldc_configs.dewarp_config.camera_type == CAMERA_TYPE_PINHOLE)
        {
            LOGGER__ERROR("Invalid value for camera_fov ({}) for a pin-hole camera type, must be lower than 160", m_ldc_configs.dewarp_config.camera_fov);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
        m_ldc_configs.dewarp_config.camera_type = CAMERA_TYPE_PINHOLE;
    }
    else
    {
        if (m_ldc_configs.dis_config.enabled || m_ldc_configs.flip_config.enabled || m_ldc_configs.rotation_config.enabled)
        {
            LOGGER__INFO("Dewarp is disabled, but other features are enabled. Enabling dewarp in identity mode (ldc will not be performed).");
            m_ldc_configs.dewarp_config.enabled = true;
            m_ldc_configs.dewarp_config.camera_type = CAMERA_TYPE_INPUT_DISTORTIONS;
        }
    }

    m_dewarp_mesh_ctx->configure(m_ldc_configs);

    // Create and initialize buffer pools
    ret = create_and_initialize_buffer_pools();
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    // if output config has changed, call callback
    bool rot_changed = m_ldc_configs.rotation_config != prev_rot_config;
    bool out_changed = !m_ldc_configs.output_video_config.dimensions_equal(prev_out_config);

    for (auto &callbacks : m_callbacks)
    {
        if (out_changed && callbacks.on_output_resolution_change)
            callbacks.on_output_resolution_change(m_ldc_configs.output_video_config);
        if (rot_changed && callbacks.on_rotation_change)
        {
            auto rot_val = m_ldc_configs.rotation_config.effective_value();
            callbacks.on_rotation_change(rot_val);
        }
    }

    m_configured = true;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDewarp::Impl::create_and_initialize_buffer_pools()
{
    uint width, height;
    width = m_ldc_configs.output_video_config.dimensions.destination_width;
    height = m_ldc_configs.output_video_config.dimensions.destination_height;
    std::string name = "dewarp_output";

    if (m_output_buffer_pool != nullptr && width == m_output_buffer_pool->get_width() && height == m_output_buffer_pool->get_height())
    {
        LOGGER__DEBUG("Buffer pool already exists, skipping creation");
        return MEDIA_LIBRARY_SUCCESS;
    }

    auto bytes_per_line = dsp_utils::get_dsp_desired_stride_from_width(width);
    // Forcing output video buffer pool to be max 5 buffers.
    m_ldc_configs.output_video_config.pool_max_buffers = 5;
    LOGGER__INFO("Creating buffer pool named {} for output resolution: width {} height {} in buffers size of {} and bytes per line {}", name, width, height, m_ldc_configs.output_video_config.pool_max_buffers, bytes_per_line);
    m_output_buffer_pool = std::make_shared<MediaLibraryBufferPool>(width, height, m_ldc_configs.input_video_config.format, (uint)m_ldc_configs.output_video_config.pool_max_buffers, CMA, bytes_per_line, name);
    if (m_output_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to init buffer pool");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDewarp::Impl::perform_angular_dis_dewarp(hailo_media_library_buffer & input_buffer, hailo_media_library_buffer & dewarp_output_buffer, dsp_image_properties_t * image, dsp_dewarp_mesh_t * mesh)
{
    std::shared_ptr<angular_dis_params_t> angular_dis_params = m_dewarp_mesh_ctx->get_angular_dis_params();

    float cur_angles_sum = *(angular_dis_params->dsp_filter_angle->cur_angles_sum);
    float cur_traj = *(angular_dis_params->dsp_filter_angle->cur_traj);
    LOGGER__DEBUG("Perform Angular dewarp previous alpha = {} cur angles sum = {} cur traj = {}", angular_dis_params->dsp_filter_angle->alpha, cur_angles_sum, cur_traj);

    dsp_filter_angle_t filter_angle_ptr = {
        .maximum_theta = angular_dis_params->dsp_filter_angle->maximum_theta,
        .alpha = angular_dis_params->dsp_filter_angle->alpha,
        .prev_angles_sum = cur_angles_sum,
        .prev_traj = cur_traj,
        .cur_angles_sum = angular_dis_params->dsp_filter_angle->cur_angles_sum.get(),
        .cur_traj = angular_dis_params->dsp_filter_angle->cur_traj.get(),
        .stabilized_theta = angular_dis_params->dsp_filter_angle->stabilized_theta.get()};

    dsp_vsm_config_t vsm_config = {
        .hoffset = angular_dis_params->dsp_vsm_config.hoffset,
        .voffset = angular_dis_params->dsp_vsm_config.voffset,
        .width = angular_dis_params->dsp_vsm_config.width,
        .height = angular_dis_params->dsp_vsm_config.height,
        .max_displacement = angular_dis_params->dsp_vsm_config.max_displacement};

    dsp_isp_vsm_t isp_vsm = {
        .center_x = angular_dis_params->isp_vsm.center_x,
        .center_y = angular_dis_params->isp_vsm.center_y,
        .dx = angular_dis_params->isp_vsm.dx,
        .dy = angular_dis_params->isp_vsm.dy};

    dsp_status ret = dsp_utils::perform_dsp_dewarp(
        input_buffer.hailo_pix_buffer.get(),
        image, mesh,
        m_ldc_configs.dewarp_config.interpolation_type,
        isp_vsm,
        vsm_config,
        filter_angle_ptr,
        angular_dis_params->cur_columns_sum,
        angular_dis_params->cur_rows_sum,
        angular_dis_params->stabilize_rotation);

    if (ret != DSP_SUCCESS)
        return MEDIA_LIBRARY_DSP_OPERATION_ERROR;

    // First time dewarp is performed without mesh correction, Afterwards we need to correct the mesh
    angular_dis_params->stabilize_rotation = true;

    return MEDIA_LIBRARY_SUCCESS;
}

/**
 * @brief Perform dewarp
 * Generate dewarp mesh, acquire buffer for dewarp output and perform dewarp on
 * the DSP
 *
 * @param[in] input_frame - pointer to the input frame
 * @param[out] dewarp_output_buffer - dewarp output buffer
 * @param[in] vsm - pointer to the vsm object
 */
media_library_return MediaLibraryDewarp::Impl::perform_dewarp(
    hailo_media_library_buffer & input_buffer,
    hailo_media_library_buffer & dewarp_output_buffer)
{
    struct timespec start_dewarp, end_dewarp;

    // Acquire buffer for dewarp output
    if (m_output_buffer_pool->acquire_buffer(dewarp_output_buffer) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        // log: failed to acquire buffer for dewarp output
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    // Perform dewarp
    dsp_dewarp_mesh_t *mesh = m_dewarp_mesh_ctx->get();
    dsp_image_properties_t *image = dewarp_output_buffer.hailo_pix_buffer.get();
    LOGGER__TRACE("Performing dewarp with mesh (w={}, h={}) interpolation type {}", mesh->mesh_width, mesh->mesh_height, m_ldc_configs.dewarp_config.interpolation_type);
    clock_gettime(CLOCK_MONOTONIC, &start_dewarp);

    if (m_ldc_configs.dis_config.angular_dis_config.enabled)
    {
        media_library_return ret = perform_angular_dis_dewarp(input_buffer, dewarp_output_buffer, image, mesh);
        if (ret != MEDIA_LIBRARY_SUCCESS)
            return ret;
    }
    else
    {
        dsp_status ret = dsp_utils::perform_dsp_dewarp(
            input_buffer.hailo_pix_buffer.get(),
            image, mesh,
            m_ldc_configs.dewarp_config.interpolation_type);

        if (ret != DSP_SUCCESS)
            return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_dewarp);
    [[maybe_unused]] long ms = (long)media_library_difftimespec_ms(end_dewarp, start_dewarp);
    LOGGER__TRACE("perform_dsp_dewarp took {} milliseconds ({} fps)", ms, (1000 / ms));

    return MEDIA_LIBRARY_SUCCESS;
}

void MediaLibraryDewarp::Impl::stamp_time_and_log_fps(timespec & start_handle, timespec & end_handle)
{
    clock_gettime(CLOCK_MONOTONIC, &end_handle);
    long ms = (long)media_library_difftimespec_ms(end_handle, start_handle);
    uint framerate = 1000 / ms;
    LOGGER__DEBUG("dewarp handle_frame took {} milliseconds ({} fps)", ms, framerate);
}

void MediaLibraryDewarp::Impl::increase_frame_counter()
{
    // Increase frame counter or reset it to 1
    m_frame_counter = (m_frame_counter == 60) ? 1 : m_frame_counter + 1;
}

media_library_return
MediaLibraryDewarp::Impl::validate_input_frame(
    hailo_media_library_buffer & input_frame)
{
    output_resolution_t &input_res = m_ldc_configs.input_video_config.resolution;
    dsp_image_properties_t *input_image_properties = input_frame.hailo_pix_buffer.get();

    if (input_res != *input_image_properties)
    {
        LOGGER__ERROR("Invalid input frame width {} input frame height {}", input_image_properties->width, input_image_properties->height);
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDewarp::Impl::handle_frame(hailo_media_library_buffer & input_frame, hailo_media_library_buffer & output_frame)
{
    std::shared_lock<std::shared_mutex> lock(rw_lock);

    // Stamp start time
    struct timespec start_handle, end_handle;
    clock_gettime(CLOCK_MONOTONIC, &start_handle);

    if (validate_input_frame(input_frame) != MEDIA_LIBRARY_SUCCESS)
    {
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    m_video_fd = input_frame.video_fd;

    // Dewarp
    media_library_return media_lib_ret = MEDIA_LIBRARY_SUCCESS;
    // If the frame is not converged, or the fps is lower than the minimum fps for DIS, we need to reset the VSM
    if ((!input_frame.isp_ae_converged) ||
        (!(input_frame.isp_ae_fps > MIN_ISP_AE_FPS_FOR_DIS || input_frame.isp_ae_fps == HAILO_ISP_AE_FPS_DEFAULT_VALUE)) ||
        (input_frame.isp_ae_average_luma < m_ldc_configs.dis_config.average_luminance_threshold))
    {
        LOGGER__INFO("Resetting VSM  - reason could be ae converged {} ae fps {} or ae luminance {}",
                     input_frame.isp_ae_converged,
                     input_frame.isp_ae_fps,
                     input_frame.isp_ae_average_luma);
        input_frame.vsm.dx = HAILO_VSM_DEFAULT_VALUE;
        input_frame.vsm.dy = HAILO_VSM_DEFAULT_VALUE;
    }

    // Update mesh context if dis is enabled and vsm has changed
    else if (m_ldc_configs.dis_config.enabled)
    {
        LOGGER__DEBUG("Updating vsm to dx {} dy {}", input_frame.vsm.dx, input_frame.vsm.dy);
        m_dewarp_mesh_ctx->on_frame_vsm_update(input_frame.vsm);
    }

    // Update last vsm
    m_last_vsm.dx = input_frame.vsm.dx;
    m_last_vsm.dy = input_frame.vsm.dy;

    media_lib_ret = perform_dewarp(input_frame, output_frame);
    output_frame.isp_ae_fps = input_frame.isp_ae_fps;
    output_frame.isp_ae_converged = input_frame.isp_ae_converged;

    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
        return media_lib_ret;

    increase_frame_counter();

    stamp_time_and_log_fps(start_handle, end_handle);

    return MEDIA_LIBRARY_SUCCESS;
}

ldc_config_t &MediaLibraryDewarp::Impl::get_ldc_configs()
{
    return m_ldc_configs;
}

input_video_config_t &MediaLibraryDewarp::Impl::get_input_video_config()
{
    return m_ldc_configs.input_video_config;
}

output_resolution_t &MediaLibraryDewarp::Impl::get_output_video_config()
{
    return m_ldc_configs.output_video_config;
}

media_library_return MediaLibraryDewarp::Impl::set_optical_zoom(float magnification)
{
    struct v4l2_control ctrl;

    if (!m_ldc_configs.optical_zoom_config.enabled)
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

media_library_return MediaLibraryDewarp::Impl::set_input_video_config(uint32_t width, uint32_t height, uint32_t framerate, dsp_image_format_t format)
{
    m_ldc_configs.input_video_config.resolution.dimensions.destination_width = width;
    m_ldc_configs.input_video_config.resolution.dimensions.destination_height = height;
    m_ldc_configs.input_video_config.resolution.framerate = framerate;
    m_ldc_configs.input_video_config.format = format;

    m_ldc_configs.output_video_config.dimensions.destination_width = width;
    m_ldc_configs.output_video_config.dimensions.destination_height = height;
    m_ldc_configs.output_video_config.framerate = framerate;

    if(m_ldc_configs.rotation_config.enabled||m_ldc_configs.flip_config.enabled||m_ldc_configs.dis_config.enabled||m_ldc_configs.dewarp_config.enabled||m_ldc_configs.optical_zoom_config.enabled)
    {
        // after setting output video config, we need to make sure rotation will take place, because we set the dimensions as if we are with rotation 0
        // so set the current rotation to 0, and then run configure() with the actual rotation value, this will force the configuration to have correct rotation
        ldc_config_t new_conf = m_ldc_configs;
        new_conf.rotation_config.angle = m_ldc_configs.rotation_config.angle;
        m_ldc_configs.rotation_config.angle = rotation_angle_t::ROTATION_ANGLE_0;

        // reconfigure ldc_mesh_context and buffer pool since we updated the config
        return configure(new_conf);
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDewarp::Impl::observe(const MediaLibraryDewarp::callbacks_t &callbacks)
{
    m_callbacks.push_back(callbacks);
    return MEDIA_LIBRARY_SUCCESS;
}