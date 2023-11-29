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
#include "dis_interface.h"

#include "camera.h"
#include "dis.h"
#include "dis_math.h"
#include "log.h"

#if LOG_TO_FILE // see log.h
DisFileLog disFileLog;
#endif // LOG_TO_FILE

RetCodes dis_init(void **ctx, dis_config_t &cfg, const char *calib,
                  int32_t calib_bytes, int32_t out_width, int32_t out_height,
                  camera_type_t camera_type, float camera_fov, DewarpT *grid)
{
    if (grid == nullptr)
    {
        LOGE("dis_init: grid = 0");
        return ERROR_INPUT_DATA;
    }
    // if ( grid->mesh_table != nullptr ) {
    //    LOGE("dis_init: grid->mesh_table already points to something");
    //    return ERROR_INPUT_DATA;
    //}
    if (*ctx != nullptr)
    {
        return ERROR_CTX; //*ctx alreay points to something
    }
    *ctx = new DIS;

    if (*ctx == nullptr)
    {
        return ERROR_CTX; //*ctx alreay points to something
    }
    DIS &dis = *reinterpret_cast<DIS *>(*ctx);
    dis.cfg = cfg;

    LOG("dis_init calib %d bytes, out resolution  %dx%d", calib_bytes,
        out_width, out_height);

    if (calib[calib_bytes - 1] != 0)
    {
        LOGE("dis_init: Calib string not terminated by 0 or improper "
             "calib_bytes");
        return ERROR_CALIB;
    }
    if (dis.init_in_cam(calib))
        return ERROR_CALIB; // creates dis.in_cam

    RetCodes ret = dis.init(out_width, out_height, camera_type, camera_fov);
    if (ret != DIS_OK)
        return ret;

    // init grid structure: it tells the outer world what the grid will be
    grid->mesh_width = 1 + (out_width + MESH_CELL_SIZE_PIX - 1) /
                               MESH_CELL_SIZE_PIX; // ceil(width/cell)
    grid->mesh_height = 1 + (out_height + MESH_CELL_SIZE_PIX - 1) /
                                MESH_CELL_SIZE_PIX; // ceil(width/cell)

    dis.calc_out_rays(grid->mesh_width, grid->mesh_height, MESH_CELL_SIZE_PIX,
                      NATURAL);

    return DIS_OK;
}

RetCodes dis_deinit(void **ctx)
{
    if (*ctx == nullptr)
    {
        return ERROR_CTX; //*ctx alreay points to something
    }
    DIS *pdis = reinterpret_cast<DIS *>(*ctx);

    delete pdis;

    *ctx = nullptr;

    return DIS_OK;
}

RetCodes dis_generate_grid(void *ctx, int in_width, int in_height,
                           float motion_x, float motion_y, int32_t panning,
                           FlipMirrorRot flip_mirror_rot, DewarpT *grid)
{
    if (ctx == nullptr)
        return ERROR_CTX; //*ctx alreay points to something
    if (grid == nullptr || grid->mesh_table == nullptr)
        return ERROR_GRID; // grid not allocated

    DIS &dis = *reinterpret_cast<DIS *>(ctx);
    if (!dis.initialized)
        return ERROR_INIT;
    if ((in_width != dis.in_cam.res.x) || (in_height != dis.in_cam.res.y))
    {
        LOGE("dis_generateGrid: INput image resolutiuon differs from the one "
             "in the calibration");
        return ERROR_INPUT_DATA;
    }

    RetCodes ret =
        dis.generate_grid(vec2{motion_x, motion_y}, panning, flip_mirror_rot,
                          *grid); // output in grid->mesh_table[]

    return ret;
}

RetCodes dis_dewarp_only_grid(void *ctx, int in_width, int in_height,
                              FlipMirrorRot flip_mirror_rot, DewarpT *grid)
{
    if (ctx == nullptr)
        return ERROR_CTX; //*ctx alreay points to something
    if (grid == nullptr || grid->mesh_table == nullptr)
        return ERROR_GRID; // grid not allocated

    DIS &dis = *reinterpret_cast<DIS *>(ctx);
    if (!dis.initialized)
        return ERROR_INIT;
    if ((in_width != dis.in_cam.res.x) || (in_height != dis.in_cam.res.y))
    {
        LOGE("dis_generateGrid: INput image resolutiuon differs from the one "
             "in the calibration");
        return ERROR_INPUT_DATA;
    }

    dis.dewarp_only_grid(flip_mirror_rot, *grid);

    return DIS_OK;
}
