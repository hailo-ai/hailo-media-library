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

#include "media_library_types.hpp"

#define V4L2_DEVICE_NAME "/dev/video0"
#define TRIPLE_A_CONFIG_PATH "/usr/bin/3aconfig.json"
#define SONY_CONFIG_PATH "/usr/bin/sony_imx678.xml"
#define MEDIA_SERVER_CONFIG_PATH "/usr/bin/media_server_cfg.json"
#define ISP_CONFIG_PATH "/usr/lib/medialib/sensors/imx678/default/default/isp_profiles/"

#define _DEFAULT "default/"
#define _DENOISE "denoise/"
#define _HDR "hdr/"
#define _BACKLIGHT "backlight/"

#define _3A_CONFIG "3aconfig.json"
#define _SONY_CONFIG "sony_imx678.xml"
#define _MEDIA_SERVER_CONFIG "media_server_cfg.json"

// 3A config
#define ISP_DEFAULT_3A_CONFIG (ISP_CONFIG_PATH _DEFAULT _3A_CONFIG)
#define ISP_DENOISE_3A_CONFIG (ISP_CONFIG_PATH _DENOISE _3A_CONFIG)
#define ISP_BACKLIGHT_3A_CONFIG (ISP_CONFIG_PATH _BACKLIGHT _3A_CONFIG)
#define ISP_HDR_3A_CONFIG_4K "/usr/bin/3aconfig_imx678_nnhdr.json"
#define ISP_HDR_3A_CONFIG_FHD "/usr/bin/3aconfig_imx678.json"

// Sony sensor config
#define ISP_DEFAULT_SONY_CONFIG (ISP_CONFIG_PATH _DEFAULT _SONY_CONFIG)
#define ISP_DENOISE_SONY_CONFIG (ISP_CONFIG_PATH _DENOISE _SONY_CONFIG)

// Media server config
#define ISP_DEFAULT_MEDIA_SERVER_CONFIG (ISP_CONFIG_PATH _DEFAULT _MEDIA_SERVER_CONFIG)
#define ISP_DENOISE_MEDIA_SERVER_CONFIG (ISP_CONFIG_PATH _DENOISE _MEDIA_SERVER_CONFIG)
#define ISP_SENSOR0_ENTRY_CONFIG "Sensor0_Entry.cfg"
#define ISP_SENSOR0_ENTRY_HDR_IMX678_CONFIG "/usr/bin/imx678hdr4k_Sensor0_Entry.cfg"
#define ISP_SENSOR0_ENTRY_IMX678_CONFIG "/usr/bin/imx678_Sensor0_Entry.cfg"
#define MEDIA_SERVER_HDR_CONFIG "/usr/bin/media_server_cfg_nnhdr.json"
#define MEDIA_SERVER_SDR_CONFIG "/usr/bin/media_server_cfg_default.json"
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
    extern bool m_auto_configure;
    void set_auto_configure(bool auto_configure);
    void set_isp_config_files_path(std::string &path);
    void override_file(const std::string &src, const std::string &dst);
    void set_default_configuration();
    void set_denoise_configuration();
    void set_backlight_configuration();
    void set_hdr_configuration(bool is_4k);
    std::string find_subdevice_path(const std::string &subdevice_name);
    void setup_hdr(bool is_4k, hdr_dol_t dol);
    void setup_sdr();
    void set_hdr_ratios(float ls_ratio, float vs_ratio);
} // namespace isp_utils

/** @} */ // end of isp_utils_definitions
