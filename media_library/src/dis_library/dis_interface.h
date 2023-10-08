/**
* Copyright 2020 (C) Hailo Technologies Ltd.
* All rights reserved.
*
* Hailo Technologies Ltd. ("Hailo") disclaims any warranties, including, but not limited to,
* the implied warranties of merchantability and fitness for a particular purpose.
* This software is provided on an "AS IS" basis, and Hailo has no obligation to provide maintenance,
* support, updates, enhancements, or modifications.
*
* You may use this software in the development of any project.
* You shall not reproduce, modify or distribute this software without prior written permission.
**/
/**
* @file interface.h
* @brief Contains interface APIs for digital image stabilization and comments about their usage
**/
#ifndef _DIS_INTERFACE_H_
#define _DIS_INTERFACE_H_

#include "interface_types.h"
#include "dewarp.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

/// @brief Initialization of Dis library. Call this API first!
/// dis_init will fill grid width, height, cell_size. Then the caller should
/// allocate memory for the mesh_table buffer of the grid (width*height*sizeof(int) bytes). The caller
/// may create a few DewarpT structures and assign each one to a frame in an external frame buffer queue.
/// DIS does not use the frames, but only the frame motion vector calculated by the HW.
///
/// @param ctx pointer to internally allocated DIS instance.
/// dis_init() will allocate the necessary internal buffers and memory
///  and return a pointer to it in *ctx. All other APIs require this pointer. This way,
///  a few instances of DIS may work together, independently from each other.
///  *ctx must be NULL, otherwise dis_init will return an error.
/// @param cfg contents of the config file (including the terminating 0) read in the memory by the caller.
/// DIS library does not access the file system.
/// @param cfg_bytes size of config file (including the terminating 0)
/// @param calib contents of the camera calibration file (including the terminating 0), see cfg.
/// @param calib_bytes size of camera calibration file (including the terminating 0)
/// @param out_width output size. Necessary to determine grid size (grid cell size is square and constant)
/// @param out_height output size. Necessary to determine grid size (grid cell size is square and constant)
/// @param grid pointer to DewarpT (size of the grid vertexes to be allocated externally).
RetCodes dis_init(void** ctx,
               const char* cfg, int32_t cfg_bytes,
               const char* calib, int32_t calib_bytes,
               int32_t out_width, int32_t out_height,
               DewarpT* grid
               );

/// @brief frees the internal memory for a given Dis instance and sets it to NULL.
/// The caller still needs to deallocate the DewarpT structures.
///
/// @param ctx Dis instance
RetCodes dis_deinit(void** ctx);

/// @brief Calculates the grid for stabilization of the current frame, described by frame
/// motion vector (motion_x, motion_y) - motion between current and the previous frame.
///
/// @param ctx pointer to DIS instance, returned by dis_init
/// @param in_width used just to check whether it is the same as in calibration
/// @param in_height used just to check whether it is the same as in calibration
/// @param motion_x x component current-to-previous frame motion vector in pixels
/// @param motion_y y component current-to-previous frame motion vector in pixels
/// @param panning 0 or 1, shows whether the panning motor rotates the camera intentionally
/// @param flip_mirror_rot as applied on the output image. Actually, the grid is reordered
/// Note!!!: when rotating to 90 or 270 deg, the output images passed to dewarp funcs must be with swapped width/height!
/// @param grid output; grid.mesh_table must be allocated by the caller. This func fills it.
RetCodes dis_generate_grid(void* ctx,
                       int in_width, int in_height,
                       float motion_x, float motion_y,
                       int32_t panning,
                       FlipMirrorRot flip_mirror_rot,
                       DewarpT* grid
                       );

/// @brief Calculates grid for dewarping the input frame only.
///
/// @param ctx pointer to DIS instance, returned by dis_init
/// @param in_width used just to check whether it is the same as in calibration
/// @param in_height used just to check whether it is the same as in calibration
/// @param flip_mirror_rot as applied on the output image. Actually, the grid is reordered
/// Note!!!: when rotating to 90 or 270 deg, the output images passed to dewarp funcs must be with swapped width/height!
/// @param grid output; grid.mesh_table must be allocated by the caller. This func fills it.
RetCodes dis_dewarp_only_grid(void* ctx,
                         int in_width, int in_height,
                         FlipMirrorRot flip_mirror_rot,
                         DewarpT* grid
                         );

#ifdef __cplusplus
};
#endif  // __cplusplus
#endif // _DIS_INTERFACE_H_
