#include "ldc_mesh_context.hpp"
#include "dis_interface.h"
#include "media_library_logger.hpp"
#include "media_library_utils.hpp"
#include "dma_memory_allocator.hpp"
#include "gyro_device.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <stdio.h>
#include <algorithm>
#include <vector>
#include <opencv2/opencv.hpp>

#define CALIBRATION_VECOTR_SIZE 1024
#define DEFAULT_ALPHA 0.1f

extern std::unique_ptr<GyroDevice> gyroApi;
std::thread gyroThread;
std::mutex global_mtx;

LdcMeshContext::LdcMeshContext(ldc_config_t &config)
{
    if (!config.check_ops_enabled(true) || config.output_video_config.dimensions.destination_width == 0 ||
        config.output_video_config.dimensions.destination_height == 0)
        return;

    configure(config);
}

static void kill_gyro_thread()
{
    std::unique_lock<std::mutex> lock(global_mtx);
    if (!gyroApi)
    {
        return;
    }

    if (gyroApi->stopRunning())
    {
        if (gyroApi->cv.wait_for(lock, std::chrono::milliseconds(5000), [] { return gyroApi->get_stopRunningAck(); }))
        {
            if (gyroThread.joinable())
                gyroThread.join();
        }
        else
        {
            printf("Timeout occurred while waiting for process to finish.\n");
        }
    }

    /* Change the signal handlers to their default values */
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    gyroApi = nullptr;
}

LdcMeshContext::~LdcMeshContext()
{
    media_library_return result = MEDIA_LIBRARY_SUCCESS;

    // Free ldc mesh context
    if (m_dis_ctx != nullptr)
    {
        result = free_dis_context();
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("failed releasing ldc mesh context on error {}", result);
        }
    }

    // Free dewarp mesh buffer
    if (m_is_initialized)
    {
        if (m_dewarp_mesh.mesh_table != nullptr)
        {
            result = DmaMemoryAllocator::get_instance().free_dma_buffer(m_dewarp_mesh.mesh_table);
            if (result != MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__ERROR("failed releasing mesh dsp buffer on error {}", result);
            }
        }

        // Free angular dis columns buffer
        if (m_angular_dis_params != nullptr)
        {
            if (m_angular_dis_params->cur_columns_sum != nullptr)
            {
                result =
                    DmaMemoryAllocator::get_instance().free_dma_buffer((void *)m_angular_dis_params->cur_columns_sum);
                if (result != MEDIA_LIBRARY_SUCCESS)
                {
                    LOGGER__ERROR("failed releasing angular dis columns buffer on error {}", result);
                }
            }

            // Free angular dis rows buffer
            if (m_angular_dis_params->cur_rows_sum != nullptr)
            {
                result = DmaMemoryAllocator::get_instance().free_dma_buffer((void *)m_angular_dis_params->cur_rows_sum);
                if (result != MEDIA_LIBRARY_SUCCESS)
                {
                    LOGGER__ERROR("failed releasing angular dis rows buffer on error {}", result);
                }
            }
        }
    }

    // Free Gyro
    if (m_gyro_initialized)
    {
        if (!m_ldc_configs.gyro_config.enabled)
        {
            LOGGER__WARNING("Gyro was not enabled, but it was initialized");
        }
        kill_gyro_thread();
        m_gyro_initialized = false;
    }
}

media_library_return LdcMeshContext::read_vsm_config()
{
    std::ifstream file(LDC_VSM_CONFIG);
    if (!file.is_open())
    {
        LOGGER__ERROR("read_vsm_config failed, could not open file {}", LDC_VSM_CONFIG);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // read ifstream to std::string
    std::string vsm_string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // convert string to struct
    return m_config_manager->config_string_to_struct<vsm_config_t>(vsm_string, m_vsm_config);
}

tl::expected<dis_calibration_t, media_library_return> LdcMeshContext::read_calibration_file(const char *name)
{
    dis_calibration_t calib{{}, {1, 1}, {}};
    std::ifstream file(name);
    if (!file.is_open())
    {
        LOGGER__ERROR("read_calibration_file failed, could not open file {}", name);
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }

    file.seekg(0);

    // Ignore first line - it is a comment
    file.ignore(1024, '\n');

    std::string row;
    std::getline(file, row);
    calib.res.x = atoi(row.c_str());
    if (file.eof())
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    std::getline(file, row);
    calib.res.y = atoi(row.c_str());
    if (file.eof())
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    std::getline(file, row);
    calib.oc.x = atof(row.c_str());
    if (file.eof())
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    std::getline(file, row);
    calib.oc.y = atof(row.c_str());
    if (file.eof())
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    std::getline(file, row);
    calib.theta2radius.push_back(atof(row.c_str()));
    if (file.eof())
    {
        LOGGER__ERROR("read_calibration_file failed, invalid data");
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }
    if (calib.theta2radius[0] != 0)
    {
        LOGGER__ERROR("Improper calibration file theta2radius[0] must be 0, but it is {}", calib.theta2radius[0]);
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }

    for (uint32_t i = 1; !file.eof() && i < CALIBRATION_VECOTR_SIZE; ++i)
    {
        std::getline(file, row);
        calib.theta2radius.push_back(atof(row.c_str()));

        if (calib.theta2radius[i] <= 0)
        {
            LOGGER__ERROR("theta2radius[{}] contain positive radii. is {}", i, calib.theta2radius[i]);
            return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
        }
        if (calib.theta2radius[i] < calib.theta2radius[i - 1])
        {
            LOGGER__ERROR(
                "Improper calibration file theta2radius[{}] must be monotonically increasing, but it is not ({})", i,
                calib.theta2radius[i]);
            return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
        }
    }
    return calib;
};

FlipMirrorRot LdcMeshContext::get_flip_value(flip_direction_t flip_dir, rotation_angle_t rotation_angle)
{
    FlipMirrorRot flip_mirror_rot;

    switch (rotation_angle)
    {
    case ROTATION_ANGLE_90: {
        switch (flip_dir)
        {
        case FLIP_DIRECTION_HORIZONTAL: {
            flip_mirror_rot = ROT90_MIRROR;
            break;
        }
        case FLIP_DIRECTION_VERTICAL: {
            flip_mirror_rot = ROT90_FLIPV;
            break;
        }
        case FLIP_DIRECTION_BOTH: {
            flip_mirror_rot = ROT90;
            break;
        }
        default: {
            flip_mirror_rot = ROT90_FLIPV_MIRROR;
            break;
        }
        }
        break;
    }
    case ROTATION_ANGLE_180: {
        switch (flip_dir)
        {
        case FLIP_DIRECTION_HORIZONTAL: {
            flip_mirror_rot = ROT180_MIRROR;
            break;
        }
        case FLIP_DIRECTION_VERTICAL: {
            flip_mirror_rot = ROT180_FLIPV;
            break;
        }
        case FLIP_DIRECTION_BOTH: {
            flip_mirror_rot = ROT180_FLIPV_MIRROR;
            break;
        }
        default: {
            flip_mirror_rot = ROT180;
            break;
        }
        }
        break;
    }
    case ROTATION_ANGLE_270: {
        switch (flip_dir)
        {
        case FLIP_DIRECTION_HORIZONTAL: {
            flip_mirror_rot = ROT270_MIRROR;
            break;
        }
        case FLIP_DIRECTION_VERTICAL: {
            flip_mirror_rot = ROT270_FLIPV;
            break;
        }
        case FLIP_DIRECTION_BOTH: {
            flip_mirror_rot = ROT270;
            break;
        }
        default: {
            flip_mirror_rot = ROT270_FLIPV_MIRROR;
            break;
        }
        }
        break;
    }
    default: {
        switch (flip_dir)
        {
        case FLIP_DIRECTION_HORIZONTAL: {
            flip_mirror_rot = MIRROR;
            break;
        }
        case FLIP_DIRECTION_VERTICAL: {
            flip_mirror_rot = FLIPV;
            break;
        }
        case FLIP_DIRECTION_BOTH: {
            flip_mirror_rot = FLIPV_MIRROR;
            break;
        }
        default: {
            flip_mirror_rot = NATURAL;
            break;
        }
        }
        break;
    }
    }

    return flip_mirror_rot;
}

static void set_handler(int signal_nb, void (*handler)(int))
{
    struct sigaction sig;
    sigaction(signal_nb, NULL, &sig);
    sig.sa_handler = handler;
    sigaction(signal_nb, &sig, NULL);
}

static void handle_sig(int sig)
{
    kill_gyro_thread();
    raise(sig);
}

media_library_return LdcMeshContext::initialize_dis_context()
{
    DewarpT dewarp_mesh;
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    float camera_fov_factor = m_ldc_configs.dis_config.camera_fov_factor;

    // Read the sensor calibration and dewarp configuration files
    m_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_VSM);
    std::vector<char> calib_file;
    status = read_vsm_config();
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("dewarp mesh initialization failed when reading vsm_config");
        return status;
    }
    auto expected_calib = read_calibration_file(m_ldc_configs.dewarp_config.sensor_calib_path.c_str());
    if (!expected_calib.has_value())
    {
        LOGGER__ERROR("dewarp mesh initialization failed when reading calib_file");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    auto calib = expected_calib.value();

    if (m_ldc_configs.optical_zoom_config.enabled && m_magnification != 1.0) // set calibration according to zoom
    {
        for (size_t i = 0; i < calib.theta2radius.size(); i++)
        {
            calib.theta2radius[i] *= m_magnification;
        }
    }

    if (m_ldc_configs.eis_config.enabled && m_ldc_configs.gyro_config.enabled)
    {
        camera_fov_factor = m_ldc_configs.eis_config.camera_fov_factor;
        if (eis_prev_enabled == false)
        {
            if (m_eis_ptr != nullptr)
            {
                /* We dynamically switched from EIS disabled to enabled, reset EIS data */
                m_eis_ptr->reset_history();
            }
            else
            {
                /* This is the first time EIS is enabled, initialize it */
                m_eis_ptr = std::make_unique<EIS>(m_ldc_configs.eis_config.eis_config_path.c_str(),
                                                  m_ldc_configs.eis_config.window_size);
            }
        }
    }

    /* Initiliase EIS only once and in the case it is enabled */
    if (m_ldc_configs.gyro_config.enabled && gyroApi == nullptr)
    {
        sigset_t set, oldset;
        gyroApi = std::make_unique<GyroDevice>(m_ldc_configs.gyro_config.sensor_name,
                                               m_ldc_configs.gyro_config.sensor_frequency,
                                               m_ldc_configs.gyro_config.gyro_scale);

        if (gyroApi->configure() != GYRO_STATUS_SUCCESS)
        {
            LOGGER__ERROR("Failed to configure GyroDevice.");
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }

        set_handler(SIGINT, &handle_sig);
        set_handler(SIGTERM, &handle_sig);
        sigfillset(&set);
        pthread_sigmask(SIG_BLOCK, &set, &oldset);
        gyroThread = std::thread(&GyroDevice::run, gyroApi.get());
        m_gyro_initialized = true;
        pthread_sigmask(SIG_SETMASK, &oldset, NULL);
    }
    else if (!m_ldc_configs.gyro_config.enabled && gyroApi != nullptr)
    {
        kill_gyro_thread();
    }

    eis_prev_enabled = m_ldc_configs.eis_config.enabled;

    // Initialize dis dewarp mesh object using DIS library
    RetCodes ret = dis_init(&m_dis_ctx, m_ldc_configs.dis_config, calib, m_input_width, m_input_height,
                            m_ldc_configs.dewarp_config.camera_type, camera_fov_factor,
                            m_ldc_configs.eis_config.enabled, &dewarp_mesh);
    if (ret != DIS_OK)
    {
        LOGGER__ERROR("dewarp mesh initialization failed on error {}", ret);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Convert stuct to dsp_dewarp_mesh_t
    m_dewarp_mesh.mesh_width = dewarp_mesh.mesh_width;
    m_dewarp_mesh.mesh_height = dewarp_mesh.mesh_height;
    return status;
}

media_library_return LdcMeshContext::free_dis_context()
{
    RetCodes ret = dis_deinit(&m_dis_ctx);
    if (ret != DIS_OK)
    {
        LOGGER__ERROR("dewarp mesh free failed on error {}", ret);
        return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

std::shared_ptr<angular_dis_params_t> LdcMeshContext::get_angular_dis_params()
{
    return m_angular_dis_params;
}

media_library_return LdcMeshContext::initialize_angular_dis()
{
    m_angular_dis_params->stabilize_rotation = false;

    angular_dis_config_t angular_dis_config = m_ldc_configs.dis_config.angular_dis_config;
    int window_width = angular_dis_config.vsm_config.width;
    int window_height = angular_dis_config.vsm_config.height;

    angular_dis_vsm_config_t dsp_vsm_config = {.hoffset = angular_dis_config.vsm_config.hoffset,
                                               .voffset = angular_dis_config.vsm_config.voffset,
                                               .width = (size_t)window_width,
                                               .height = (size_t)window_height,
                                               .max_displacement = angular_dis_config.vsm_config.max_displacement};

    m_angular_dis_params->dsp_filter_angle = std::make_shared<angular_dis_filter_angle_t>();
    m_angular_dis_params->dsp_filter_angle->cur_angles_sum = std::make_shared<float>();
    m_angular_dis_params->dsp_filter_angle->cur_traj = std::make_shared<float>();
    m_angular_dis_params->dsp_filter_angle->stabilized_theta = std::make_shared<float>();
    *(m_angular_dis_params->dsp_filter_angle->stabilized_theta) = 0.0;
    *m_angular_dis_params->dsp_filter_angle->cur_traj = 0.0f;
    *m_angular_dis_params->dsp_filter_angle->cur_angles_sum = 0.0f;

    m_angular_dis_params->dsp_vsm_config = dsp_vsm_config;

    m_angular_dis_params->dsp_filter_angle->alpha = DEFAULT_ALPHA;

    m_angular_dis_params->isp_vsm.dx = 0.0;
    m_angular_dis_params->isp_vsm.dy = 0.0;

    // TODO: read center configurations
    m_angular_dis_params->isp_vsm.center_x = m_vsm_config.vsm_h_size;
    m_angular_dis_params->isp_vsm.center_y = m_vsm_config.vsm_v_size;

    if (angular_dis_config.enabled && m_angular_dis_params->cur_columns_sum == nullptr &&
        m_angular_dis_params->cur_rows_sum == nullptr)
    {
        // Allocate memory for angular dis buffers
        media_library_return result = DmaMemoryAllocator::get_instance().allocate_dma_buffer(
            (window_width * sizeof(uint16_t)), (void **)&m_angular_dis_params->cur_columns_sum);
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("angular dis buffer initialization failed in the buffer allocation process (tried to "
                          "allocate buffer in size of {})",
                          window_width * sizeof(uint16_t));
            return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
        }

        result = DmaMemoryAllocator::get_instance().allocate_dma_buffer((window_height * sizeof(uint16_t)),
                                                                        (void **)&m_angular_dis_params->cur_rows_sum);
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("angular dis buffer initialization failed in the buffer allocation process (tried to "
                          "allocate buffer in size of {})",
                          window_height * sizeof(uint16_t));
            return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return LdcMeshContext::initialize_dewarp_mesh()
{
    DewarpT mesh = {(int)m_dewarp_mesh.mesh_width, (int)m_dewarp_mesh.mesh_height, (int *)m_dewarp_mesh.mesh_table};

    flip_direction_t flip_dir = FLIP_DIRECTION_NONE;
    rotation_angle_t rotation_angle = ROTATION_ANGLE_0;
    if (m_ldc_configs.flip_config.enabled)
        flip_dir = m_ldc_configs.flip_config.direction;
    if (m_ldc_configs.rotation_config.enabled)
        rotation_angle = m_ldc_configs.rotation_config.angle;
    FlipMirrorRot flip_mirror_rot = get_flip_value(flip_dir, rotation_angle);
    DmaMemoryAllocator::get_instance().dmabuf_sync_start((void *)m_dewarp_mesh.mesh_table);
    RetCodes ret = dis_dewarp_only_grid(m_dis_ctx, m_input_width, m_input_height, flip_mirror_rot, &mesh);
    DmaMemoryAllocator::get_instance().dmabuf_sync_end((void *)m_dewarp_mesh.mesh_table);
    if (ret != DIS_OK)
    {
        LOGGER__ERROR("Failed to generate mesh, status: {}", ret);
        return MEDIA_LIBRARY_ERROR;
    }

    m_dewarp_mesh.mesh_table = mesh.mesh_table;
    m_dewarp_mesh.mesh_width = mesh.mesh_width;
    m_dewarp_mesh.mesh_height = mesh.mesh_height;

    LOGGER__INFO("generated base dewarp mesh grid {}x{}", mesh.mesh_width, mesh.mesh_height);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return LdcMeshContext::configure(ldc_config_t &ldc_configs)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    bool prev_eis_stabilize = m_ldc_configs.eis_config.stabilize;
    m_ldc_configs = ldc_configs;
    m_input_width = m_ldc_configs.input_video_config.resolution.dimensions.destination_width;
    m_input_height = m_ldc_configs.input_video_config.resolution.dimensions.destination_height;
    m_magnification = m_ldc_configs.optical_zoom_config.magnification;
    m_last_threshold_timestamp = 0;

    if (!ldc_configs.check_ops_enabled())
        return MEDIA_LIBRARY_SUCCESS;

    if (!m_is_initialized) // initialize mesh for the first time
    {
        m_magnification = m_ldc_configs.optical_zoom_config.magnification;
        m_angular_dis_params = std::make_shared<angular_dis_params_t>();

        LOGGER__INFO("Initiazing dewarp mesh context");
        ret = initialize_dis_context();
        if (ret != MEDIA_LIBRARY_SUCCESS)
            return ret;

        // Allocate memory for mesh table - doing it outside of initialize_dewarp_mesh for reuse of the buffer
        size_t mesh_size = m_dewarp_mesh.mesh_width * m_dewarp_mesh.mesh_height * 2 * 4;

        media_library_return result =
            DmaMemoryAllocator::get_instance().allocate_dma_buffer(mesh_size, (void **)&m_dewarp_mesh.mesh_table);
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("dewarp mesh initialization failed in the buffer allocation process (tried to allocate "
                          "buffer in size of {})",
                          mesh_size);
            return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
        }
    }
    else // free the context and reinitialize, since dis parameters might have changed
    {
        ret = free_dis_context();
        if (ret != MEDIA_LIBRARY_SUCCESS)
            return ret;

        ret = initialize_dis_context();
        if (ret != MEDIA_LIBRARY_SUCCESS)
            return ret;
    }

    ret = initialize_angular_dis();
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    // if magnification level has changed, reinitialize dis context
    if (m_magnification != m_ldc_configs.optical_zoom_config.magnification)
    {
        m_magnification = m_ldc_configs.optical_zoom_config.magnification;
        ret = free_dis_context();
        if (ret != MEDIA_LIBRARY_SUCCESS)
            return ret;

        ret = initialize_dis_context();
        if (ret != MEDIA_LIBRARY_SUCCESS)
            return ret;
    }

    ret = initialize_dewarp_mesh();
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    if (!prev_eis_stabilize && m_ldc_configs.eis_config.stabilize && m_eis_ptr != nullptr)
    {
        /* We dynamically switched from EIS disabled to enabled, reset EIS data */
        LOGGER__INFO("EIS (stabilize) was disabled and now enabled, resetting EIS data");
        m_eis_ptr->reset_history();
    }

    m_is_initialized = true;
    LOGGER__INFO("Dewarp mesh init done.");

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return LdcMeshContext::update_isp_vsm(struct hailo15_vsm &vsm)
{
    m_angular_dis_params->isp_vsm.dx = vsm.dx;
    m_angular_dis_params->isp_vsm.dy = vsm.dy;

    // TODO: Read from cfg and update at start.

    m_angular_dis_params->isp_vsm.center_x = m_vsm_config.vsm_h_size;
    m_angular_dis_params->isp_vsm.center_y = m_vsm_config.vsm_v_size;

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return LdcMeshContext::on_frame_vsm_update(struct hailo15_vsm &vsm)
{
    if (!m_ldc_configs.dis_config.enabled || (vsm.dy == 0 && vsm.dx == 0))
        return MEDIA_LIBRARY_SUCCESS;

    std::unique_lock<std::shared_mutex> lock(m_mutex);

    // Update dewarp mesh with the VSM data to perform DIS
    LOGGER__DEBUG("Updating mesh with VSM");
    DewarpT mesh = {(int)m_dewarp_mesh.mesh_width, (int)m_dewarp_mesh.mesh_height, (int *)m_dewarp_mesh.mesh_table};

    flip_direction_t flip_dir = FLIP_DIRECTION_NONE;
    rotation_angle_t rotation_angle = ROTATION_ANGLE_0;
    if (m_ldc_configs.flip_config.enabled)
        flip_dir = m_ldc_configs.flip_config.direction;
    if (m_ldc_configs.rotation_config.enabled)
        rotation_angle = m_ldc_configs.rotation_config.angle;
    FlipMirrorRot flip_mirror_rot = get_flip_value(flip_dir, rotation_angle);

    DmaMemoryAllocator::get_instance().dmabuf_sync_start((void *)m_dewarp_mesh.mesh_table);
    RetCodes ret = dis_generate_grid(m_dis_ctx, m_input_width, m_input_height, vsm.dx, vsm.dy, 0, flip_mirror_rot,
                                     m_angular_dis_params, &mesh);
    DmaMemoryAllocator::get_instance().dmabuf_sync_end((void *)m_dewarp_mesh.mesh_table);
    if (ret != DIS_OK)
    {
        LOGGER__ERROR("Failed to update mesh with VSM, status: {}", ret);
        return MEDIA_LIBRARY_ERROR;
    }

    m_dewarp_mesh.mesh_table = mesh.mesh_table;
    m_dewarp_mesh.mesh_width = mesh.mesh_width;
    m_dewarp_mesh.mesh_height = mesh.mesh_height;

    if (update_isp_vsm(vsm) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to update mesh with VSM, status: {}", ret);
        return MEDIA_LIBRARY_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return LdcMeshContext::on_frame_eis_update(uint64_t curr_frame_isp_timestamp_ns,
                                                         uint64_t integration_time, bool enabled, uint32_t curr_fps)
{
    if (!m_gyro_initialized)
    {
        LOGGER__ERROR("on_frame_eis_update called with uninitialized gyro!");
        return MEDIA_LIBRARY_ERROR;
    }

    std::vector<unbiased_gyro_sample_t> unbiased_gyro_samples;
    DewarpT grid = {(int)m_dewarp_mesh.mesh_width, (int)m_dewarp_mesh.mesh_height, (int *)m_dewarp_mesh.mesh_table};

    flip_direction_t flip_dir = FLIP_DIRECTION_NONE;
    rotation_angle_t rotation_angle = ROTATION_ANGLE_0;
    if (m_ldc_configs.flip_config.enabled)
        flip_dir = m_ldc_configs.flip_config.direction;
    if (m_ldc_configs.rotation_config.enabled)
        rotation_angle = m_ldc_configs.rotation_config.angle;
    FlipMirrorRot flip_mirror_rot = get_flip_value(flip_dir, rotation_angle);

    /* TODO: Maybe this parameter is to be dynamically changed? */
    uint64_t num_of_readout_lines = 2160;
    uint64_t readout_time = num_of_readout_lines * m_ldc_configs.eis_config.line_readout_time;

    std::vector<std::pair<uint64_t, cv::Mat>> current_orientations = {{0, cv::Mat::eye(3, 3, CV_64F)}};
    std::vector<cv::Mat> rolling_shutter_rotations(m_dewarp_mesh.mesh_height, cv::Mat::eye(3, 3, CV_32F));

    std::vector<gyro_sample_t>::iterator closest_vsync_sample;
    bool found_vsync_sample = gyroApi->get_closest_vsync_sample(curr_frame_isp_timestamp_ns, closest_vsync_sample);

    uint64_t threshold_timestamp, middle_exposure_timestamp;
    std::vector<gyro_sample_t> gyro_samples;

    if (found_vsync_sample)
    {
        /* We found a gyro sample with VSYNC */
        middle_exposure_timestamp = (*closest_vsync_sample).timestamp_ns - (integration_time / 2);
        /* Previous odd VSYNC - (integration_time / 2) + readout_time */
        threshold_timestamp = middle_exposure_timestamp + readout_time;
        gyro_samples = gyroApi->get_gyro_samples_for_frame_vsync(closest_vsync_sample, threshold_timestamp);
    }
    else
    {
        /* No gyro sample with VSYNC found, try finding samples with ISP timestamp */
        LOGGER__WARNING("No gyro samples with VSYNC found for the current frame, trying with ISP timestamp...");
        middle_exposure_timestamp = curr_frame_isp_timestamp_ns - (integration_time / 2) - readout_time;
        threshold_timestamp = middle_exposure_timestamp + readout_time;
        gyro_samples = gyroApi->get_gyro_samples_for_frame_isp_timestamp(threshold_timestamp);
    }

    // if stabilize is false, set middle_exposure_timestamp to 0, this will cause EIS to return the identity matrix
    // instead of an actual rotation matrix this will cause no EIS to be applied
    if (!m_ldc_configs.eis_config.stabilize)
        middle_exposure_timestamp = 0;

    if ((m_last_threshold_timestamp == 0) || (!enabled))
    {
        /* The first frame OR the EIS is currently disabled
        (with a possibility of it being enabled in the future):
        perform Dewarp without EIS fixes */
        m_last_threshold_timestamp = threshold_timestamp;
        goto generate_grid;
    }

    if (gyro_samples.size() <= 1)
    {
        /* If no gyro samples were found (at all) for any reason, perform dewarp with no correction */
        LOGGER__WARNING("No gyro samples found for the current frame (at all)!");
        m_eis_ptr->reset_history();
        m_last_threshold_timestamp = threshold_timestamp;
        goto generate_grid;
    }

    m_eis_ptr->remove_bias(gyro_samples, unbiased_gyro_samples, m_ldc_configs.gyro_config.gyro_scale,
                           m_ldc_configs.eis_config.iir_hpf_coefficient);
    current_orientations = m_eis_ptr->integrate_rotations_rolling_shutter(unbiased_gyro_samples);
    if ((!current_orientations.empty()) && (current_orientations[0].first != 0))
    {
        rolling_shutter_rotations = m_eis_ptr->get_rolling_shutter_rotations(
            current_orientations, m_dewarp_mesh.mesh_height, middle_exposure_timestamp, readout_time);
    }
    m_last_threshold_timestamp = threshold_timestamp;
    // cv::Mat smooth_orientation = m_eis_ptr->smooth(current_orientation,
    // m_ldc_configs.eis_config.rotational_smoothing_coefficient);

generate_grid:
    /* A safety mechanism to remove any unwanted side effects that were gathered
     during the time EIS was on, such as bias */
    if ((enabled) && ((m_eis_ptr->m_frame_count++) >= (curr_fps * EIS_RESET_TIME)))
    {
        bool reset_needed = m_eis_ptr->check_periodic_reset(rolling_shutter_rotations, curr_fps);

        if (reset_needed)
        {
            m_eis_ptr->reset_history();
            m_last_threshold_timestamp = 0;
        }
    }
    dis_generate_eis_grid_rolling_shutter(m_dis_ctx, flip_mirror_rot, rolling_shutter_rotations, &grid);
    m_dewarp_mesh.mesh_table = grid.mesh_table;
    m_dewarp_mesh.mesh_width = grid.mesh_width;
    m_dewarp_mesh.mesh_height = grid.mesh_height;

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return LdcMeshContext::set_optical_zoom(float magnification)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    m_magnification = magnification;

    // upon optical zoom, dis_library should be reinitialized with modified calibration
    ret = free_dis_context();
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    ret = initialize_dis_context();
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    return initialize_dewarp_mesh();
}

dsp_dewarp_mesh_t *LdcMeshContext::get()
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return &m_dewarp_mesh;
}
