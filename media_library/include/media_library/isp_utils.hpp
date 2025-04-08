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
 * @file isp_utils.hpp
 * @brief MediaLibrary ISP CPP API module
 **/

#pragma once

#include <iostream>
#include <filesystem>
#include <tl/expected.hpp>
#include "media_library_types.hpp"
#include "v4l2_ctrl.hpp"

#define V4L2_DEVICE_NAME "/dev/video0"
#define TRIPLE_A_CONFIG_PATH "/usr/bin/3aconfig.json"
#define SONY_CONFIG_PATH "/usr/bin/sony_imx678.xml"
#define SENSOR0_ENTRY_CONFIG_PATH "/usr/bin/Sensor0_Entry.cfg"
#define MEDIA_SERVER_CONFIG_PATH "/usr/bin/media_server_cfg.json"
#define ISP_CONFIG_PATH "/usr/lib/medialib/default_config/isp_profiles/"

#define _DAYLIGHT "daylight/"
#define _LOWLIGHT "lowlight/"
#define _HDR "hdr/"
#define _2DOL "2dol/"
#define _3DOL "3dol/"

#define _3A_CONFIG "3aconfig.json"
#define _SONY_CONFIG "sony_imx678.xml"
#define _MEDIA_SERVER_CONFIG "media_server_cfg.json"
#define ISP_SENSOR0_ENTRY_CONFIG "Sensor0_Entry.cfg"

// 3A config
#define ISP_DAYLIGHT_3A_CONFIG (ISP_CONFIG_PATH _DAYLIGHT _3A_CONFIG)
#define ISP_LOWLIGHT_3A_CONFIG (ISP_CONFIG_PATH _LOWLIGHT _3A_CONFIG)
#define ISP_HDR_2DOL_3A_CONFIG (ISP_CONFIG_PATH _HDR _2DOL _3A_CONFIG)

// Sensor0 config
#define ISP_DAYLIGHT_SENSOR0_CONFIG (ISP_CONFIG_PATH _DAYLIGHT ISP_SENSOR0_ENTRY_CONFIG)
#define ISP_LOWLIGHT_SENSOR0_CONFIG (ISP_CONFIG_PATH _LOWLIGHT ISP_SENSOR0_ENTRY_CONFIG)
#define ISP_HDR_2DOL_SENSOR0_CONFIG (ISP_CONFIG_PATH _HDR _2DOL ISP_SENSOR0_ENTRY_CONFIG)

// Media server config
#define MEDIA_SERVER_CONFIG "/usr/bin/media_server_cfg.json"

/** @defgroup isp_utils_definitions MediaLibrary ISP utilities CPP API
 * definitions
 *  @{
 */

/**
  ISP interfaces and utilities
*/
namespace isp_utils
{
typedef struct
{
    std::vector<uint64_t> rhs_times;
    std::vector<uint64_t> shr_times;
    uint64_t vmax;
    uint64_t hmax;
} isp_hdr_sensor_params_t;

extern bool m_auto_configure;
void set_auto_configure(bool auto_configure);
void set_isp_config_files_path(std::string &path);
void override_file(const std::string &src, const std::string &dst);
void set_daylight_configuration();
void set_lowlight_configuration();
void set_hdr_configuration();
std::string find_sensor_name();
void setup_hdr(bool is_4k, hdr_dol_t dol);
void setup_sdr(bool is_4k);
void set_hdr_ratios(float ls_ratio, float vs_ratio);
/* When HDR is on, set the offeset to be the time of the frame capture in the sensor, so NNC timings will not effect it
 */
void set_hdr_forward_timestamp(bool enabled);
tl::expected<isp_hdr_sensor_params_t, media_library_return> get_hdr_isp_params(
    uint8_t num_exposures, uint64_t line_readout_time, std::shared_ptr<v4l2::v4l2ControlRepository> v4l2_ctrl_repo,
    bool force_refresh = false);
} // namespace isp_utils

/** @} */ // end of isp_utils_definitions
