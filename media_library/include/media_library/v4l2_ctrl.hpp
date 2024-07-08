#pragma once

#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <thread>
#include <linux/videodev2.h>
#include <unordered_map>
#include <iostream>
#include <cstring>

namespace isp_utils
{
    namespace ctrl
    {
        enum v4l2_ctrl_id
        {
            V4L2_CTRL_POWERLINE_FREQUENCY = 1,
            V4L2_CTRL_NOISE_REDUCTION = 2,
            V4L2_CTRL_SHARPNESS_DOWN = 3,
            V4L2_CTRL_SHARPNESS_UP = 4,
            V4L2_CTRL_BRIGHTNESS = 5,
            V4L2_CTRL_CONTRAST = 6,
            V4L2_CTRL_SATURATION = 7,
            V4L2_CTRL_EE_ENABLE = 8,

            V4L2_CTRL_AE_ENABLE = 9,
            V4L2_CTRL_AE_GAIN = 10,
            V4L2_CTRL_AE_INTEGRATION_TIME = 11,
            V4L2_CTRL_AE_WDR_VALUES = 15,

            V4L2_CTRL_WDR_CONTRAST = 12,

            V4L2_CTRL_AWB_MODE = 13,
            V4L2_CTRL_AWB_ILLUM_INDEX = 14,

            V4l2_CTRL_CSI_MODE_SEL = 16,
            V4l2_CTRL_IMX_WDR = 17,

            V4l2_CTRL_SET_HDR_RATIOS = 18,

            V4L2_CTRL_MAX
        };

        class v4l2Control
        {
        private:
            std::string m_device;
            int m_fd;
            std::unordered_map<v4l2_ctrl_id, uint> m_ctrl_id_to_id;
            uint get_id(v4l2_ctrl_id id);
            int xioctl(int request, void *arg);
            uint v4l2_get_ctrl_id(const std::string &v4l2_ctrl_name);

        public:
            static std::unordered_map<v4l2_ctrl_id, std::string> m_ctrl_id_to_name;
            v4l2Control(std::string device);

            template <typename T>
            bool v4l2_ctrl_set(v4l2_ctrl_id id, T val);
            template <typename T>
            bool v4l2_ctrl_get(v4l2_ctrl_id id, T &val);
            template <typename T>
            bool v4l2_ext_ctrl_set(v4l2_ctrl_id id, T val);
            template <typename T>
            bool v4l2_ext_ctrl_set_array(v4l2_ctrl_id id, T val, size_t size);
            template <typename T>
            bool v4l2_ext_ctrl_set2(v4l2_ctrl_id id, T &val);
            template <typename T>
            bool v4l2_ext_ctrl_get(v4l2_ctrl_id id, T &val);
        };
    }
}