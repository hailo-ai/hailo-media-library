#include "ldc_mesh_context.hpp"
#include "dis_interface.h"
#include "media_library_logger.hpp"
#include "media_library_utils.hpp"
#include "dma_memory_allocator.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <stdio.h>

#define CALIBRATION_VECOTR_SIZE 1024
#define DEFAULT_ALPHA 0.1f

LdcMeshContext::LdcMeshContext(ldc_config_t &config)
{
    if (!config.dewarp_config.enabled ||
        config.output_video_config.dimensions.destination_width == 0 ||
        config.output_video_config.dimensions.destination_height == 0)
        return;

    configure(config);
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
    if(m_is_initialized)
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
        if(m_angular_dis_params != nullptr)
        {
            if (m_angular_dis_params->cur_columns_sum != nullptr)
            {
                result = DmaMemoryAllocator::get_instance().free_dma_buffer((void*)m_angular_dis_params->cur_columns_sum);
                if (result != MEDIA_LIBRARY_SUCCESS)
                {
                    LOGGER__ERROR("failed releasing angular dis columns buffer on error {}", result);
                }
            }
            

            // Free angular dis rows buffer
            if (m_angular_dis_params->cur_rows_sum != nullptr)
            {
                result = DmaMemoryAllocator::get_instance().free_dma_buffer((void*)m_angular_dis_params->cur_rows_sum);
                if (result != MEDIA_LIBRARY_SUCCESS)
                {
                    LOGGER__ERROR("failed releasing angular dis rows buffer on error {}", result);
                }
            }
        }
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
            LOGGER__ERROR("Improper calibration file theta2radius[{}] must be monotonically increasing, but it is not ({})", i, calib.theta2radius[i]);
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
            flip_mirror_rot = ROT90;
            break;
        }
        default:
        {
            flip_mirror_rot = ROT90_FLIPV_MIRROR;
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
            flip_mirror_rot = ROT270;
            break;
        }
        default:
        {
            flip_mirror_rot = ROT270_FLIPV_MIRROR;
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

media_library_return LdcMeshContext::initialize_dis_context()
{
    DewarpT dewarp_mesh;

    // Read the sensor calibration and dewarp configuration files
    m_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_VSM);
    std::vector<char> calib_file;
    media_library_return vsm_status = read_vsm_config();
    if (vsm_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("dewarp mesh initialization failed when reading vsm_config");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
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
    RetCodes ret = dis_init(&m_dis_ctx, m_ldc_configs.dis_config, calib,
                            m_input_width, m_input_height,
                            m_ldc_configs.dewarp_config.camera_type, m_ldc_configs.dewarp_config.camera_fov, &dewarp_mesh);
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
    int window_height =  angular_dis_config.vsm_config.height;

    angular_dis_vsm_config_t dsp_vsm_config = {
        .hoffset = angular_dis_config.vsm_config.hoffset,
        .voffset = angular_dis_config.vsm_config.voffset,
        .width = (size_t)window_width,
        .height = (size_t)window_height,
        .max_displacement = angular_dis_config.vsm_config.max_displacement
    };

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

    if(angular_dis_config.enabled && m_angular_dis_params->cur_columns_sum == nullptr && m_angular_dis_params->cur_rows_sum == nullptr)
    {
        // Allocate memory for angular dis buffers
        media_library_return result = DmaMemoryAllocator::get_instance().allocate_dma_buffer((window_width*sizeof(uint16_t)), (void **)&m_angular_dis_params->cur_columns_sum);
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("angular dis buffer initialization failed in the buffer allocation process (tried to allocate buffer in size of {})", window_width*sizeof(uint16_t));
            return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
        }

        result = DmaMemoryAllocator::get_instance().allocate_dma_buffer((window_height*sizeof(uint16_t)), (void **)&m_angular_dis_params->cur_rows_sum);
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("angular dis buffer initialization failed in the buffer allocation process (tried to allocate buffer in size of {})", window_height*sizeof(uint16_t));
            return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}


media_library_return LdcMeshContext::initialize_dewarp_mesh()
{
    DewarpT mesh = {(int)m_dewarp_mesh.mesh_width,
                    (int)m_dewarp_mesh.mesh_height,
                    (int *)m_dewarp_mesh.mesh_table};

    flip_direction_t flip_dir = FLIP_DIRECTION_NONE;
    rotation_angle_t rotation_angle = ROTATION_ANGLE_0;
    if (m_ldc_configs.flip_config.enabled)
        flip_dir = m_ldc_configs.flip_config.direction;
    if (m_ldc_configs.rotation_config.enabled)
        rotation_angle = m_ldc_configs.rotation_config.angle;
    FlipMirrorRot flip_mirror_rot = get_flip_value(flip_dir, rotation_angle);
    DmaMemoryAllocator::get_instance().dmabuf_sync_start((void*)m_dewarp_mesh.mesh_table);
    RetCodes ret = dis_dewarp_only_grid(m_dis_ctx, m_input_width, m_input_height, flip_mirror_rot, &mesh);
    DmaMemoryAllocator::get_instance().dmabuf_sync_end((void*)m_dewarp_mesh.mesh_table);
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
    m_ldc_configs = ldc_configs;
    m_input_width = m_ldc_configs.input_video_config.resolution.dimensions.destination_width;
    m_input_height = m_ldc_configs.input_video_config.resolution.dimensions.destination_height;
    m_magnification = m_ldc_configs.optical_zoom_config.magnification;

    if(!m_ldc_configs.dewarp_config.enabled)
        return MEDIA_LIBRARY_SUCCESS;

    if (!m_is_initialized) // initialize mesh for the first time
    {
        m_angular_dis_params = std::make_shared<angular_dis_params_t>();

        LOGGER__INFO("Initiazing dewarp mesh context");
        ret = initialize_dis_context();
        if(ret != MEDIA_LIBRARY_SUCCESS)
            return ret;

        // Allocate memory for mesh table - doing it outside of initialize_dewarp_mesh for reuse of the buffer
        size_t mesh_size = m_dewarp_mesh.mesh_width * m_dewarp_mesh.mesh_height * 2 * 4;

        media_library_return result = DmaMemoryAllocator::get_instance().allocate_dma_buffer(mesh_size, (void **)&m_dewarp_mesh.mesh_table);
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("dewarp mesh initialization failed in the buffer allocation process (tried to allocate buffer in size of {})", mesh_size);
            return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
        }
    }
    else // free the context and reinitialize, since dis parameters might have changed
    {
        ret = free_dis_context();
        if (ret != MEDIA_LIBRARY_SUCCESS)
            return ret;
    
        ret = initialize_dis_context();
        if(ret != MEDIA_LIBRARY_SUCCESS)
            return ret;
    }

    ret = initialize_angular_dis();
    if(ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    ret = initialize_dewarp_mesh();
    if(ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

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
    if (!m_ldc_configs.dis_config.enabled)
        return MEDIA_LIBRARY_SUCCESS;

    std::unique_lock<std::shared_mutex> lock(m_mutex);

    // Update dewarp mesh with the VSM data to perform DIS
    LOGGER__DEBUG("Updating mesh with VSM");
    DewarpT mesh = {(int)m_dewarp_mesh.mesh_width,
                    (int)m_dewarp_mesh.mesh_height,
                    (int *)m_dewarp_mesh.mesh_table};

    flip_direction_t flip_dir = FLIP_DIRECTION_NONE;
    rotation_angle_t rotation_angle = ROTATION_ANGLE_0;
    if (m_ldc_configs.flip_config.enabled)
        flip_dir = m_ldc_configs.flip_config.direction;
    if (m_ldc_configs.rotation_config.enabled)
        rotation_angle = m_ldc_configs.rotation_config.angle;
    FlipMirrorRot flip_mirror_rot = get_flip_value(flip_dir, rotation_angle);

    DmaMemoryAllocator::get_instance().dmabuf_sync_start((void*)m_dewarp_mesh.mesh_table);
    RetCodes ret = dis_generate_grid(m_dis_ctx, m_input_width, m_input_height, vsm.dx,
                                     vsm.dy, 0, flip_mirror_rot, m_angular_dis_params, &mesh);
    DmaMemoryAllocator::get_instance().dmabuf_sync_end((void*)m_dewarp_mesh.mesh_table);
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