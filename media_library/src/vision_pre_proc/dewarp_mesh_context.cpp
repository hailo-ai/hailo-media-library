#include "dewarp_mesh_context.hpp"
#include "dis_interface.h"
#include "media_library_logger.hpp"
#include "media_library_utils.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <stdio.h>

#define CALIBRATION_VECOTR_SIZE 1024

DewarpMeshContext::DewarpMeshContext(pre_proc_op_configurations &config)
{
    if (!config.dewarp_config.enabled)
        return;

    configure(config);
}

DewarpMeshContext::~DewarpMeshContext()
{
    if (!m_pre_proc_configs.dewarp_config.enabled)
        return;

    free_dis_context();

    // Free memory for mesh table
    dsp_status result = dsp_utils::release_hailo_dsp_buffer(m_dewarp_mesh.mesh_table);
    if (result != DSP_SUCCESS)
    {
        LOGGER__ERROR("failed releasing mesh dsp buffer on error {}", result);
    }
}

tl::expected<dis_calibration_t, media_library_return> DewarpMeshContext::read_calibration_file(const char *name)
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
            LOGGER__ERROR("Improper calibration file theta2radius[{}] must be monotonically increasing, but it is not ({})", i, calib.theta2radius[i]);
            return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
        }
    }
    return calib;
};

FlipMirrorRot DewarpMeshContext::get_flip_value(flip_direction_t flip_dir, rotation_angle_t rotation_angle)
{
    FlipMirrorRot flip_mirror_rot;

    switch (rotation_angle)
    {
    case ROTATION_ANGLE_90:
    {
        switch (flip_dir)
        {
        case FLIP_DIRECTION_HORIZONTAL:
        {
            flip_mirror_rot = ROT90_MIRROR;
            break;
        }
        case FLIP_DIRECTION_VERTICAL:
        {
            flip_mirror_rot = ROT90_FLIPV;
            break;
        }
        case FLIP_DIRECTION_BOTH:
        {
            flip_mirror_rot = ROT90_FLIPV_MIRROR;
            break;
        }
        default:
        {
            flip_mirror_rot = ROT90;
            break;
        }
        }
        break;
    }
    case ROTATION_ANGLE_180:
    {
        switch (flip_dir)
        {
        case FLIP_DIRECTION_HORIZONTAL:
        {
            flip_mirror_rot = ROT180_MIRROR;
            break;
        }
        case FLIP_DIRECTION_VERTICAL:
        {
            flip_mirror_rot = ROT180_FLIPV;
            break;
        }
        case FLIP_DIRECTION_BOTH:
        {
            flip_mirror_rot = ROT180_FLIPV_MIRROR;
            break;
        }
        default:
        {
            flip_mirror_rot = ROT180;
            break;
        }
        }
        break;
    }
    case ROTATION_ANGLE_270:
    {
        switch (flip_dir)
        {
        case FLIP_DIRECTION_HORIZONTAL:
        {
            flip_mirror_rot = ROT270_MIRROR;
            break;
        }
        case FLIP_DIRECTION_VERTICAL:
        {
            flip_mirror_rot = ROT270_FLIPV;
            break;
        }
        case FLIP_DIRECTION_BOTH:
        {
            flip_mirror_rot = ROT270_FLIPV_MIRROR;
            break;
        }
        default:
        {
            flip_mirror_rot = ROT270;
            break;
        }
        }
        break;
    }
    default:
    {
        switch (flip_dir)
        {
        case FLIP_DIRECTION_HORIZONTAL:
        {
            flip_mirror_rot = MIRROR;
            break;
        }
        case FLIP_DIRECTION_VERTICAL:
        {
            flip_mirror_rot = FLIPV;
            break;
        }
        case FLIP_DIRECTION_BOTH:
        {
            flip_mirror_rot = FLIPV_MIRROR;
            break;
        }
        default:
        {
            flip_mirror_rot = NATURAL;
            break;
        }
        }
        break;
    }
    }

    return flip_mirror_rot;
}

media_library_return DewarpMeshContext::initialize_dis_context()
{
    DewarpT dewarp_mesh;

    // Read the sensor calibration and dewarp configuration files
    std::vector<char> calib_file;
    auto expected_calib = read_calibration_file(m_pre_proc_configs.dewarp_config.sensor_calib_path.c_str());
    if (!expected_calib.has_value())
    {
        LOGGER__ERROR("dewarp mesh initialization failed when reading calib_file");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    auto calib = expected_calib.value();

    if (m_pre_proc_configs.optical_zoom_config.enabled && m_magnification != 1.0) // set calibration according to zoom
    {
        // crop
        auto cropped = calib.theta2radius;
        size_t crop_size = static_cast<size_t>(CALIBRATION_VECOTR_SIZE / m_magnification);
        cropped.erase(cropped.begin() + crop_size, cropped.end());

        // convert cropped to difference series
        for (size_t i = 0; i < cropped.size() - 1; ++i)
        {
            cropped[i] = cropped[i + 1] - cropped[i];
        }

        // Resize the matrix using cv::resize
        cv::Mat originalMat(1, crop_size, CV_32FC1, cropped.data());
        cv::Mat resizedMat;
        cv::resize(originalMat, resizedMat, cv::Size(CALIBRATION_VECOTR_SIZE, 1));
        std::vector<float> resizedVector(resizedMat.begin<float>(), resizedMat.end<float>());

        // convert resizedVector from difference series to cumulative series
        calib.theta2radius = std::vector<float>{0};
        for (size_t i = 0; i < CALIBRATION_VECOTR_SIZE; ++i)
        {
            calib.theta2radius.push_back(resizedVector[i] + calib.theta2radius[i]);
        }
    }

    // Initialize dis dewarp mesh object using DIS library
    RetCodes ret = dis_init(&m_dis_ctx, m_pre_proc_configs.dis_config, calib,
                            m_input_width, m_input_height,
                            m_pre_proc_configs.dewarp_config.camera_type, m_pre_proc_configs.dewarp_config.camera_fov, &dewarp_mesh);
    if (ret != DIS_OK)
    {
        LOGGER__ERROR("dewarp mesh initialization failed on error {}", ret);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Convert stuct to dsp_dewarp_mesh_t
    m_dewarp_mesh.mesh_width = dewarp_mesh.mesh_width;
    m_dewarp_mesh.mesh_height = dewarp_mesh.mesh_height;
    LOGGER__INFO("dewarp mesh initialization finished {}x{}", dewarp_mesh.mesh_width, dewarp_mesh.mesh_height);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return DewarpMeshContext::free_dis_context()
{
    RetCodes ret = dis_deinit(&m_dis_ctx);
    if (ret != DIS_OK)
    {
        LOGGER__ERROR("dewarp mesh free failed on error {}", ret);
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return DewarpMeshContext::initialize_dewarp_mesh()
{
    DewarpT mesh = {(int)m_dewarp_mesh.mesh_width,
                    (int)m_dewarp_mesh.mesh_height,
                    (int *)m_dewarp_mesh.mesh_table};

    flip_direction_t flip_dir = FLIP_DIRECTION_NONE;
    rotation_angle_t rotation_angle = ROTATION_ANGLE_0;
    if (m_pre_proc_configs.flip_config.enabled)
        flip_dir = m_pre_proc_configs.flip_config.direction;
    if (m_pre_proc_configs.rotation_config.enabled)
        rotation_angle = m_pre_proc_configs.rotation_config.angle;
    FlipMirrorRot flip_mirror_rot = get_flip_value(flip_dir, rotation_angle);
    RetCodes ret = dis_dewarp_only_grid(m_dis_ctx, m_input_width, m_input_height, flip_mirror_rot, &mesh);
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

media_library_return DewarpMeshContext::configure(pre_proc_op_configurations &pre_proc_op_configs)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_pre_proc_configs = pre_proc_op_configs;
    if (!m_pre_proc_configs.dewarp_config.enabled)
        return MEDIA_LIBRARY_SUCCESS;

    if (pre_proc_op_configs.dewarp_config.enabled &&
        pre_proc_op_configs.rotation_config.enabled &&
        (pre_proc_op_configs.rotation_config.angle == ROTATION_ANGLE_90 ||
         pre_proc_op_configs.rotation_config.angle == ROTATION_ANGLE_270))
    {
        // Swap width and height for rotation 90 or 270
        m_dewarp_output_width = pre_proc_op_configs.input_video_config.resolution.dimensions.destination_height;
        m_dewarp_output_height = pre_proc_op_configs.input_video_config.resolution.dimensions.destination_width;
        LOGGER__INFO("DewarpMeshContext::configure - ROTATION ANGLE {}", pre_proc_op_configs.rotation_config.angle);
    }
    else
    {
        m_dewarp_output_width = pre_proc_op_configs.input_video_config.resolution.dimensions.destination_width;
        m_dewarp_output_height = pre_proc_op_configs.input_video_config.resolution.dimensions.destination_height;
    }

    m_pre_proc_configs = pre_proc_op_configs;
    m_input_width = m_pre_proc_configs.input_video_config.resolution.dimensions.destination_width;
    m_input_height = m_pre_proc_configs.input_video_config.resolution.dimensions.destination_height;
    m_magnification = m_pre_proc_configs.optical_zoom_config.magnification;

    if (!m_is_initialized) // initialize mesh for the first time
    {
        LOGGER__INFO("Initiazing dewarp mesh context");

        initialize_dis_context();

        // Allocate memory for mesh table - doing it outside of initialize_dewarp_mesh for reuse of the buffer
        size_t mesh_size = m_dewarp_mesh.mesh_width * m_dewarp_mesh.mesh_height * 2 * 4;
        dsp_status result = dsp_utils::create_hailo_dsp_buffer(mesh_size, (void **)&m_dewarp_mesh.mesh_table);
        if (result != DSP_SUCCESS)
        {
            LOGGER__ERROR("dewarp mesh initialization failed in the buffer allocation process (tried to allocate buffer in size of {})", mesh_size);
            return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
        }

        initialize_dewarp_mesh();

        m_is_initialized = true;
        LOGGER__INFO("Dewarp mesh init done.");
    }

    if (m_pre_proc_configs.dewarp_config.enabled) // Yes - initialize mesh
        return initialize_dewarp_mesh();

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return DewarpMeshContext::on_frame_vsm_update(struct hailo15_vsm &vsm)
{
    if (!m_pre_proc_configs.dis_config.enabled)
        return MEDIA_LIBRARY_SUCCESS;

    std::unique_lock<std::shared_mutex> lock(m_mutex);

    // Update dewarp mesh with the VSM data to perform DIS
    LOGGER__DEBUG("Updating mesh with VSM");
    DewarpT mesh = {(int)m_dewarp_mesh.mesh_width,
                    (int)m_dewarp_mesh.mesh_height,
                    (int *)m_dewarp_mesh.mesh_table};

    flip_direction_t flip_dir = FLIP_DIRECTION_NONE;
    rotation_angle_t rotation_angle = ROTATION_ANGLE_0;
    if (m_pre_proc_configs.flip_config.enabled)
        flip_dir = m_pre_proc_configs.flip_config.direction;
    if (m_pre_proc_configs.rotation_config.enabled)
        rotation_angle = m_pre_proc_configs.rotation_config.angle;
    FlipMirrorRot flip_mirror_rot = get_flip_value(flip_dir, rotation_angle);

    RetCodes ret = dis_generate_grid(m_dis_ctx, m_input_width, m_input_height, vsm.dx,
                                     vsm.dy, 0, flip_mirror_rot, &mesh);
    if (ret != DIS_OK)
    {
        LOGGER__ERROR("Failed to update mesh with VSM, status: {}", ret);
        return MEDIA_LIBRARY_ERROR;
    }

    m_dewarp_mesh.mesh_table = mesh.mesh_table;
    m_dewarp_mesh.mesh_width = mesh.mesh_width;
    m_dewarp_mesh.mesh_height = mesh.mesh_height;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return DewarpMeshContext::set_optical_zoom(float magnification)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_magnification = magnification;

    // upon optical zoom, dis_library should be reinitialized with modified calibration
    free_dis_context();
    initialize_dis_context();
    initialize_dewarp_mesh();

    return MEDIA_LIBRARY_SUCCESS;
}

dsp_dewarp_mesh_t *DewarpMeshContext::get()
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return &m_dewarp_mesh;
}