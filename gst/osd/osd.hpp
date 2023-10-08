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

#include <gst/video/video.h>
#include <map>
#include <vector>
#include "osd_utils.hpp"
#include "media_library/dsp_utils.hpp"

typedef enum
{
    OSD_STATUS_UNINITIALIZED = -1,
    OSD_STATUS_OK,
} osd_status_t;

class OsdParams
{
public:
    std::vector<osd::staticText> static_texts;
    std::vector<osd::staticImage> static_images;
    std::vector<osd::dateTime> date_times;

    OsdParams(std::vector<osd::staticText> static_texts,
              std::vector<osd::staticImage> static_images,
              std::vector<osd::dateTime> date_times) {
        this->static_texts = static_texts;
        this->static_images = static_images;
        this->date_times = date_times;
    }
};

__BEGIN_DECLS
// OsdParams *load_json_config(const std::string config_path);
OsdParams *load_json_config(const std::string config_path, std::string config_str, bool use_str);
void free_param_resources(OsdParams *params_ptr);
osd_status_t initialize_overlay_images(OsdParams *params, int full_image_width, int full_image_height);
osd_status_t blend_all(dsp_image_properties_t &input_image_properties, size_t image_width, size_t image_height, OsdParams *params);
__END_DECLS