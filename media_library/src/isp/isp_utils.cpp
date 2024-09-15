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
#include <nlohmann/json.hpp>
#include <fstream>
#include <regex>

/** @defgroup isp_utils_definitions MediaLibrary ISP utilities CPP API
 * definitions
 *  @{
 */

#define MEDIA_SERVER_AWB_ENTRY "awb"
#define MEDIA_SERVER_AWB_STITCH_MODE_ENTRY "stitch_mode"
#define MEDIA_SERVER_STITCH_MODE_COMPENSATION 2
#define MEDIA_SERVER_STITCH_MODE_ISP_PIPELINE 0
#define SENSOR_ENTRY_HDR_ENABLE_ENTRY "hdr_enable"
#define SENSOR_ENTRY_MODE_ENTRY "mode"
#define SENSOR_ENTRY_HDR_4K_MODE "3"
#define SENSOR_ENTRY_HDR_FHD_MODE "2"
#define SENSOR_ENTRY_SDR_4K_MODE "0"
#define SENSOR_ENTRY_SDR_FHD_MODE "1"
#define HDR_ENABLE_REGEX "hdr_enable = \\d+$"
#define MODE_REGEX "mode = \\d+$"

using json = nlohmann::json;

namespace isp_utils
{
    bool m_auto_configure = false;
    std::string m_isp_config_files_path;

    void set_auto_configure(bool auto_configure)
    {
        m_auto_configure = auto_configure;
    }

    void set_isp_config_files_path(std::string &isp_config_files_path)
    {
        m_isp_config_files_path = isp_config_files_path;
    }

    std::string find_subdevice_path(const std::string &subdevice_name)
    {
        for (const auto &entry : std::filesystem::directory_iterator("/sys/class/video4linux/"))
        {
            if (entry.path().filename().string().find("v4l-subdev") != std::string::npos)
            {
                std::ifstream name_file(entry.path() / "name");
                std::string name;
                name_file >> name;
                if (name.find(subdevice_name) != std::string::npos)
                {
                    return "/dev/" + entry.path().filename().string();
                }
            }
        }
        return "";
    }

    void override_file(const std::string &src, const std::string &dst)
    {
        LOGGER__DEBUG("ISP config overriding file {} to {}", src, dst);
        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    }

    void set_default_configuration()
    {
        if (m_auto_configure)
            override_file(ISP_DEFAULT_3A_CONFIG, TRIPLE_A_CONFIG_PATH);
    }

    void set_denoise_configuration()
    {
        if (m_auto_configure)
            override_file(ISP_DENOISE_3A_CONFIG, TRIPLE_A_CONFIG_PATH);
    }

    void set_backlight_configuration()
    {
        if (m_auto_configure)
            override_file(ISP_BACKLIGHT_3A_CONFIG, TRIPLE_A_CONFIG_PATH);
    }

    void set_hdr_configuration(bool is_4k)
    {
        if (m_auto_configure)
            override_file(is_4k ? ISP_HDR_3A_CONFIG_4K : ISP_HDR_3A_CONFIG_FHD, TRIPLE_A_CONFIG_PATH);
    }

    void edit_media_server_cfg(const std::string& path, bool hdr_enable)
    {
        std::ifstream if_cfg (path.c_str());

        if(!if_cfg.is_open()){
                std::cout << "HDR: can't open " << path << " for reading"<< std::endl;
                return;
        }

        json cfg = json::parse(if_cfg);
        cfg[MEDIA_SERVER_AWB_ENTRY][MEDIA_SERVER_AWB_STITCH_MODE_ENTRY] = hdr_enable ? MEDIA_SERVER_STITCH_MODE_COMPENSATION : MEDIA_SERVER_STITCH_MODE_ISP_PIPELINE;
        if_cfg.close();
        std::ofstream of_cfg(path.c_str(), std::ofstream::trunc);
        if(!of_cfg.is_open()){
                std::cout << "HDR: can't open " << path << " for writing" << std::endl;
                return;
        }
        of_cfg << std::setw(4) << cfg << std::endl;
        of_cfg.close();
    }

    void edit_sensor_entry(const std::string& path, bool hdr_enable, bool is_4k)
    {
        std::ifstream if_entry(path.c_str());

        if(!if_entry.is_open()){
                std::cout << "HDR: can't open " << path << " for reading"<< std::endl;
                return;
        }

        std::string line;
        std::string edited_file;
        const char* mode = "0";
        std::regex mode_regex (MODE_REGEX);
        std::regex hdr_enable_regex (HDR_ENABLE_REGEX);
        if(is_4k){
                mode = hdr_enable ? SENSOR_ENTRY_HDR_4K_MODE : SENSOR_ENTRY_SDR_4K_MODE; 
        }else{
                mode = hdr_enable ? SENSOR_ENTRY_HDR_FHD_MODE : SENSOR_ENTRY_SDR_FHD_MODE;
        }

        while(getline(if_entry, line)){
                /* Check that the line contains "hdr_enable" */
                if(std::regex_match(line, hdr_enable_regex)){
                        edited_file += std::string(SENSOR_ENTRY_HDR_ENABLE_ENTRY) + std::string(" = ") + std::string(hdr_enable ? "1" : "0");
                }

                /* Check that the lines contains "mode" but not "mode." */
                else if(std::regex_match(line, mode_regex)){
                        edited_file += std::string(SENSOR_ENTRY_MODE_ENTRY) + std::string(" = ") + std::string(mode);

                }else{
                        edited_file += line;
                }

                edited_file += std::string("\n");
        }

        if_entry.close();
        std::ofstream of_entry(path.c_str(), std::ofstream::trunc);

        if(!of_entry.is_open()){
                std::cout << "HDR: can't open " << path << " for writing" << std::endl;
                return;
        }

        of_entry << edited_file << std::endl;
        of_entry.close();
    }

    void setup_hdr(bool is_4k)
    {
        LOGGER__DEBUG("Setting up HDR configuration");
        edit_media_server_cfg(m_isp_config_files_path + std::string("/") + std::string(_MEDIA_SERVER_CONFIG), true);
        edit_sensor_entry(m_isp_config_files_path + std::string("/") + std::string(ISP_SENSOR0_ENTRY_CONFIG), true, is_4k);
        if (auto imx678_path = find_subdevice_path("imx678"); !imx678_path.empty())
        {
            isp_utils::ctrl::v4l2Control v4l2_ctrl(imx678_path);
            bool val = true;
            if (!v4l2_ctrl.v4l2_ext_ctrl_set(isp_utils::ctrl::V4l2_CTRL_IMX_WDR, val))
                LOGGER__WARN("Failed to set IMX_WDR for {} to {}", imx678_path, val);
        }
        else
        {
            LOGGER__DEBUG("Subdevice 'imx678' not found.");
        }

        if (auto csi_path = find_subdevice_path("csi"); !csi_path.empty())
        {
            auto mode_sel = is_4k ? 2 : 1;
            isp_utils::ctrl::v4l2Control v4l2_ctrl(csi_path);
            if (!v4l2_ctrl.v4l2_ext_ctrl_set(isp_utils::ctrl::V4l2_CTRL_CSI_MODE_SEL, mode_sel))
                LOGGER__WARN("Failed to set CSI_MODE_SEL for {} to {}", csi_path, mode_sel);
        }
        else
        {
            LOGGER__DEBUG("Subdevice 'csi' not found.");
        }
    }

    void setup_sdr()
    {
        LOGGER__DEBUG("Setting up SDR configuration");

        edit_media_server_cfg(m_isp_config_files_path + std::string("/") + std::string(_MEDIA_SERVER_CONFIG), false);
        edit_sensor_entry(m_isp_config_files_path + std::string("/") + std::string(ISP_SENSOR0_ENTRY_CONFIG), false, true);

        if (auto imx678_path = find_subdevice_path("imx678"); !imx678_path.empty())
        {
            isp_utils::ctrl::v4l2Control v4l2_ctrl(imx678_path);
            bool val = false;
            if (!v4l2_ctrl.v4l2_ext_ctrl_set(isp_utils::ctrl::V4l2_CTRL_IMX_WDR, val))
                LOGGER__WARN("Failed to set IMX_WDR for {} to {}", imx678_path, val);
        }
        else
        {
            LOGGER__DEBUG("Subdevice 'imx678' not found.");
        }

        if (auto csi_path = find_subdevice_path("csi"); !csi_path.empty())
        {
            isp_utils::ctrl::v4l2Control v4l2_ctrl(csi_path);
            if (!v4l2_ctrl.v4l2_ext_ctrl_set(isp_utils::ctrl::V4l2_CTRL_CSI_MODE_SEL, 0))
                LOGGER__WARN("Failed to set CSI_MODE_SEL for {} to 0", csi_path);
        }
        else
        {
            LOGGER__DEBUG("Subdevice 'csi' not found.");
        }
    }

    void set_hdr_ratios(float ls_ratio, float vs_ratio)
    {
        isp_utils::ctrl::v4l2Control v4l2_ctrl("/dev/video0");
        int ratios[] = {static_cast<int>(ls_ratio * (1 << 16)), static_cast<int>(vs_ratio * (1 << 16))};
        if (!v4l2_ctrl.v4l2_ext_ctrl_set_array(isp_utils::ctrl::V4l2_CTRL_SET_HDR_RATIOS, ratios, 2))
        {
            LOGGER__WARN("Failed to set HDR ratios to {} and {}", ls_ratio, vs_ratio);
        }
    }

} // namespace isp_utils

/** @} */ // end of isp_utils_definitions
