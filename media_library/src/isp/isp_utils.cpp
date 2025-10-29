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
#include "isp_utils.hpp"
#include "logger_macros.hpp"
#include "sensor_registry.hpp"
#include "media_library_types.hpp"
#include "v4l2_ctrl.hpp"
#include "media_library_logger.hpp"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <fstream>
#include <optional>
#include <regex>
#include <tl/expected.hpp>

#define MODULE_NAME LoggerType::Isp

/** @defgroup isp_utils_definitions MediaLibrary ISP utilities CPP API
 * definitions
 *  @{
 */

#define MEDIA_SERVER_VSM_ENTRY "vsm"
#define MEDIA_SERVER_VSM_H_OFFSET_ENTRY "vsm_h_offset"
#define MEDIA_SERVER_VSM_V_OFFSET_ENTRY "vsm_v_offset"
#define MEDIA_SERVER_AWB_ENTRY "awb"
#define MEDIA_SERVER_AWB_STITCH_MODE_ENTRY "stitch_mode"
#define MEDIA_SERVER_DGAIN_ENTRY "dgain"
#define MEDIA_SERVER_DGAIN_DUMMY_ENTRY "dummy"
#define MEDIA_SERVER_BLS_ENTRY "bls"
#define MEDIA_SERVER_BLS_DUMMY_ENTRY "dummy"
#define SENSOR_ENTRY_HDR_ENABLE_ENTRY "hdr_enable"
#define SENSOR_ENTRY_MODE_ENTRY "mode"

static constexpr int SDR_STITCH_MODE = 0;

#define REGEX_INTEGER "\\d+"
#define REGEX_XML_FILENAME "\\w+\\.xml"
#define HDR_ENABLE_REGEX "hdr_enable = " REGEX_INTEGER
#define MODE_REGEX "(^|\n)mode = " REGEX_INTEGER
#define MODE_XML_REGEX(MODE) "(\\[mode\\." + std::to_string(MODE) + "\\]\\nxml = \")(" REGEX_XML_FILENAME ")"

using json = nlohmann::json;

namespace isp_utils
{

std::string m_isp_config_files_path;

std::optional<SensorType> get_sensor_type(size_t sensor_index)
{
    auto &registry = SensorRegistry::get_instance();
    return registry.detect_sensor_type(sensor_index);
}

void set_isp_config_files_path(std::string &isp_config_files_path)
{
    m_isp_config_files_path = isp_config_files_path;
}

media_library_return edit_media_server_cfg(const std::string &path, const int stitch_mode,
                                           const output_resolution_t &input_resolution)
{
    auto &registry = SensorRegistry::get_instance();
    auto resolution = registry.detect_resolution(input_resolution);
    if (!resolution)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Unsupported resolution: {}x{}",
                              input_resolution.dimensions.destination_width,
                              input_resolution.dimensions.destination_height);
        return MEDIA_LIBRARY_ERROR;
    }

    auto resolution_info = registry.get_resolution_info(resolution.value());
    if (!resolution_info)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get resolution info");
        return MEDIA_LIBRARY_ERROR;
    }

    std::ifstream if_cfg(path.c_str());
    if (!if_cfg.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "HDR: can't open {} for reading", path);
        return MEDIA_LIBRARY_ERROR;
    }

    json cfg = json::parse(if_cfg);

    cfg[MEDIA_SERVER_AWB_ENTRY][MEDIA_SERVER_AWB_STITCH_MODE_ENTRY] = stitch_mode;
    cfg[MEDIA_SERVER_VSM_ENTRY][MEDIA_SERVER_VSM_H_OFFSET_ENTRY] = resolution_info->vsm_offsets.h_offset;
    cfg[MEDIA_SERVER_VSM_ENTRY][MEDIA_SERVER_VSM_V_OFFSET_ENTRY] = resolution_info->vsm_offsets.v_offset;

    if_cfg.close();
    std::ofstream of_cfg(path.c_str(), std::ofstream::trunc);
    if (!of_cfg.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "HDR: can't open {} for writing", path);
        return MEDIA_LIBRARY_ERROR;
    }
    of_cfg << std::setw(4) << cfg << std::endl;
    of_cfg.close();

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return edit_media_server_pre_isp_denoise_cfg(const std::string &path, const bool mode)
{
    std::ifstream if_cfg(path.c_str());

    if (!if_cfg.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "ISP Utils: can't open {} for reading", path);
        return MEDIA_LIBRARY_ERROR;
    }

    json cfg = json::parse(if_cfg);

    cfg[MEDIA_SERVER_DGAIN_ENTRY][MEDIA_SERVER_DGAIN_DUMMY_ENTRY] = mode;
    cfg[MEDIA_SERVER_BLS_ENTRY][MEDIA_SERVER_BLS_DUMMY_ENTRY] = mode;

    if_cfg.close();
    std::ofstream of_cfg(path.c_str(), std::ofstream::trunc);
    if (!of_cfg.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "ISP Utils: can't open {} for writing", path);
        return MEDIA_LIBRARY_ERROR;
    }
    of_cfg << std::setw(4) << cfg << std::endl;
    of_cfg.close();

    return MEDIA_LIBRARY_SUCCESS;
}

inline std::string replace_by_regex(const std::string &file_content, const std::string &regex_pattern_find,
                                    const std::string &regex_pattern_replace, const std::string &replace_with)
{
    std::smatch find_result;
    if (!std::regex_search(file_content, find_result, std::regex(regex_pattern_find)))
    {
        return file_content;
    }

    std::string find_string = find_result.str();
    std::string replaced_find = std::regex_replace(find_string, std::regex(regex_pattern_replace), replace_with);

    return std::regex_replace(file_content, std::regex(regex_pattern_find), replaced_find);
}

std::string edit_sensor_entry_hdr_mode(const std::string &file_content, const hdr_config_t &hdr_config)
{
    const int hdr_mode = hdr_config.enabled ? 1 : 0;
    return replace_by_regex(file_content, HDR_ENABLE_REGEX, REGEX_INTEGER, std::to_string(hdr_mode));
}

media_library_return setup_hdr(const output_resolution_t &input_resolution, const hdr_config_t &hdr_config,
                               const int stitch_mode, std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager)
{
    auto &registry = SensorRegistry::get_instance();
    auto mode_info = registry.get_sensor_mode_info_hdr(input_resolution, hdr_config.dol);
    if (!mode_info)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get sensor mode info for HDR setup");
        return MEDIA_LIBRARY_ERROR;
    }

    if (MEDIA_LIBRARY_SUCCESS !=
        edit_media_server_cfg(m_isp_config_files_path + std::string("/") + std::string(_MEDIA_SERVER_CONFIG),
                              stitch_mode, input_resolution))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to edit media server config for HDR setup");
        return MEDIA_LIBRARY_ERROR;
    }

    if (!v4l2_ctrl_manager)
    {
        return MEDIA_LIBRARY_ERROR;
    }
    if (!v4l2_ctrl_manager->ext_ctrl_set(v4l2::ImxCtrl::IMX_WDR, true))
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Failed to set IMX_WDR");
    }
    if (!v4l2_ctrl_manager->ext_ctrl_set(v4l2::CsiCtrl::CSI_MODE_SEL, mode_info->csi_mode))
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Failed to set CSI_MODE_SEL");
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return setup_sdr(const output_resolution_t &input_resolution,
                               std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager, const bool dgain_mode)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Setting up SDR configuration");

    auto &registry = SensorRegistry::get_instance();
    auto mode_info = registry.get_sensor_mode_info_sdr(input_resolution);
    if (!mode_info)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get sensor mode info for HDR setup");
        return MEDIA_LIBRARY_ERROR;
    }

    if (MEDIA_LIBRARY_SUCCESS !=
        edit_media_server_cfg(m_isp_config_files_path + std::string("/") + std::string(_MEDIA_SERVER_CONFIG),
                              SDR_STITCH_MODE, input_resolution))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to edit media server config for SDR setup");
        return MEDIA_LIBRARY_ERROR;
    }

    if (MEDIA_LIBRARY_SUCCESS !=
        edit_media_server_pre_isp_denoise_cfg(
            m_isp_config_files_path + std::string("/") + std::string(_MEDIA_SERVER_CONFIG), dgain_mode))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to edit pre-ISP denoise config for SDR setup");
        return MEDIA_LIBRARY_ERROR;
    }

    if (!v4l2_ctrl_manager->ext_ctrl_set(v4l2::ImxCtrl::IMX_WDR, false))
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Failed to set IMX_WDR");
    }

    if (!v4l2_ctrl_manager->ext_ctrl_set(v4l2::CsiCtrl::CSI_MODE_SEL, mode_info->csi_mode))
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Failed to set CSI_MODE_SEL");
    }

    return MEDIA_LIBRARY_SUCCESS;
}

tl::expected<isp_hdr_sensor_params_t, media_library_return> get_hdr_isp_params(
    uint8_t num_exposures, uint64_t line_readout_time, uint64_t num_readout_lines,
    std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager, bool force_refresh)
{
    isp_hdr_sensor_params_t hdr_params;

    // get vmax
    if (!v4l2_ctrl_manager->get(v4l2::ImxCtrl::VERTICAL_SPAN, hdr_params.vmax, force_refresh))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get vmax");
        return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }

    hdr_params.vmax *= line_readout_time;

    // get hmax
    if (!v4l2_ctrl_manager->get(v4l2::ImxCtrl::HORIZONTAL_SPAN, hdr_params.hmax, force_refresh))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get hmax");
        return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }

    hdr_params.hmax *= line_readout_time;
    uint64_t val;
    // calculate long readout time
    hdr_params.rhs_times.push_back(num_readout_lines * line_readout_time * (num_exposures == 1 ? 1 : 2));
    // get long exposure time
    if (!v4l2_ctrl_manager->get(v4l2::ImxCtrl::SHUTTER_TIMING_LONG, val, force_refresh))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get long exposure time");
        return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }
    hdr_params.shr_times.push_back(val * line_readout_time);
    if (!(--num_exposures)) // only one exposure
    {
        return hdr_params;
    }

    // get short readout time
    if (!v4l2_ctrl_manager->get(v4l2::ImxCtrl::READOUT_TIMING_SHORT, val, force_refresh))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get readout short time");
        tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }
    hdr_params.rhs_times.push_back(val * line_readout_time);
    // get short exposure time
    if (!v4l2_ctrl_manager->get(v4l2::ImxCtrl::SHUTTER_TIMING_SHORT, val, force_refresh))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get short exposure time");
        tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }
    hdr_params.shr_times.push_back(val * line_readout_time);
    if (!(--num_exposures))
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "ISP utils got readout times: {}, {}", hdr_params.rhs_times[0],
                             hdr_params.rhs_times[1]);
        return hdr_params;
    }

    // get very short readout time
    if (!v4l2_ctrl_manager->get(v4l2::ImxCtrl::READOUT_TIMING_VERY_SHORT, val, force_refresh))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get readout very short time");
        tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }
    hdr_params.rhs_times.push_back(val * line_readout_time);
    // get very short exposure time
    if (!v4l2_ctrl_manager->get(v4l2::ImxCtrl::SHUTTER_TIMING_VERY_SHORT, val, force_refresh))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get very short exposure time");
        tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }
    hdr_params.shr_times.push_back(val * line_readout_time);
    LOGGER__MODULE__INFO(MODULE_NAME, "ISP utils got readout times: {}, {}, {}", hdr_params.rhs_times[0],
                         hdr_params.rhs_times[1], hdr_params.rhs_times[2]);
    return hdr_params;
}

bool set_isp_mcm_mode(uint32_t target_mcm_mode, std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager)
{
    if (!v4l2_ctrl_manager->ext_ctrl_set(v4l2::IspCtrl::MCM_MODE_SEL, target_mcm_mode))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set MCM_MODE_SEL to {}", target_mcm_mode);
        return false;
    }
    return true;
}
} // namespace isp_utils

/** @} */ // end of isp_utils_definitions
