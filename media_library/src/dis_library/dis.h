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
/**
 * @file dis.h
 * @brief DIS (Digital image stabilization) class and methods
 *
 * Main class for methods for generating stabilizing and dewarping grids on the output.
 **/
#ifndef _DIS_DIS_H_
#define _DIS_DIS_H_

#include "camera.h"
#include "dewarp.h"
#include "dis_common.h"
#include "dis_math.h"
#include "interface_types.h"
#include <array>
#include <deque>
#include <memory>
#include <vector>

#ifndef GRID_IS_IN_PIX_INDEXES
// Depends on the warper implementation.
// If the warper assumes that top-left pixel is at coordinates 0,0, then set this to 1.
// If the warper assumes that top-left pixel is at coordinates 0.5,0.5, then set this to 0.
// Normally, top-left pixel (in intput and in output image) is coordinate 0.5,0.5 and the grid's 1st vertex is assumed
// at output coordinates 0,0. However, the warper may not be familiar with this detail, hence if GRID_IS_IN_PIX_INDEXES
// is 1, the mesh is generated such that to compensate - the first vertex is at true output coordinates 0.5,0.5 and all
// vertexes are "true_input_pcoords - 0.5", i.e. input image indexes instead of coordinates.
#define GRID_IS_IN_PIX_INDEXES (1)
#endif

/// Main class for digital image stabilization. Contains input and output camera models, methods for generating
/// stabilizing and dewarping grids on the output.
class DIS
{
public:
    // DIS Configuration parameters
    dis_config_t cfg;
    /// Whether the class is initialized properly.
    bool initialized = false;
    /// Input camera model
    FishEye in_cam;

private:
    // Dewarp Configurations
    camera_type_t m_camera_type = CAMERA_TYPE_FISHEYE;
    float m_camera_fov = 0;
    /// Flip/mirror/rotation code of last processed frame.
    int last_flip_mirror_rot = 0;
    /// Output camera model. Points to either a PinHole or a FishEye class according to config.
    /// Gets free-ed automatically in destructor.
    /// out_cam orientation does not depend on the flip/mirrir/rot. Its resolution is as passed to dis_init()
    std::unique_ptr<Camera> out_cam;
    /// Rays in output camera through grid vertices
    std::vector<vec3> out_rays;

    /// actual camera orientation, accumulated from frame-to-frame MVs, radians
    float in_la = 0;
    float in_lo = 0;
    // Circular buffer storing the motion vectors of the last FMV_HISTORY_LEN (see config) frames.
    // std::deque<vec2> motion_vecs;

    /// Stabilization filter coefficient.
    /// k takes values in the range [0, 1] and determines the strength of the filter and its response delay.
    /// 1 means no filter, rapid response. Small values but >0 mean very strong filter and slow response to
    /// changes in input MVs. Roughly 1/k is the support of an averaging filter and the response time.
    /// The filter is an IIR filter, so a step in the input MVs result in an infinitely long exponential graph
    /// of the stabilized position.
    float k = 0.1f;

    float filt_lo = 0.f, filt_la = 0.f; /// filtered (stabilized) orientation

    /// running average of frame motion vector
    vec2 prev_fmv_mean = {0.f, 0.f};
    /// running average of square frame motion vector
    vec2 prev_fmv_sq_mean = {0.f, 0.f};
    /// running average formula coefficient
    float running_avg_coeff = 0.f;

    /// black corners as angles: positive means stabilized frame view area exceeds the input frame view area.
    /// Negative values tell how much more shake could cause black corners in this frame.
    std::array<float, 4> crn;      // angles, rad  //L,T,R,B
    std::array<float, 4> diag_crn; // diagonal angles, rad  //TL,TR,BR,BL
    /// Available room for stabilization (angles).
    /// If the stabilizing rotation is 0 (don't rotate, just crop), then crn = -room4stab.
    std::array<float, 4> room4stab;      // angles, rad  //L,T,R,B
    std::array<float, 4> diag_room4stab; // diagonal angles, rad  //TL,TR,BR,BL

    /// If stabilizing rotation is too high, it would cause black corners, hence stabilizing rotation is limited
    /// to avoid black corners. Those flags indicate that for debugging and analysis
    char BLKCRN_FLAG_LR = '-'; /// 'L' or 'R' stabilizing rotation limited
    char BLKCRN_FLAG_TB = '-'; /// 'T' or 'B' stabilizing rotation limited

    /// stabilized frame counter
    int frame_cnt = 0;

public:
    DIS(){};

    /// @brief initialize DIS class. parse_config() and init_in_cam() must be called first!
    /// @param out_width output image width
    /// @param out_height output image height
    RetCodes init(int out_width, int out_height, camera_type_t camera_type, float camera_fov);

    /// @brief creates Dis::in_cam
    /// @param calib camera calibration as a string with a terminating 0 read from a file
    /// First row is a comment - skipped. Next rows are as follows:
    /// width, height : resolution of the calibration image, used during lens calibration.
    /// If it differs from the input frames resolution, the calibration is not relevant
    /// for the input frames.
    /// Optical center x, y : in pixel coordinates, top-left pixel is 0.5,0.5
    /// 1025 values for radius in pixels for theta = 0: pi/1024 : pi. !!! MUST be monotonically increasing !!!
    int init_in_cam(dis_calibration_t calib);

    /// @brief fills out output camera rays in field out_rays
    /// @param grid_w grid width
    /// @param grid_h grid height
    /// @param grid_sq grid square size
    /// @param flip_mirror_rot as applied on the output image
    void calc_out_rays(int grid_w, int grid_h, int grid_sq, FlipMirrorRot flip_mirror_rot);

    /// @brief Calculates the grid for stabilization of the current frame, described by frame
    /// motion vector between current and the previous frame.
    /// @param fmv motion vector per frame
    /// @param panning panning per frame
    /// @param flip_mirror_rot as applied on the output image
    /// @param grid output grid
    RetCodes generate_grid(vec2 fmv, int32_t panning, FlipMirrorRot flip_mirror_rot, DewarpT &grid);

    /// @brief Calculates grid for dewarping the input frame only.
    /// @param flip_mirror_rot as applied on the output image
    /// @param grid output grid
    RetCodes dewarp_only_grid(FlipMirrorRot flip_mirror_rot, DewarpT &grid);

private:
    /// @brief Generates grid, which only resizes the input image into the output one. Used for debug.
    void gen_resize_grid(DewarpT &grid);

    /// @brief checks for black corners, and if necessary adjusts stabilizing angles so as to not go out of
    /// input frame FoV
    /// @param stab_lo stabilizing rotation angle (longitude)
    /// @param stab_la stabilizing rotation angle (latitude)
    bool black_corner_adjust(float &stab_lo, float &stab_la);
};

#endif //  _DIS_DIS_H_
