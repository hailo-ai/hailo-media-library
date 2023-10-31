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
#include "generate_mesh.hpp"
#include "math.h"
#include "dis_interface.h"
#include "media_library_logger.hpp"
#include <iostream>
#include <stdio.h>
#include <fstream>

/**
    * @brief Reads a file into a vector of chars
    *
    * @param[in] name - path to the file
    * @param[out] str - vector of chars to read the file into
    *
    * @return 0 on success, -1 on failure
*/
media_library_return read_file(const char* name, std::vector<char>& str)
{
    std::ifstream file(name); 
    if(!file.is_open()) {
        LOGGER__ERROR("Could not open file {}", name);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR; 
    }
    file.seekg(0, std::ios::end);
    str.resize( (size_t)file.tellg() + 1 ); //+1 for terminating 0
    str[file.tellg()] = 0; //put terminating 0
    file.seekg(0);
    file.read(str.data(), str.size());
    return MEDIA_LIBRARY_SUCCESS;
};

/**
    * @brief Initializes the dewarp mesh
    *
    * @param[in] ctx - internal pointer to the dis context
    * @param[out] dsp_dewarp_mesh - dewarp mesh to initialize
    * @param[in] dewarp_config - dewarp configuration to get the paths to the calibration and configuration files
    * @param[in] input_width - input width of the frame
    * @param[in] input_height - input height of the frame
    *
    * @return media_library_return
*/
media_library_return init_mesh(void **ctx, dsp_dewarp_mesh_t &dsp_dewarp_mesh, dewarp_config_t &dewarp_config, dis_config_t &dis_config, uint input_width, uint input_height)
{
    DewarpT dewarp_mesh;

    // Read the sensor calibration and dewarp configuration files
    std::vector<char> calib_file;
    if(read_file(dewarp_config.sensor_calib_path.c_str(), calib_file) != MEDIA_LIBRARY_SUCCESS)
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;

    // Initialize dis dewarp mesh object using DIS library
    RetCodes ret = dis_init(ctx, dis_config, calib_file.data(), \
                            calib_file.size(), input_width, input_height, dewarp_config.camera_type, dewarp_config.camera_fov, &dewarp_mesh);
    if (ret != DIS_OK)
    {
        LOGGER__ERROR("dewarp mesh initialization failed on error {}", ret);
        return MEDIA_LIBRARY_ERROR;
    }

    // Convert stuct to dsp_dewarp_mesh_t
    dsp_dewarp_mesh.mesh_table = dewarp_mesh.mesh_table;
    dsp_dewarp_mesh.mesh_width = dewarp_mesh.mesh_width;
    dsp_dewarp_mesh.mesh_height = dewarp_mesh.mesh_height;

    size_t mesh_size = dsp_dewarp_mesh.mesh_width * dsp_dewarp_mesh.mesh_height * 2 * 4;
    // Allocate memory for mesh table
    dsp_status result = dsp_utils::create_hailo_dsp_buffer(mesh_size, (void **)&dsp_dewarp_mesh.mesh_table);
    if (result != DSP_SUCCESS)
    {
        LOGGER__ERROR("dewarp mesh initialization failed in the buffer allocation process (tried to allocate buffer in size of {})", mesh_size);
        return MEDIA_LIBRARY_ERROR;
    }

    LOGGER__INFO("Dewarp mesh init done. mesh size: {}", mesh_size);
    return MEDIA_LIBRARY_SUCCESS;
}

/**
    * @brief Frees the dewarp mesh
    *
    * @param[in] ctx - internal pointer to the dis context
    * @param[out] dsp_dewarp_mesh - dewarp mesh to free
    *
    * @return media_library_return
*/
media_library_return free_mesh(void **ctx, dsp_dewarp_mesh_t &dewarp_mesh)
{
    void *mesh_table = dewarp_mesh.mesh_table;
    // Free memory for mesh table
    dsp_status result = dsp_utils::release_hailo_dsp_buffer(mesh_table);
    if (result != DSP_SUCCESS)
    {
        return MEDIA_LIBRARY_ERROR;
    }

    RetCodes ret = dis_deinit(ctx);
    if (ret != DIS_OK)
    {
        LOGGER__ERROR("dewarp mesh free failed on error {}", ret);
        return MEDIA_LIBRARY_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}


FlipMirrorRot get_flip_value(flip_direction_t flip_dir, rotation_angle_t rotation_angle)
{
    FlipMirrorRot flip_mirror_rot;

    switch(rotation_angle)
    {
        case ROTATION_ANGLE_90:
        {
            switch(flip_dir)
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
            switch(flip_dir)
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
            switch(flip_dir)
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
            switch(flip_dir)
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

/**
    * @brief Generates the mesh for dewarping the input frame
    *
    * @param[in] ctx - internal pointer to the dis context
    * @param[out] dsp_dewarp_mesh - dewarp mesh to generate
    * @param[in] input_width - input width of the frame
    * @param[in] input_height - input height of the frame
    *
    * @return media_library_return
*/
media_library_return generate_dewarp_only_mesh(void* ctx, dsp_dewarp_mesh_t &dsp_dewarp_mesh, uint input_width, uint input_height, flip_direction_t flip_dir, rotation_angle_t rotation_angle)
{
    DewarpT dewarp_mesh = {dsp_dewarp_mesh.mesh_width, dsp_dewarp_mesh.mesh_height, dsp_dewarp_mesh.mesh_table};
    FlipMirrorRot flip_mirror_rot = get_flip_value(flip_dir, rotation_angle);
    RetCodes ret = dis_dewarp_only_grid(ctx, input_width, input_height, flip_mirror_rot, &dewarp_mesh);
    if (ret != DIS_OK)
    {
        LOGGER__ERROR("dewarp mesh generation failed on error {}", ret);
        return MEDIA_LIBRARY_ERROR;
    }

    dsp_dewarp_mesh.mesh_table = dewarp_mesh.mesh_table;
    dsp_dewarp_mesh.mesh_sq_size = MESH_CELL_SIZE_PIX;
    dsp_dewarp_mesh.mesh_width = dewarp_mesh.mesh_width;
    dsp_dewarp_mesh.mesh_height = dewarp_mesh.mesh_height;
    return MEDIA_LIBRARY_SUCCESS;
}

/**
    * @brief Generates the mesh grid for stabilization of the current frame, described by frame
    * motion vector (vsm)
    *
    * @param[in] ctx - internal pointer to the dis context
    * @param[out] dsp_dewarp_mesh - dewarp mesh to generate
    * @param[in] input_width - input width of the frame
    * @param[in] input_height - input height of the frame
    * @param[in] vsm - pointer to the vsm structure (motion vector)
    *
    * @return media_library_return
*/
media_library_return generate_mesh(void* ctx, dsp_dewarp_mesh_t &dsp_dewarp_mesh, uint input_width, uint input_height, hailo15_vsm &vsm, flip_direction_t flip_dir, rotation_angle_t rotation_angle)
{
    DewarpT dewarp_mesh = {dsp_dewarp_mesh.mesh_width, dsp_dewarp_mesh.mesh_height, dsp_dewarp_mesh.mesh_table};
    FlipMirrorRot flip_mirror_rot = get_flip_value(flip_dir, rotation_angle);

    RetCodes ret = dis_generate_grid(ctx, input_width, input_height, vsm.dx, vsm.dy, 0, flip_mirror_rot, &dewarp_mesh);
    if (ret != DIS_OK)
    {
        LOGGER__ERROR("dewarp mesh generation failed on error {}", ret);
        return MEDIA_LIBRARY_ERROR;
    }

    dsp_dewarp_mesh.mesh_table = dewarp_mesh.mesh_table;
    dsp_dewarp_mesh.mesh_sq_size = MESH_CELL_SIZE_PIX;
    dsp_dewarp_mesh.mesh_width = dewarp_mesh.mesh_width;
    dsp_dewarp_mesh.mesh_height = dewarp_mesh.mesh_height;
    return MEDIA_LIBRARY_SUCCESS;
}