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

#include <fstream>
#include <linux/v4l2-controls.h>
#include <linux/v4l2-subdev.h>
#include <stdint.h>
#include <string>
#include <sys/ioctl.h>
#include <time.h>
#include <tl/expected.hpp>
#include <vector>

#include "buffer_pool.hpp"
#include "common.hpp"
#include "dewarp.hpp"
#include "dsp_utils.hpp"
#include "env_vars.hpp"
#include "hailo_media_library_perfetto.hpp"
#include "perfetto_fps_tracer.hpp"
#include "ldc_mesh_context.hpp"
#include "logger_macros.hpp"
#include "media_library_buffer.hpp"
#include "media_library_logger.hpp"
#include "media_library_utils.hpp"
#include "snapshot.hpp"

#define HAILO15_ISP_CID_LSC_BASE (V4L2_CID_USER_BASE + 0x3200)
#define HAILO15_ISP_CID_LSC_OPTICAL_ZOOM (HAILO15_ISP_CID_LSC_BASE + 0x0009)

#define EIS_NUM_FRAMES_PULL_INTEGRATION_TIME (120)
#define VSM_PRINTS_FILE_PATH ("/tmp/dis_vsm_output.txt")

struct VSMPrintFile
{
    std::ofstream outfile;
    bool env_print_vsm_to_file;

    VSMPrintFile(const std::string &fname = VSM_PRINTS_FILE_PATH)
    {
        auto var_result = get_env_variable<bool>(MEDIALIB_VSM_PRINT_ENV_VAR);
        if (var_result.has_value())
        {
            env_print_vsm_to_file = var_result.value();
        }
        else
        {
            env_print_vsm_to_file = false;
        }

        if (env_print_vsm_to_file)
        {
            outfile.open(fname);
        }
    }

    void writeToFile(struct hailo15_vsm::hailo15_vsm &vsm)
    {
        if (outfile.is_open() && env_print_vsm_to_file)
        {
            outfile << "dx = " << vsm.dx << "; dy = " << vsm.dy << "\n";
        }
    }

    ~VSMPrintFile()
    {
        if (outfile.is_open())
        {
            outfile.close();
        }
    }
};
VSMPrintFile vsm_fh;

#define MODULE_NAME LoggerType::Dewarp

class MediaLibraryDewarp::Impl final
{
  public:
    static tl::expected<std::shared_ptr<MediaLibraryDewarp::Impl>, media_library_return> create();
    // Constructor
    Impl(media_library_return &status);
    // Destructor
    ~Impl();
    // Move constructor
    Impl(Impl &&) = delete;
    // Move assignment
    Impl &operator=(Impl &&) = delete;

    // Perform pre-processing on the input frame and return the output frames
    media_library_return handle_frame(HailoMediaLibraryBufferPtr input_frame, HailoMediaLibraryBufferPtr output_frame);

    // set the callbacks object
    media_library_return observe(const MediaLibraryDewarp::callbacks_t &callbacks);

  private:
    std::unique_ptr<LdcMeshContext> m_dewarp_mesh_ctx;
    // frame counter - used internally for matching requested framerate
    uint m_frame_counter;
    // last vsm
    struct hailo15_vsm m_last_vsm;
    // output buffer pool
    MediaLibraryBufferPoolPtr m_output_buffer_pool;
    // video fd
    int m_video_fd;

    uint64_t m_curr_ae_integration_time;
    uint64_t m_curr_ae_integration_time_counter;

    std::vector<MediaLibraryDewarp::callbacks_t> m_callbacks;

    // FPS tracer for Perfetto
    PerfettoFpsTracer m_fps_tracer;

    // Sets the optical zoom value to adjust the lens shading correction.
    media_library_return set_optical_zoom(float magnification);
    media_library_return adjust_buffer_pools(HailoMediaLibraryBufferPtr input_frame);
    bool should_adjust_buffer_pools(HailoMediaLibraryBufferPtr input_frame);
    media_library_return perform_dewarp(HailoMediaLibraryBufferPtr input_buffer,
                                        HailoMediaLibraryBufferPtr dewarp_output_buffer);
    media_library_return perform_angular_dis_dewarp(HailoMediaLibraryBufferPtr input_buffer,
                                                    HailoMediaLibraryBufferPtr dewarp_output_buffer,
                                                    dsp_dewarp_mesh_t *mesh);
    void stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle);
    void increase_frame_counter();
};

//------------------------ MediaLibraryDewarp ------------------------
tl::expected<std::shared_ptr<MediaLibraryDewarp>, media_library_return> MediaLibraryDewarp::create()
{
    auto impl_expected = Impl::create();
    if (impl_expected.has_value())
        return std::make_shared<MediaLibraryDewarp>(impl_expected.value());
    else
        return tl::make_unexpected(impl_expected.error());
}

MediaLibraryDewarp::MediaLibraryDewarp(std::shared_ptr<MediaLibraryDewarp::Impl> impl) : m_impl(impl)
{
}

MediaLibraryDewarp::~MediaLibraryDewarp() = default;

media_library_return MediaLibraryDewarp::handle_frame(HailoMediaLibraryBufferPtr input_frame,
                                                      HailoMediaLibraryBufferPtr output_frame)
{
    media_library_return status;
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN("MediaLibraryDewarp::handle_frame", DSP_THREADED_TRACK,
                                          MEDIA_LIBRARY_DETAILED_CATEGORY, "isp_timestamp_ms",
                                          input_frame->isp_timestamp_ns / 1000000);
    status = m_impl->handle_frame(input_frame, output_frame);
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(DSP_THREADED_TRACK, MEDIA_LIBRARY_DETAILED_CATEGORY);
    return status;
}

media_library_return MediaLibraryDewarp::observe(const MediaLibraryDewarp::callbacks_t &callbacks)
{
    return m_impl->observe(callbacks);
}

//------------------------ MediaLibraryDewarp::Impl ------------------------

tl::expected<std::shared_ptr<MediaLibraryDewarp::Impl>, media_library_return> MediaLibraryDewarp::Impl::create()
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    std::shared_ptr<MediaLibraryDewarp::Impl> dewarp = std::make_shared<MediaLibraryDewarp::Impl>(status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return dewarp;
}

MediaLibraryDewarp::Impl::Impl(media_library_return &status) : m_fps_tracer("Dewarp FPS")
{
    m_video_fd = -1;
    m_last_vsm.dx = 0;
    m_last_vsm.dy = 0;

    // Start frame count from 0 - to make sure we always handle the first frame even if framerate is set to 0
    m_frame_counter = 0;

    dsp_status dsp_ret = dsp_utils::acquire_device();
    if (dsp_ret != DSP_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to acquire DSP device, status: {}", dsp_ret);
        status = MEDIA_LIBRARY_OUT_OF_RESOURCES;
        return;
    }

    m_dewarp_mesh_ctx = std::make_unique<LdcMeshContext>();

    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryDewarp::Impl::~Impl()
{
    m_dewarp_mesh_ctx = nullptr;
    dsp_status status = dsp_utils::release_device();
    if (status != DSP_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to release DSP device, status: {}", status);
    }
}

bool MediaLibraryDewarp::Impl::should_adjust_buffer_pools(HailoMediaLibraryBufferPtr input_frame)
{
    auto input_frame_dimensions = input_frame->get_attached_profile()->sensor_config.input_video.resolution;

    if (m_output_buffer_pool != nullptr && input_frame_dimensions.width == m_output_buffer_pool->get_width() &&
        input_frame_dimensions.height == m_output_buffer_pool->get_height())
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Buffer pool already exists, skipping creation");
        return false;
    }

    return true;
}

media_library_return MediaLibraryDewarp::Impl::adjust_buffer_pools(HailoMediaLibraryBufferPtr input_frame)
{
    auto input_frame_dimensions = input_frame->get_attached_profile()->sensor_config.input_video.resolution;
    auto bytes_per_line = dsp_utils::get_dsp_desired_stride_from_width(input_frame_dimensions.width);
    std::string name = "dewarp_output";
    // Forcing output video buffer pool to be max 5 buffers.
    static constexpr size_t POOL_MAX_BUFFERS = 5;
    LOGGER__MODULE__INFO(
        MODULE_NAME,
        "Creating buffer pool named {} for output resolution: width {} height {} in buffers size of {} and "
        "bytes per line {}",
        name, input_frame_dimensions.width, input_frame_dimensions.height, POOL_MAX_BUFFERS, bytes_per_line);
    m_output_buffer_pool = std::make_shared<MediaLibraryBufferPool>(
        input_frame_dimensions.width, input_frame_dimensions.height, HAILO_FORMAT_NV12, POOL_MAX_BUFFERS,
        HAILO_MEMORY_TYPE_DMABUF, bytes_per_line, name);
    if (m_output_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init buffer pool");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDewarp::Impl::perform_angular_dis_dewarp(
    HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr dewarp_output_buffer, dsp_dewarp_mesh_t *mesh)
{
    std::shared_ptr<angular_dis_params_t> angular_dis_params = m_dewarp_mesh_ctx->get_angular_dis_params();

    float cur_angles_sum = *(angular_dis_params->dsp_filter_angle->cur_angles_sum);
    float cur_traj = *(angular_dis_params->dsp_filter_angle->cur_traj);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Perform Angular dewarp previous alpha = {} cur angles sum = {} cur traj = {}",
                          angular_dis_params->dsp_filter_angle->alpha, cur_angles_sum, cur_traj);

    dsp_filter_angle_t filter_angle_ptr = {.maximum_theta = angular_dis_params->dsp_filter_angle->maximum_theta,
                                           .alpha = angular_dis_params->dsp_filter_angle->alpha,
                                           .prev_angles_sum = cur_angles_sum,
                                           .prev_traj = cur_traj,
                                           .cur_angles_sum = angular_dis_params->dsp_filter_angle->cur_angles_sum.get(),
                                           .cur_traj = angular_dis_params->dsp_filter_angle->cur_traj.get(),
                                           .stabilized_theta =
                                               angular_dis_params->dsp_filter_angle->stabilized_theta.get()};

    dsp_vsm_config_t vsm_config = {.hoffset = angular_dis_params->dsp_vsm_config.hoffset,
                                   .voffset = angular_dis_params->dsp_vsm_config.voffset,
                                   .width = angular_dis_params->dsp_vsm_config.width,
                                   .height = angular_dis_params->dsp_vsm_config.height,
                                   .max_displacement = angular_dis_params->dsp_vsm_config.max_displacement};

    dsp_isp_vsm_t isp_vsm = {.center_x = angular_dis_params->isp_vsm.center_x,
                             .center_y = angular_dis_params->isp_vsm.center_y,
                             .dx = angular_dis_params->isp_vsm.dx,
                             .dy = angular_dis_params->isp_vsm.dy};

    dsp_status ret =
        dsp_utils::perform_dsp_dewarp(input_buffer->buffer_data.get(), dewarp_output_buffer->buffer_data.get(), mesh,
                                      input_buffer->get_attached_profile()->iq_settings.dewarp.interpolation_type,
                                      isp_vsm, vsm_config, filter_angle_ptr, angular_dis_params->cur_columns_sum,
                                      angular_dis_params->cur_rows_sum, angular_dis_params->stabilize_rotation);

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
media_library_return MediaLibraryDewarp::Impl::perform_dewarp(HailoMediaLibraryBufferPtr input_buffer,
                                                              HailoMediaLibraryBufferPtr dewarp_output_buffer)
{
    struct timespec start_dewarp, end_dewarp;

    if (should_adjust_buffer_pools(input_buffer))
    {
        media_library_return ret = adjust_buffer_pools(input_buffer);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to adjust buffer pools");
            return ret;
        }
    }

    // Acquire buffer for dewarp output
    if (m_output_buffer_pool->acquire_buffer(dewarp_output_buffer) != MEDIA_LIBRARY_SUCCESS)
    {
        // log: failed to acquire buffer for dewarp output
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    // Perform dewarp
    dsp_dewarp_mesh_t *mesh = m_dewarp_mesh_ctx->get();
    auto &dewarp_config = input_buffer->get_attached_profile()->iq_settings.dewarp;
    LOGGER__MODULE__TRACE(MODULE_NAME, "Performing dewarp with mesh (w={}, h={}) interpolation type {}",
                          mesh->mesh_width, mesh->mesh_height, dewarp_config.interpolation_type);
    clock_gettime(CLOCK_MONOTONIC, &start_dewarp);

    if (input_buffer->get_attached_profile()->stabilizer_settings.dis.angular_dis_config.enabled)
    {
        media_library_return ret = perform_angular_dis_dewarp(input_buffer, dewarp_output_buffer, mesh);
        if (ret != MEDIA_LIBRARY_SUCCESS)
            return ret;
    }
    else
    {
        dsp_status ret =
            dsp_utils::perform_dsp_dewarp(input_buffer->buffer_data.get(), dewarp_output_buffer->buffer_data.get(),
                                          mesh, dewarp_config.interpolation_type);

        if (ret != DSP_SUCCESS)
            return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_dewarp);
    [[maybe_unused]] long ms = (long)media_library_difftimespec_ms(end_dewarp, start_dewarp);
    LOGGER__MODULE__TRACE(MODULE_NAME, "perform_dsp_dewarp took {} milliseconds ({} fps)", ms, (1000 / ms));

    return MEDIA_LIBRARY_SUCCESS;
}

void MediaLibraryDewarp::Impl::stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle)
{
    clock_gettime(CLOCK_MONOTONIC, &end_handle);
    long ms = (long)media_library_difftimespec_ms(end_handle, start_handle);
    uint framerate = 1000 / ms;
    LOGGER__MODULE__TRACE(MODULE_NAME, "dewarp handle_frame took {} milliseconds ({} fps)", ms, framerate);

    m_fps_tracer.record_frame();
}

void MediaLibraryDewarp::Impl::increase_frame_counter()
{
    // Increase frame counter or reset it to 1
    m_frame_counter = (m_frame_counter == 60) ? 1 : m_frame_counter + 1;
}

media_library_return MediaLibraryDewarp::Impl::handle_frame(HailoMediaLibraryBufferPtr input_frame,
                                                            HailoMediaLibraryBufferPtr output_frame)
{
    // Stamp start time
    struct timespec start_handle, end_handle;
    clock_gettime(CLOCK_MONOTONIC, &start_handle);

    m_video_fd = input_frame->video_fd;
    auto &input_frame_stabilization_config = input_frame->get_attached_profile()->stabilizer_settings;
    auto &input_frame_optical_zoom_config = input_frame->get_attached_profile()->application_settings.optical_zoom;

    m_dewarp_mesh_ctx->handle_frame(input_frame);

    // Dewarp
    media_library_return media_lib_ret = MEDIA_LIBRARY_SUCCESS;
    // If the frame is not converged, or the fps is lower than the minimum fps for DIS, we need to reset the VSM
    if ((!input_frame->isp_ae_converged) ||
        (!(input_frame->isp_ae_fps > MIN_ISP_AE_FPS_FOR_DIS ||
           input_frame->isp_ae_fps == HAILO_ISP_AE_FPS_DEFAULT_VALUE)) ||
        (input_frame->isp_ae_average_luma < input_frame_stabilization_config.dis.average_luminance_threshold))
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME,
                              "Resetting VSM  - reason could be ae converged {} ae fps {} or ae luminance {}",
                              input_frame->isp_ae_converged, input_frame->isp_ae_fps, input_frame->isp_ae_average_luma);
        input_frame->vsm.dx = HAILO_VSM_DEFAULT_VALUE;
        input_frame->vsm.dy = HAILO_VSM_DEFAULT_VALUE;
    }

    // Update mesh context if dis is enabled and vsm has changed
    else if (input_frame_stabilization_config.dis.enabled)
    {
        vsm_fh.writeToFile(input_frame->vsm);
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Updating vsm to dx {} dy {}", input_frame->vsm.dx, input_frame->vsm.dy);
        m_dewarp_mesh_ctx->on_frame_vsm_update(input_frame->vsm);
    }
    if (input_frame_stabilization_config.gyro.enabled)
    {
        /* Pull the integration time each time we are not converged or every 120 frames */
        if ((!input_frame->isp_ae_converged) ||
            (m_curr_ae_integration_time_counter % EIS_NUM_FRAMES_PULL_INTEGRATION_TIME == 0))
        {
            m_curr_ae_integration_time_counter = 0;
            m_curr_ae_integration_time = input_frame->isp_ae_integration_time;
            if (m_curr_ae_integration_time == 0)
            {
                LOGGER__MODULE__WARNING(MODULE_NAME, "EIS: Integration time 0 received!");
            }
        }
        ++m_curr_ae_integration_time_counter;

        if (input_frame_stabilization_config.eis.enabled)
        {
            media_lib_ret = m_dewarp_mesh_ctx->on_frame_eis_update(
                input_frame->isp_timestamp_ns, m_curr_ae_integration_time * 1000, input_frame->isp_ae_fps / 1000,
                input_frame_stabilization_config.eis.enabled);
            if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
                return media_lib_ret;
        }
    }

    // Update last vsm
    m_last_vsm.dx = input_frame->vsm.dx;
    m_last_vsm.dy = input_frame->vsm.dy;

    HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN("perform_dewarp", DSP_THREADED_TRACK, MEDIA_LIBRARY_DETAILED_CATEGORY,
                                          "isp_timestamp_ms", input_frame->isp_timestamp_ns / 1000000);
    media_lib_ret = perform_dewarp(input_frame, output_frame);
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(DSP_THREADED_TRACK, MEDIA_LIBRARY_DETAILED_CATEGORY);
    output_frame->copy_metadata_from(input_frame);

    output_frame->optical_zoom_magnification =
        input_frame_optical_zoom_config.enabled ? input_frame_optical_zoom_config.magnification : 1.0f;

    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
        return media_lib_ret;

    increase_frame_counter();

    stamp_time_and_log_fps(start_handle, end_handle);

    SnapshotManager::get_instance().take_snapshot("dewarp", output_frame);

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDewarp::Impl::set_optical_zoom(float magnification)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Setting optical zoom to {}", magnification);

    struct v4l2_control ctrl;

    if (m_video_fd != -1)
    {
        memset(&ctrl, 0, sizeof(ctrl));
        ctrl.id = HAILO15_ISP_CID_LSC_OPTICAL_ZOOM;
        ctrl.value = static_cast<int>(magnification * 100);
        if (ioctl(m_video_fd, VIDIOC_S_CTRL, &ctrl))
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Could not update v4l2-ctl about new optical zoom");
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }
    else
    {
        LOGGER__MODULE__WARNING(MODULE_NAME, "video fd is not initialized, skipping v4l2-ctl update");
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDewarp::Impl::observe(const MediaLibraryDewarp::callbacks_t &callbacks)
{
    m_callbacks.push_back(callbacks);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return ldc_config_t::update(ldc_config_t &ldc_configs)
{
    bool disable_dewarp =
        ldc_configs.optical_zoom_config.enabled &&
        ldc_configs.optical_zoom_config.magnification >= ldc_configs.optical_zoom_config.max_dewarping_magnification;

    dewarp_config.enabled = disable_dewarp ? false : ldc_configs.dewarp_config.enabled;
    dewarp_config.camera_type = dewarp_config.enabled ? CAMERA_TYPE_PINHOLE : CAMERA_TYPE_INPUT_DISTORTIONS;

    dis_config = ldc_configs.dis_config;
    eis_config = ldc_configs.eis_config;
    gyro_config = ldc_configs.gyro_config;
    optical_zoom_config = ldc_configs.optical_zoom_config;
    application_input_streams_config = ldc_configs.application_input_streams_config;
    input_video_config = ldc_configs.input_video_config;
    flip_config = ldc_configs.flip_config;
    rotation_config = ldc_configs.rotation_config;

    // TODO: can we change interpolation type?
    if (dewarp_config != ldc_configs.dewarp_config)
    {
        // Update dewarp configuration is restricted
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    update_flip_rotate(ldc_configs);
    return MEDIA_LIBRARY_SUCCESS;
}

void ldc_config_t::update_flip_rotate(ldc_config_t &ldc_configs)
{
    flip_config = ldc_configs.flip_config;
    if (!is_env_variable_on(MEDIALIB_DEWARP_DSP_OPTIMIZATION_ENV_VAR))
    {
        return;
    }

    rotation_angle_t current_rotation_angle = rotation_config.effective_value();
    rotation_angle_t new_rotation_angle = ldc_configs.rotation_config.effective_value();
    if (current_rotation_angle != new_rotation_angle)
    {
        if (current_rotation_angle % 2 != new_rotation_angle % 2 &&
            dewarp_config.enabled) // if the rotation angle is not aligned, rotate the output resolutions
        {
            rotate_output_dimensions();
        }
    }

    rotation_config = ldc_configs.rotation_config;
}
