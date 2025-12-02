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

#include <tl/expected.hpp>
#include "media_library_types.hpp"
#include "sensor_types.hpp"
#include "v4l2_ctrl.hpp"

#define _MEDIA_SERVER_CONFIG "media_server_cfg.json"

/** @defgroup isp_utils_definitions MediaLibrary ISP utilities CPP API
 * definitions
 *  @{
 */

/**
  ISP interfaces and utilities
*/
namespace isp_utils
{

enum isp_mcm_mode
{
    ISP_MCM_MODE_OFF = 0,
    ISP_MCM_MODE_STITCHING = 1,    // default mode for MCM
    ISP_MCM_MODE_INJECTION = 2,    // read raw and write back to MCM, 16 bit
    ISP_MCM_MODE_PACKED = 3,       // read raw and write back to MCM, 12 bit
    ISP_MCM_MODE_MULTI_SENSOR = 4, // read raw from multiple sensors and write back to MCM, 12 bit
    ISP_MCM_MODE_MAX
};

typedef struct
{
    std::vector<uint64_t> rhs_times;
    std::vector<uint64_t> shr_times;
    uint64_t vmax;
    uint64_t hmax;
} isp_hdr_sensor_params_t;

void set_isp_config_files_path(std::string &path);
std::optional<SensorType> get_sensor_type(size_t sensor_index = 0);
media_library_return setup_hdr(const output_resolution_t &input_resolution, const hdr_config_t &hdr_config,
                               const int stitch_mode, std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager);
media_library_return setup_sdr(const output_resolution_t &input_resolution,
                               std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager,
                               const bool dgain_mode = false);
/* When HDR is on, set the offeset to be the time of the frame capture in the sensor, so NNC timings will not effect it
 */
void set_hdr_forward_timestamp(bool enabled);
tl::expected<isp_hdr_sensor_params_t, media_library_return> get_hdr_isp_params(
    uint8_t num_exposures, uint64_t line_readout_time, uint64_t num_readout_lines,
    std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager, bool force_refresh = false);
bool set_isp_mcm_mode(uint32_t target_mcm_mode, std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager);
} // namespace isp_utils

/** @} */ // end of isp_utils_definitions
