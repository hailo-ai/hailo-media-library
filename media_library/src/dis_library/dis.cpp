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
#include "dis.h"

#include "camera.h"
#include "dewarp.h"
#include "dis_common.h"
#include "dis_math.h"
#include "interface_types.h"
#include "log.h"

#include <map>
#include <sstream>

/// Map from the possible FlipMirrorRot values to their corresponding rotation
/// matrices.
const std::map<int, mat2> ROT_MAT_MAP = {
    {0, {1, 0, 0, 1}}, {1, {0, -1, 1, 0}}, {2, {-1, 0, 0, -1}}, {3, {0, 1, -1, 0}}, {4, {-1, 0, 0, 1}}, {5, {0, -1, -1, 0}}, {6, {1, 0, 0, -1}}, {7, {0, 1, 1, 0}}};

///////////////////////////////////////////////////////////////////////////////
// init_in_cam()
///////////////////////////////////////////////////////////////////////////////
int DIS::init_in_cam(const char *calib_data_str)
{
    std::istringstream ss(std::string(calib_data_str, strlen(calib_data_str)));
    std::istream &file(ss);

    ivec2 res;
    // Optical center in pixel coordinates. top-left pixel has coordinates (0.5,
    // 0.5)
    vec2 oc = {1, 1};
    float theta2radius[1025];

    // Ignore first line - it is a comment
    file.ignore(1024, '\n');

    std::string row;
    std::getline(file, row);
    res.x = atoi(row.c_str());
    if (file.eof())
        return -1;
    std::getline(file, row);
    res.y = atoi(row.c_str());
    if (file.eof())
        return -1;
    std::getline(file, row);
    oc.x = atof(row.c_str());
    if (file.eof())
        return -1;
    std::getline(file, row);
    oc.y = atof(row.c_str());
    if (file.eof())
        return -1;
    std::getline(file, row);
    theta2radius[0] = atof(row.c_str());
    if (file.eof())
        return -1;
    if (theta2radius[0] != 0)
    {
        LOGE(
            "Improper calibration file theta2radius[0] must be 0, but it is %f",
            theta2radius[0]);
    }
    for (uint32_t i = 1;
         !file.eof() && i < sizeof(theta2radius) / sizeof(theta2radius[0]); ++i)
    {
        std::getline(file, row);
        theta2radius[i] = atof(row.c_str());

        if (theta2radius[i] <= 0)
        {
            LOGE("theta2radius[0] contain positive radii. Index %d is %f", i,
                 theta2radius[i]);
            return -1;
        }
        if (theta2radius[i] < theta2radius[i - 1])
        {
            LOGE("Improper calibration file theta2radius[0] must be "
                 "monotonically increasing, but it is not at "
                 "index %d (%f)",
                 i, theta2radius[i]);
            return -1;
        }
    }

    in_cam.init(oc, res, theta2radius);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// init_in_cam()
///////////////////////////////////////////////////////////////////////////////
RetCodes DIS::init(int out_width, int out_height, camera_type_t camera_type,
                   float camera_fov)
{
    m_camera_type = camera_type;
    m_camera_fov = camera_fov;

    if (out_width <= 0 || out_height <= 0)
    {
        LOGE("Output size my be between 2 and 4095. Otherwise the "
             "grid.mesh_table format can not be ");
        return ERROR_INPUT_DATA;
    }

    // create input camera from 'calib' structure
    if (in_cam.res.x <= 1 || in_cam.res.x >= 4096 || in_cam.res.y <= 1 ||
        in_cam.res.y >= 4096)
    {
        LOGE("Input size my be between 2 and 4095. Otherwise the "
             "grid.mesh_table format can not be ");
        return ERROR_CALIB;
    }

    float out_diag = vec2(out_width, out_height).len();
    float max_out_fov = 0; // for print
    // create virtual camera
    if (m_camera_type == CAMERA_TYPE_PINHOLE)
    { // pinhole
        float flen;
        const float in_tan_ltrb[4] = {
            tan(std::min(in_cam.ltrb[0], RADIANS(89.9))),
            tan(std::min(in_cam.ltrb[1], RADIANS(89.9))),
            tan(std::min(in_cam.ltrb[2], RADIANS(89.9))),
            tan(std::min(in_cam.ltrb[3], RADIANS(89.9)))};
        // find max possible output fov
        // The real cameras practically have always barrel distortions. So, if
        // the output is pinhole, H- or V-FOV is always the bottle-neck and the
        // corners see more than needed.
        flen = std::max(
            out_width / (in_tan_ltrb[0] + in_tan_ltrb[2]),
            out_height /
                (in_tan_ltrb[1] +
                 in_tan_ltrb[3])); // std::max() : smaller FOV, bigger flen
        max_out_fov = 2 * std::atan2(0.5f * out_diag, flen);
        if (m_camera_fov > 0.f)
        { // configured FOV
            flen = 0.5f * out_diag /
                   tan(RADIANS(std::min(m_camera_fov / 2, 89.9f)));
        }
        // calc out OC such that the cropping to be symmetrical
        vec2 oc;
        oc.x = out_width * 0.5f + flen * (in_tan_ltrb[0] - in_tan_ltrb[2]) / 2;
        oc.y = out_height * 0.5f + flen * (in_tan_ltrb[1] - in_tan_ltrb[3]) / 2;
        // create virtual pinhole camera
        out_cam = std::make_unique<PinHole>(
            PinHole(flen, oc, ivec2(out_width, out_height)));
    }
    else if (m_camera_type == CAMERA_TYPE_FISHEYE) // fisheye
    {
        float out_fov = 0;
        // find max possible output fov
        // If output is fisheye, it has barrel distortions. They may be bigger
        // or smaller than the input camera. So, find the limitting one of the 3
        // FOVs: H,V,D. Note, aspect ratio of input and output may differ, so
        // their diagonal FOVs appear in different directions. Since the camera
        // model is radial, an output and an input pixels seeing same scen are
        // situated on the same radial line. The above is true only if the
        // optical center and the geometrical center coincide. Here, the output
        // OC is made such that to correspond to input OC, i.e. the shape of the
        // input frame warped on the output frame to look radially symmetrical.
        // Hence this assumption is close to the truth. On the other hand, small
        // potential black corrers (due to the described simplification) will not
        // be visible. AEven if they are visible, it is always an option for
        // configure the FOV explicitly to a certain value. Calc DFOV, assuming
        // each if in_fov_h,v,d is the bottle-neck and choose the minimum value.
        // Note, out HFOV/VFOV/DFOV = width/height/diagonal because for fisheye
        // radius = k * theta

        // crop the input to same aspect ratio as output (the corners) should be
        // in same direction on the sensor plain.
        float crop_in_y =
            std::min(in_cam.res.y, in_cam.res.x * out_height / out_width);
        float crop_in_x =
            std::min(in_cam.res.x, in_cam.res.y * out_width / out_height);

        // find minimum half diagonal FOV -   the minimum theta angle of all 4
        // corners
        float in_fov_d =
            2 * in_cam.rad2theta(std::hypotf(crop_in_x / 2,
                                             crop_in_y / 2)); // input half DFOV

        out_fov = in_fov_d;
        out_fov = std::min(out_fov, (in_cam.ltrb[0] + in_cam.ltrb[2]) *
                                        out_diag / out_width);
        out_fov = std::min(out_fov, (in_cam.ltrb[1] + in_cam.ltrb[3]) *
                                        out_diag / out_height);
        max_out_fov = out_fov;

        if (m_camera_fov > 0.f)
        { // configured FOV
            out_fov = RADIANS(m_camera_fov);
        }

        // calc out OC such that the cropping to be symmetrical. Not accurate
        // when DFOV is the limitation, but accurate calc is too complex. DFOV is
        // the limitation when output camera is more distorted than the input
        // one, which is not a practical case.
        vec2 oc;
        float flen = out_diag / out_fov; // fisheye: rad = flen * theta
        oc.x = out_width * 0.5f + flen * (in_cam.ltrb[0] - in_cam.ltrb[2]) / 2;
        oc.y = out_height * 0.5f + flen * (in_cam.ltrb[1] - in_cam.ltrb[3]) / 2;

        // make F-theta for pure fisheye camera
        float theta2r[FishEye::theta2r_size];
        for (int i = 0; i < FishEye::theta2r_size; i++)
        {
            theta2r[i] = i * (flen * FishEye::theta_step);
        }
        // create virtual fisheye camera
        out_cam = std::make_unique<FishEye>(
            FishEye(oc, ivec2(out_width, out_height), theta2r));
    }
    else if (m_camera_type ==
             CAMERA_TYPE_INPUT_DISTORTIONS) // input distortions
    {
        // I.e. the output image when in the center is cropped and scaled
        // version of the input image theta2rad is same as input, but scaled to
        // get the out_diag/2 at out_fov/2
        float s, out_fov;
        // find max possible output fov
        // crop the input to same aspect ratio as output (the corners) should be
        // in same direction on the sensor plain.
        float crop_in_y =
            std::min(in_cam.res.y, in_cam.res.x * out_height / out_width);
        float crop_in_x =
            std::min(in_cam.res.x, in_cam.res.y * out_width / out_height);
        float crop_diag = std::hypotf(crop_in_x, crop_in_y);
        out_fov = 2 * in_cam.rad2theta(crop_diag / 2);
        max_out_fov = out_fov;
        s = out_diag / crop_diag;

        if (m_camera_fov > 0.f)
        { // configured FOV
            out_fov = RADIANS(m_camera_fov);
            s = out_diag / (2 * in_cam.theta2rad(out_fov / 2));
        }
        vec2 oc;
        oc.x = 0.5f * out_width + s * (in_cam.oc.x - 0.5f * in_cam.res.x);
        oc.y = 0.5f * out_height + s * (in_cam.oc.y - 0.5f * in_cam.res.y);

        // make F-theta for pure fisheye camera
        float theta2r[FishEye::theta2r_size];
        for (int i = 0; i < FishEye::theta2r_size; i++)
        {
            theta2r[i] = s * in_cam.theta2r[i];
        }
        // create virtual fisheye camera
        out_cam = std::make_unique<FishEye>(
            FishEye(oc, ivec2(out_width, out_height), theta2r));
    }

    const float ONE_DEG_IN_RADS = RADIANS(1.0f);

    room4stab[0] = in_cam.ltrb[0] - out_cam->ltrb[0];
    room4stab[1] = in_cam.ltrb[1] - out_cam->ltrb[1];
    room4stab[2] = in_cam.ltrb[2] - out_cam->ltrb[2];
    room4stab[3] = in_cam.ltrb[3] - out_cam->ltrb[3];

    diag_room4stab[0] = in_cam.diag_ltrb[0] - out_cam->diag_ltrb[0];
    diag_room4stab[1] = in_cam.diag_ltrb[1] - out_cam->diag_ltrb[1];
    diag_room4stab[2] = in_cam.diag_ltrb[2] - out_cam->diag_ltrb[2];
    diag_room4stab[3] = in_cam.diag_ltrb[3] - out_cam->diag_ltrb[3];

    // check if output FOV is an allowed value
    for (int i = 0; i < 4; i++)
    {
        if (room4stab[i] <= -1e-5)
        { // if <=0, but leave some room for quantization errors when automaticx
          // full-fov
            LOGE("Output camera FOV is too large.");
            return ERROR_CONFIG;
        }
    }
    for (int i = 0; i < 4; i++)
    {
        if (room4stab[i] < ONE_DEG_IN_RADS)
        {
            LOG("WARNING: Large output camera FOV may cause stabilization to "
                "be unoptimal. Black corners may appear.");
            break;
        }
    }
    LOG("outFOV % .2f deg (max %.2f), room4stab deg LTBR: %.3f %.3f %.3f %.3f",
        DEGREES(out_cam->fov), DEGREES(max_out_fov), DEGREES(room4stab[0]),
        DEGREES(room4stab[1]), DEGREES(room4stab[2]), DEGREES(room4stab[3]));

    k = cfg.minimun_coefficient_filter;

    //    // allocate FMVs circular buffer
    //    for (int i = 0; i < cfg.FMV_HISTORY_LEN; i++)
    //        motion_vecs.push_back(vec2{0,0});

    last_flip_mirror_rot = NATURAL;
    initialized = true;
    return DIS_OK;
}

///////////////////////////////////////////////////////////////////////////////
// gen_resize_grid()
// Osed for debug. generate grid, which only resizes the input image into the
// output one
///////////////////////////////////////////////////////////////////////////////
void DIS::gen_resize_grid(DewarpT &grid)
{
    vec2 rsz(float(in_cam.res.x) / out_cam->res.x,
             float(in_cam.res.y) / out_cam->res.y);
    for (int r = 0; r < grid.mesh_height; r++)
        for (int c = 0; c < grid.mesh_width; c++)
        {
            vec2 pt(c * MESH_CELL_SIZE_PIX, r * MESH_CELL_SIZE_PIX);
#if GRID_IS_IN_PIX_INDEXES
            pt = pt + vec2(0.5f, 0.5f); // convert index to coordinate
#endif
            pt.x = pt.x * rsz.x;
            pt.y = pt.y * rsz.y;
#if GRID_IS_IN_PIX_INDEXES
            pt = pt - vec2(0.5f, 0.5f); // convert coordinate to index
#endif
            int ind = (r * grid.mesh_width + c) * 2;
            grid.mesh_table[ind] = pt.x * (1 << MESH_FRACT_BITS);     // x
            grid.mesh_table[ind + 1] = pt.y * (1 << MESH_FRACT_BITS); // y
        }
}
///////////////////////////////////////////////////////////////////////////////
// generate_grid()
///////////////////////////////////////////////////////////////////////////////
RetCodes DIS::generate_grid(vec2 fmv, int32_t panning,
                            FlipMirrorRot flip_mirror_rot, DewarpT &grid)
{
    if (cfg.debug.generate_resize_grid)
    { // generate grid, which only resizes the input image into the output one
        gen_resize_grid(grid);
        return DIS_OK;
    }

    if (std::abs(fmv.x) > in_cam.res.x * 0.5f ||
        std::abs(fmv.y) > in_cam.res.y * 0.5f)
    {
        LOGE("fmv with impossible value %f.1 %.1f", fmv.x, fmv.y);
        return ERROR_INPUT_DATA; // impossible fmv values
    }

    // analyze FMV and decide whether valid (caused by camera motion) or fake
    // (caused by moving object in the scene)
    running_avg_coeff = std::max(1.0f * cfg.running_average_coefficient,
                                 1.0f / (frame_cnt + 1));

    vec2 fmv_mean =
        prev_fmv_mean * (1 - running_avg_coeff) + fmv * running_avg_coeff;
    vec2 fmv_sq_mean{prev_fmv_sq_mean.x * (1 - running_avg_coeff) +
                         fmv.x * fmv.x * running_avg_coeff,
                     prev_fmv_sq_mean.y * (1 - running_avg_coeff) +
                         fmv.y * fmv.y * running_avg_coeff};
    vec2 dev_from_mean = fmv - fmv_mean;

    vec2 var{std::max(1.f, fmv_sq_mean.x - fmv_mean.x * fmv_mean.x) *
                 cfg.std_multiplier * cfg.std_multiplier,
             std::max(1.f, fmv_sq_mean.y - fmv_mean.y * fmv_mean.y) *
                 cfg.std_multiplier * cfg.std_multiplier};

    // char FMV_LIMITED = '-';
    // clamp outlier motion vectors
    if (dev_from_mean.x * dev_from_mean.x > var.x ||
        dev_from_mean.y * dev_from_mean.y > var.y)
    {
        fmv.x = prev_fmv_mean.x;
        fmv.y = prev_fmv_mean.y;

        // FMV_LIMITED = '+';
    }

    prev_fmv_mean = fmv_mean;
    prev_fmv_sq_mean = fmv_sq_mean;

    //    //filter FMV track and find new stabilized position
    //    //push FMV to the buffer
    //    store_motion_vec(fmv);

    // convert MVs to camera angles. DIS assumes rotational camera shake and
    // stabilizes it. Translational shake in practice is less important because
    // it affects the image by scale 1/distance-to-object, which is usually
    // small. Also, camera translation causes close objects to move wrt the
    // background, which makes it impossible to stabilize by a simple warp (the
    // scene 3D mad is necessary).
    float fmv_lo = in_cam.rad2theta(fmv.x);
    float fmv_la = in_cam.rad2theta(fmv.y);

    // accumulate the current frame-to-frame rotation into orientation since the
    // beginning. It is then filtered to get the intentional orientation
    // trajectory and the difference between the actual and filtered orientation
    // is the stabilizing rotation for each frame.
    in_lo += fmv_lo;
    in_la += fmv_la;

    // filter
    filt_lo = (in_lo - filt_lo) * k + filt_lo;
    filt_la = (in_la - filt_la) * k + filt_la;
    if (cfg.debug.fix_stabilization)
    {
        filt_lo = cfg.debug.fix_stabilization_longitude;
        filt_la = cfg.debug.fix_stabilization_longitude;
    }

    // stabilizing rotation is the difference between actual and stabilized
    // orientation
    float stab_la = filt_la - in_la;
    float stab_lo = filt_lo - in_lo;

    if (!cfg.debug.fix_stabilization && cfg.black_corners_correction_enabled)
    {
        // check if black corners will appear with this stabilizing rotation. If
        // so, limit (decrease) the stabilizing rotation.
        if (black_corner_adjust(stab_lo, stab_la))
        {
            // if limitation occurred, update the filtered filt_la/lo, so next
            // filtered position will be close to the current one. Otherwise,
            // limitations, appearing at the peaks of shaking cause sudden
            // jump-and-return frames within otherwise stable output video.
            filt_la = in_la + stab_la;
            filt_lo = in_lo + stab_lo;
        }
    }
    // LOG("stats %d MV: %.1f, %.1f, %.4f, %.4f , grossErr: %c ; orient: %.3f,
    // %.3f ; stab: %.3f, %.3f ; "
    //     "CRN: %.3f, %.3f, %.3f, %.3f , limited %c, %c ; k: %.4f",
    //     frame_cnt,
    //     fmv.x, fmv.y, fmv_lo, fmv_la, //FMV
    //     FMV_LIMITED, //whether motion vector was limited by statistics
    //     in_lo, in_la, //actual orientation
    //     filt_lo, filt_la,  //stabilized orientation
    //     crn[0], crn[1], crn[2], crn[3], BLKCRN_FLAG_LR, BLKCRN_FLAG_TB,
    //     //black corners (negative: OK, positive: NOT OK)
    //     k
    // );

    // adjust k according to statistics
    // if black corners appear, weaken the filter (increase k). However, don't
    // wait for black corners to appear and get limited - if the filtered orient
    // is close to black corners - increase k.
    bool weaken = false;
    for (int i = 0; i < 4; i++)
    {
        if (crn[i] > -cfg.black_corners_threshold * room4stab[i])
        {
            k = std::min(k + cfg.increment_coefficient_threshold, 1.f);
            weaken = true;
            break;
        }
    }
    if (!weaken)
    {
        // decrease k down to K_MIN : strengthen the filter if it was weakened
        k = std::max(cfg.minimun_coefficient_filter,
                     k - cfg.decrement_coefficient_threshold);
    }

    // convert stabilizing rotation from longitude/latitude to rotation matrix
    float cos_lo = std::cos(stab_lo);
    float sin_lo = std::sin(stab_lo);
    float cos_la = std::cos(stab_la);
    float sin_la = std::sin(stab_la);
    mat3 stab_rot = {cos_lo, 0, sin_lo,
                     -sin_la * sin_lo, cos_la, sin_la * cos_lo,
                     -cos_la * sin_lo, -sin_la, cos_la * cos_lo};

    // if the output rotation is changed, swap the grid size and re-calculate
    // output rays - see calc_out_rays()
    int cur_flip_mirror_rot = static_cast<int>(flip_mirror_rot);
    if (cur_flip_mirror_rot != last_flip_mirror_rot)
    {
        if ((cur_flip_mirror_rot - last_flip_mirror_rot) % 2 != 0)
        {
            std::swap(grid.mesh_width, grid.mesh_height);
        }
        last_flip_mirror_rot = cur_flip_mirror_rot;
        calc_out_rays(grid.mesh_width, grid.mesh_height, MESH_CELL_SIZE_PIX,
                      flip_mirror_rot);
    }

    // project out-vertexes rays and generate grid
    for (int y = 0; y < grid.mesh_height; y++)
    {
        for (int x = 0; x < grid.mesh_width; x++)
        {
            int ind = y * grid.mesh_width + x;

            vec2 pt = in_cam.ray2point(stab_rot * out_rays[ind]);
#if GRID_IS_IN_PIX_INDEXES
            pt = pt - vec2(0.5f, 0.5f); // convert coordinate to index
#endif

            grid.mesh_table[ind * 2] = pt.x * (1 << MESH_FRACT_BITS);     // x
            grid.mesh_table[ind * 2 + 1] = pt.y * (1 << MESH_FRACT_BITS); // y
        }
    }

    frame_cnt++;

    return DIS_OK;
}

///////////////////////////////////////////////////////////////////////////////
// dewarp_only_grid()
///////////////////////////////////////////////////////////////////////////////
RetCodes DIS::dewarp_only_grid(FlipMirrorRot flip_mirror_rot, DewarpT &grid)
{
    if (cfg.debug.generate_resize_grid)
    { // generate grid, which only resizes the input image into the output one
        gen_resize_grid(grid);
        return DIS_OK;
    }

    // if the output rotation is changed, swap the grid size and re-calculate
    // output rays - see calc_out_rays()
    int cur_flip_mirror_rot = static_cast<int>(flip_mirror_rot);
    if (cur_flip_mirror_rot != last_flip_mirror_rot)
    {
        if ((cur_flip_mirror_rot - last_flip_mirror_rot) % 2 != 0)
        {
            std::swap(grid.mesh_width, grid.mesh_height);
        }
        last_flip_mirror_rot = cur_flip_mirror_rot;
        calc_out_rays(grid.mesh_width, grid.mesh_height, MESH_CELL_SIZE_PIX,
                      flip_mirror_rot);
    }

    for (int y = 0; y < grid.mesh_height; y++)
    {
        for (int x = 0; x < grid.mesh_width; x++)
        {
            int ind = y * grid.mesh_width + x;

            vec2 pt = in_cam.ray2point(out_rays[ind]); // xi, yi
#if GRID_IS_IN_PIX_INDEXES
            pt = pt - vec2(0.5f, 0.5f); // convert coordinate to index
#endif

            grid.mesh_table[ind * 2] = pt.x * (1 << MESH_FRACT_BITS);     // x
            grid.mesh_table[ind * 2 + 1] = pt.y * (1 << MESH_FRACT_BITS); // y
        }
    }

    frame_cnt++;

    return DIS_OK;
}
///////////////////////////////////////////////////////////////////////////////
// calc_out_rays
///////////////////////////////////////////////////////////////////////////////
void DIS::calc_out_rays(int grid_w, int grid_h, int grid_sq,
                        FlipMirrorRot flip_mirror_rot)
{
    // output rays are the rays corresponding to each vertex in of the grid,
    // which is a point in the output image The vertexes positions in the output
    // image do not chang in time, so they and their corresponding rays are
    // calculated at init time or at changing the output image rotation.
    // Output image rotation is not related to the output camera - it is
    // implemented as output image rotation, i.e. es if the output image is
    // generated without rotation (out_cam does not knoa about it) and then the
    // image is rotated/flipped/mirrored.
    if (out_rays.size() != (uint32_t)(grid_w * grid_h))
    {
        out_rays.resize(grid_w * grid_h);
    }

    mat2 rot_mat = ROT_MAT_MAP.at(static_cast<int>(flip_mirror_rot));

    vec2 gc_cam(out_cam->res.x * 0.5f, out_cam->res.y * 0.5f);
    vec2 gc_out = gc_cam;
    if (flip_mirror_rot & 1)
    { // out buffer is rotated (portrait)
        std::swap(gc_out.x, gc_out.y);
    }

    for (int y = 0; y < grid_h; y++)
    {
        for (int x = 0; x < grid_w; x++)
        {
            vec2 pto(x * grid_sq - gc_out.x, y * grid_sq - gc_out.y);
#if GRID_IS_IN_PIX_INDEXES
            pto = pto + vec2(0.5f, 0.5f); // convert index to coordinate
#endif
            vec2 pt = rot_mat * pto + gc_cam;

            vec3 ray = out_cam->point2ray(pt);
            out_rays[y * grid_w + x] = ray;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// store_motion_vec
///////////////////////////////////////////////////////////////////////////////
// void DIS::store_motion_vec(vec2 fmv)
//{
//    motion_vecs.pop_front();
//    motion_vecs.push_back(fmv);
//}

///////////////////////////////////////////////////////////////////////////////
// black_corner_adjust
///////////////////////////////////////////////////////////////////////////////
bool DIS::black_corner_adjust(float &stab_lo, float &stab_la)
{
    crn[0] = -room4stab[0] - stab_lo;
    crn[1] = -room4stab[1] - stab_la;
    crn[2] = -room4stab[2] + stab_lo;
    crn[3] = -room4stab[3] + stab_la;

    diag_crn[0] =
        std::max(-diag_room4stab[3] - stab_lo, -diag_room4stab[0] - stab_lo);
    diag_crn[1] =
        std::max(-diag_room4stab[0] - stab_la, -diag_room4stab[1] - stab_la);
    diag_crn[2] =
        std::max(-diag_room4stab[1] + stab_lo, -diag_room4stab[2] + stab_lo);
    diag_crn[3] =
        std::max(-diag_room4stab[2] + stab_la, -diag_room4stab[3] + stab_la);

    crn[0] = std::max(diag_crn[0], crn[0]);
    crn[1] = std::max(diag_crn[1], crn[1]);
    crn[2] = std::max(diag_crn[2], crn[2]);
    crn[3] = std::max(diag_crn[3], crn[3]);

    bool limited = false;
    BLKCRN_FLAG_TB = '-';
    BLKCRN_FLAG_LR = '-';
    if (crn[0] > 0)
    {
        stab_lo = crn[0] + stab_lo;
        BLKCRN_FLAG_LR = 'L';
        limited = true;
    }
    else if (crn[2] > 0)
    {
        stab_lo = -crn[2] + stab_lo;
        BLKCRN_FLAG_LR = 'R';
        limited = true;
    }

    if (crn[1] > 0)
    {
        stab_la = crn[1] + stab_la;
        BLKCRN_FLAG_TB = 'T';
        limited = true;
    }
    else if (crn[3] > 0)
    {
        stab_la = -crn[3] + stab_la;
        BLKCRN_FLAG_TB = 'B';
        limited = true;
    }
    return limited;
}
