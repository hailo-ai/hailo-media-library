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
#pragma once
#include "dsp_utils.hpp"
#include "media_library_types.hpp"
#include "v4l2_vsm/hailo_vsm.h"

media_library_return init_mesh(void **ctx, dsp_dewarp_mesh_t &dsp_dewarp_mesh, dewarp_config_t &dewarp_config, uint output_width, uint output_height);
media_library_return free_mesh(void **ctx, dsp_dewarp_mesh_t &dsp_dewarp_mesh);
media_library_return generate_dewarp_only_mesh(void* ctx, dsp_dewarp_mesh_t &dsp_dewarp_mesh, uint input_width, uint input_height, flip_direction_t flip_dir, rotation_angle_t rotation_angle);
media_library_return generate_mesh(void* ctx, dsp_dewarp_mesh_t &dsp_dewarp_mesh, uint input_width, uint input_height, hailo15_vsm &vsm, flip_direction_t flip_dir, rotation_angle_t rotation_angle);