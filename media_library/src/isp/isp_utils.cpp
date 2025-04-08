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
#include "v4l2_ctrl.hpp"
#include "media_library_logger.hpp"
#include "common.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <regex>

#define MODULE_NAME LoggerType::Isp

/** @defgroup isp_utils_definitions MediaLibrary ISP utilities CPP API
 * definitions
 *  @{
 */

#define NUM_READOUT_LINES_4K 2160 // 4k only

#define MEDIA_SERVER_AWB_ENTRY "awb"
#define MEDIA_SERVER_AWB_STITCH_MODE_ENTRY "stitch_mode"
#define MEDIA_SERVER_STITCH_MODE_COMPENSATION 2
#define MEDIA_SERVER_STITCH_MODE_ISP_PIPELINE 0
#define SENSOR_ENTRY_HDR_ENABLE_ENTRY "hdr_enable"
#define SENSOR_ENTRY_MODE_ENTRY "mode"
#define SENSOR_ENTRY_HDR_4K_MODE_DOL2 4
#define SENSOR_ENTRY_HDR_4K_MODE_DOL3 3
#define SENSOR_ENTRY_HDR_FHD_MODE_DOL2 5
#define SENSOR_ENTRY_HDR_FHD_MODE_DOL3 2
#define SENSOR_ENTRY_SDR_4K_MODE 0
#define SENSOR_ENTRY_SDR_FHD_MODE 1

#define REGEX_INTEGER "\\d+"
#define REGEX_XML_FILENAME "\\w+\\.xml"
#define HDR_ENABLE_REGEX "hdr_enable = " REGEX_INTEGER
#define MODE_REGEX "(^|\n)mode = " REGEX_INTEGER
#define MODE_XML_REGEX(MODE) "(\\[mode\\." + std::to_string(MODE) + "\\]\\nxml = \")(" REGEX_XML_FILENAME ")"

using json = nlohmann::json;

namespace isp_utils
{

bool m_auto_configure = false;
std::string m_isp_config_files_path;

std::string find_sensor_name()
{
    for (const auto &entry : std::filesystem::directory_iterator("/sys/class/video4linux/"))
    {
        if (entry.path().filename().string().find("v4l-subdev") != std::string::npos)
        {
            std::ifstream name_file(entry.path() / "name");
            std::string name;
            name_file >> name;
            if (name.find("imx") != std::string::npos)
            {
                return name.substr(0, name.find(" "));
            }
        }
    }
    return "";
}

void set_auto_configure(bool auto_configure)
{
    m_auto_configure = auto_configure;
}

void set_isp_config_files_path(std::string &isp_config_files_path)
{
    m_isp_config_files_path = isp_config_files_path;
}

void override_file(const std::string &src, const std::string &dst)
{
    if (!std::filesystem::exists(src))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Source file {} does not exist", src);
        return;
    }
    if (!std::filesystem::exists(dst))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Destination file {} does not exist", dst);
        return;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "ISP config overriding file {} to {}", src, dst);
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
}

void set_daylight_configuration()
{
    if (!m_auto_configure)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Skipping set_daylight_configuration as auto_configure is disabled");
        return;
    }
    if (m_auto_configure)
    {
        override_file(ISP_DAYLIGHT_3A_CONFIG, TRIPLE_A_CONFIG_PATH);
        override_file(ISP_DAYLIGHT_SENSOR0_CONFIG, SENSOR0_ENTRY_CONFIG_PATH);
    }
}

void set_lowlight_configuration()
{
    if (!m_auto_configure)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Skipping set_lowlight_configuration as auto_configure is disabled");
        return;
    }
    if (m_auto_configure)
    {
        override_file(ISP_LOWLIGHT_3A_CONFIG, TRIPLE_A_CONFIG_PATH);
        override_file(ISP_LOWLIGHT_SENSOR0_CONFIG, SENSOR0_ENTRY_CONFIG_PATH);
    }
}

void set_hdr_configuration()
{
    if (!m_auto_configure)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Skipping set_hdr_configuration as auto_configure is disabled");
        return;
    }
    if (m_auto_configure)
    {
        override_file(ISP_HDR_2DOL_3A_CONFIG, TRIPLE_A_CONFIG_PATH);
        override_file(ISP_HDR_2DOL_SENSOR0_CONFIG, SENSOR0_ENTRY_CONFIG_PATH);
    }
}

void edit_media_server_cfg(const std::string &path, bool hdr_enable)
{
    std::ifstream if_cfg(path.c_str());

    if (!if_cfg.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "HDR: can't open {} for reading", path);
        return;
    }

    json cfg = json::parse(if_cfg);
    cfg[MEDIA_SERVER_AWB_ENTRY][MEDIA_SERVER_AWB_STITCH_MODE_ENTRY] =
        hdr_enable ? MEDIA_SERVER_STITCH_MODE_COMPENSATION : MEDIA_SERVER_STITCH_MODE_ISP_PIPELINE;
    if_cfg.close();
    std::ofstream of_cfg(path.c_str(), std::ofstream::trunc);
    if (!of_cfg.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "HDR: can't open {} for writing", path);
        return;
    }
    of_cfg << std::setw(4) << cfg << std::endl;
    of_cfg.close();
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

inline int get_sensor_mode(bool hdr_enable, bool is_4k, hdr_dol_t dol)
{
    int mode;

    if (!hdr_enable)
    {
        mode = is_4k ? SENSOR_ENTRY_SDR_4K_MODE : SENSOR_ENTRY_SDR_FHD_MODE;
    }
    else
    {
        if (dol == HDR_DOL_2)
        {
            mode = is_4k ? SENSOR_ENTRY_HDR_4K_MODE_DOL2 : SENSOR_ENTRY_HDR_FHD_MODE_DOL2;
        }
        else
        {
            mode = is_4k ? SENSOR_ENTRY_HDR_4K_MODE_DOL3 : SENSOR_ENTRY_HDR_FHD_MODE_DOL3;
        }
    }

    return mode;
}

/**
 * @brief Updates sensor configuration entries to reflect HDR and resolution settings.
 *
 * This function opens and modifies a sensor configuration file at the specified path.
 * Based on HDR and resolution settings, it updates the sensor mode and HDR enable flag
 * values in the file.
 *
 * @param path Path to the sensor configuration file.
 * @param hdr_enable Boolean indicating whether HDR should be enabled.
 * @param is_4k Boolean indicating if the sensor is set to 4K resolution.
 *
 * @details
 * - Retrieves the sensor mode based on HDR and resolution settings.
 * - Uses regex to replace the `mode` value with the computed mode.
 * - Sets `hdr_enable` to 1 or 0, based on the `hdr_enable` parameter.
 * - If HDR is enabled, it ensures that mode-specific XML file settings are updated
 *   to correspond with the new mode.
 * - Logs warnings if the file or specific entries cannot be located.
 */
void edit_sensor_entry(const std::string &path, bool hdr_enable, bool is_4k, hdr_dol_t dol = HDR_DOL_2)
{
    if (!m_auto_configure)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Skipping edit_sensor_entry as auto_configure is disabled");
        return;
    }
    std::ifstream if_entry(path.c_str());
    if (!if_entry.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "HDR: can't open {} for reading", path);
        return;
    }

    std::string file_content = std::string(std::istreambuf_iterator<char>(if_entry), std::istreambuf_iterator<char>());
    if_entry.close();

    // Replace Sensor Mode
    int mode = get_sensor_mode(hdr_enable, is_4k, dol);

    file_content = replace_by_regex(file_content, MODE_REGEX, REGEX_INTEGER, std::to_string(mode));

    // Replace HDR Enable
    file_content = replace_by_regex(file_content, HDR_ENABLE_REGEX, REGEX_INTEGER, hdr_enable ? "1" : "0");

    // Replace Mode 3 XML with Mode 0 XML
    if (hdr_enable)
    {
        std::smatch mode_sdr_xml_match;
        if (!std::regex_search(
                file_content, mode_sdr_xml_match,
                std::regex(MODE_XML_REGEX(is_4k ? SENSOR_ENTRY_SDR_4K_MODE : SENSOR_ENTRY_SDR_FHD_MODE))))
        {
            LOGGER__MODULE__WARN(MODULE_NAME, "HDR: can't find mode 0 xml in {} mode 3 xml will need to change", path);
        }
        else
        {
            std::string mode_sdr_xml_str = mode_sdr_xml_match[2].str(); // the xml name is in the 2nd regex group
            file_content = replace_by_regex(file_content, MODE_XML_REGEX(mode), REGEX_XML_FILENAME, mode_sdr_xml_str);
        }
    }

    std::ofstream of_entry(path.c_str(), std::ofstream::trunc);
    if (!of_entry.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "HDR: can't open {} for writing", path);
        return;
    }
    of_entry << file_content;
    of_entry.close();
}

void setup_hdr(bool is_4k, hdr_dol_t dol)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Setting up HDR configuration");
    edit_media_server_cfg(m_isp_config_files_path + std::string("/") + std::string(_MEDIA_SERVER_CONFIG), true);
    if (m_auto_configure)
    {
        edit_sensor_entry(m_isp_config_files_path + std::string("/") + std::string(ISP_SENSOR0_ENTRY_CONFIG), true,
                          is_4k, dol);
    }
    if (!v4l2::ext_ctrl_set(v4l2::ImxCtrl::IMX_WDR, true))
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Failed to set IMX_WDR");
    }

    auto mode_sel = is_4k ? 2 : 1;
    if (!v4l2::ext_ctrl_set(v4l2::CsiCtrl::CSI_MODE_SEL, mode_sel))
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Failed to set CSI_MODE_SEL");
    }
}

void setup_sdr(bool is_4k)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Setting up SDR configuration");
    edit_media_server_cfg(m_isp_config_files_path + std::string("/") + std::string(_MEDIA_SERVER_CONFIG), false);
    if (m_auto_configure)
    {

        edit_sensor_entry(m_isp_config_files_path + std::string("/") + std::string(ISP_SENSOR0_ENTRY_CONFIG), false,
                          is_4k);
    }

    if (!v4l2::ext_ctrl_set(v4l2::ImxCtrl::IMX_WDR, false))
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Failed to set IMX_WDR");
    }

    if (!v4l2::ext_ctrl_set(v4l2::CsiCtrl::CSI_MODE_SEL, 0))
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Failed to set CSI_MODE_SEL");
    }
}

void set_hdr_ratios(float ls_ratio, float vs_ratio)
{
    int ratios[] = {static_cast<int>(ls_ratio * (1 << 16)), static_cast<int>(vs_ratio * (1 << 16))};
    if (!v4l2::ext_ctrl_set(v4l2::Video0Ctrl::HDR_RATIOS, ratios))
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Failed to set HDR ratios to {} and {}", ls_ratio, vs_ratio);
    }
}

void set_hdr_forward_timestamp(bool enabled)
{
    auto fd_opt = v4l2::get_device_fd(v4l2::Device::VIDEO0);
    if (!fd_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "set_hdr_forward_timestamp Failed to get device fd");
        return;
    }
    auto fd = fd_opt.value();

    auto ioctl_id_opt = v4l2::get_ctrl_id(fd->get_fd(), v4l2::Video0Ctrl::HDR_FORWARD_TIMESTAMPS);
    if (!ioctl_id_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "set_hdr_forward_timestamp Failed to get ioctl id");
        return;
    }
    uint32_t ioctl_id = ioctl_id_opt.value();

    if (ioctl(fd->get_fd(), ioctl_id, &enabled) < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "set_hdr_forward_timestamp ioctl failed: {}", strerror(errno));
    }
    else
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "set_hdr_forward_timestamp to {} success", enabled);
    }
}

tl::expected<isp_hdr_sensor_params_t, media_library_return> get_hdr_isp_params(
    uint8_t num_exposures, uint64_t line_readout_time, std::shared_ptr<v4l2::v4l2ControlRepository> v4l2_ctrl_repo,
    bool force_refresh)
{
    isp_hdr_sensor_params_t hdr_params;

    // get vmax
    if (!v4l2_ctrl_repo->get(v4l2::ImxCtrl::VERTICAL_SPAN, hdr_params.vmax, force_refresh))
    {
        LOGGER__ERROR("Failed to get vmax");
        return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }

    hdr_params.vmax *= line_readout_time;

    // get hmax
    if (!v4l2_ctrl_repo->get(v4l2::ImxCtrl::HORIZONTAL_SPAN, hdr_params.hmax, force_refresh))
    {
        LOGGER__ERROR("Failed to get hmax");
        return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }

    hdr_params.hmax *= line_readout_time;
    uint64_t val;
    // calculate long readout time
    hdr_params.rhs_times.push_back(NUM_READOUT_LINES_4K * line_readout_time * (num_exposures == 1 ? 1 : 2));
    // get long exposure time
    if (!v4l2_ctrl_repo->get(v4l2::ImxCtrl::SHUTTER_TIMING_LONG, val, force_refresh))
    {
        LOGGER__ERROR("Failed to get long exposure time");
        return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }
    hdr_params.shr_times.push_back(val * line_readout_time);
    if (!(--num_exposures)) // only one exposure
    {
        return hdr_params;
    }

    // get short readout time
    if (!v4l2_ctrl_repo->get(v4l2::ImxCtrl::READOUT_TIMING_SHORT, val, force_refresh))
    {
        LOGGER__ERROR("Failed to get readout short time");
        tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }
    hdr_params.rhs_times.push_back(val * line_readout_time);
    // get short exposure time
    if (!v4l2_ctrl_repo->get(v4l2::ImxCtrl::SHUTTER_TIMING_SHORT, val, force_refresh))
    {
        LOGGER__ERROR("Failed to get short exposure time");
        tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }
    hdr_params.shr_times.push_back(val * line_readout_time);
    if (!(--num_exposures))
    {
        LOGGER__INFO("ISP utils got readout times: {}, {}", hdr_params.rhs_times[0], hdr_params.rhs_times[1]);
        return hdr_params;
    }

    // get very short readout time
    if (!v4l2_ctrl_repo->get(v4l2::ImxCtrl::READOUT_TIMING_VERY_SHORT, val, force_refresh))
    {
        LOGGER__ERROR("Failed to get readout very short time");
        tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }
    hdr_params.rhs_times.push_back(val * line_readout_time);
    // get very short exposure time
    if (!v4l2_ctrl_repo->get(v4l2::ImxCtrl::SHUTTER_TIMING_VERY_SHORT, val, force_refresh))
    {
        LOGGER__ERROR("Failed to get very short exposure time");
        tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }
    hdr_params.shr_times.push_back(val * line_readout_time);
    LOGGER__INFO("ISP utils got readout times: {}, {}, {}", hdr_params.rhs_times[0], hdr_params.rhs_times[1],
                 hdr_params.rhs_times[2]);
    return hdr_params;
}
} // namespace isp_utils

/** @} */ // end of isp_utils_definitions
