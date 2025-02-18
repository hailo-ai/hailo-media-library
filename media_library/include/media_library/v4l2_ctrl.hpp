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
    V4L2_CTRL_POWERLINE_FREQUENCY,
    V4L2_CTRL_NOISE_REDUCTION,
    V4L2_CTRL_SHARPNESS_DOWN,
    V4L2_CTRL_SHARPNESS_UP,
    V4L2_CTRL_BRIGHTNESS,
    V4L2_CTRL_CONTRAST,
    V4L2_CTRL_SATURATION,
    V4L2_CTRL_EE_ENABLE,

    V4L2_CTRL_AE_ENABLE,
    V4L2_CTRL_AE_GAIN,
    V4L2_CTRL_AE_INTEGRATION_TIME,
    V4L2_CTRL_AE_WDR_VALUES,

    V4L2_CTRL_WDR_CONTRAST,

    V4L2_CTRL_AWB_MODE,
    V4L2_CTRL_AWB_ILLUM_INDEX,

    V4l2_CTRL_CSI_MODE_SEL,
    V4l2_CTRL_IMX_WDR,

    V4l2_CTRL_SET_HDR_RATIOS,

    V4L2_CTRL_MAX
};

class v4l2Control
{
  private:
    std::string m_device;
    int m_fd;
    std::unordered_map<v4l2_ctrl_id, uint> m_ctrl_id_to_id;
    uint get_id(v4l2_ctrl_id id);
    int xioctl(unsigned long request, void *arg);
    uint v4l2_get_ctrl_id(const std::string &v4l2_ctrl_name);

  public:
    static std::unordered_map<v4l2_ctrl_id, std::string> m_ctrl_id_to_name;
    v4l2Control(std::string device);
    ~v4l2Control();

    template <typename T> bool v4l2_ctrl_set(v4l2_ctrl_id id, T val);
    template <typename T> bool v4l2_ctrl_get(v4l2_ctrl_id id, T &val);
    template <typename T> bool v4l2_ext_ctrl_set(v4l2_ctrl_id id, T val);
    template <typename T> bool v4l2_ext_ctrl_set_array(v4l2_ctrl_id id, T val);
    template <typename T> bool v4l2_ext_ctrl_set2(v4l2_ctrl_id id, T &val);
    template <typename T> bool v4l2_ext_ctrl_get(v4l2_ctrl_id id, T &val);
    template <typename T> bool v4l2_ioctl_set(unsigned long request, T &val);
};
} // namespace ctrl
} // namespace isp_utils
