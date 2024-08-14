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
#include <fstream>

/** @defgroup isp_utils_definitions MediaLibrary ISP utilities CPP API
 * definitions
 *  @{
 */

namespace isp_utils
{
    bool m_auto_configure = false;

    void set_auto_configure(bool auto_configure)
    {
        m_auto_configure = auto_configure;
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

    void setup_hdr(bool is_4k)
    {
        LOGGER__DEBUG("Setting up HDR configuration");
        if (is_4k)
        {
            override_file(MEDIA_SERVER_HDR_CONFIG, MEDIA_SERVER_CONFIG);
            override_file(ISP_SENSOR0_ENTRY_HDR_IMX678_CONFIG, ISP_SENSOR0_ENTRY_CONFIG);
            if (m_auto_configure)
                override_file(ISP_HDR_3A_CONFIG_4K, TRIPLE_A_CONFIG_PATH);
        }
        else
        {
            override_file(ISP_SENSOR0_ENTRY_IMX678_CONFIG, ISP_SENSOR0_ENTRY_CONFIG);
            if (m_auto_configure)
                override_file(ISP_HDR_3A_CONFIG_FHD, TRIPLE_A_CONFIG_PATH);
        }

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
        if (std::filesystem::exists(MEDIA_SERVER_SDR_CONFIG))
        {
            override_file(MEDIA_SERVER_SDR_CONFIG, MEDIA_SERVER_CONFIG);
        }

        override_file(ISP_SENSOR0_ENTRY_IMX678_CONFIG, ISP_SENSOR0_ENTRY_CONFIG);
        if (m_auto_configure)
            override_file("/usr/bin/3aconfig_imx678.json", TRIPLE_A_CONFIG_PATH);

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