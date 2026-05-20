#pragma once

#include <unordered_map>
#include <cstdint>
#include <cstring>

#include <linux/videodev2.h>
#include <sys/types.h>

namespace v4l2_ctrl
{

int xioctl(unsigned long request, void *arg);
uint get_ctrl_id(const char *v4l2_ctrl_name);

struct min_max_isp_params_t
{
    int32_t min;
    int32_t max;
};

enum class Video0Ctrl
{
    POWERLINE_FREQUENCY,
    NOISE_REDUCTION,
    SHARPNESS_DOWN,
    SHARPNESS_UP,
    BRIGHTNESS,
    CONTRAST,
    SATURATION,
    EE_ENABLE,

    AE_ENABLE,
    AE_GAIN,
    AE_INTEGRATION_TIME,
    AE_CONVERGED,
    AE_WDR_VALUES,

    WDR_CONTRAST,

    AWB_MODE,
    AWB_ILLUM_INDEX,

    HDR_RATIOS,

    MAX
};
static const std::unordered_map<Video0Ctrl, const char *> m_ctrl_id_to_name = {
    {Video0Ctrl::POWERLINE_FREQUENCY, "isp_ae_flicker_period"},
    {Video0Ctrl::SHARPNESS_DOWN, "isp_ee_y_gain_down"},
    {Video0Ctrl::SHARPNESS_UP, "isp_ee_y_gain_up"},
    {Video0Ctrl::BRIGHTNESS, "isp_cproc_brightness"},
    {Video0Ctrl::CONTRAST, "isp_cproc_contrast"},
    {Video0Ctrl::SATURATION, "isp_cproc_saturation"},
    {Video0Ctrl::EE_ENABLE, "isp_ee_enable"},
    {Video0Ctrl::AE_ENABLE, "isp_ae_enable"},
    {Video0Ctrl::AE_GAIN, "isp_ae_gain"},
    {Video0Ctrl::AE_INTEGRATION_TIME,
     "isp_ae_integration_time"}, // inside "classname": "AdaptiveAe" in "priority": [[[3.8e-05,0.0035],
    {Video0Ctrl::AE_CONVERGED, "isp_ae_converged"},
    {Video0Ctrl::WDR_CONTRAST, "isp_wdr_contrast"},
    {Video0Ctrl::AWB_MODE, "isp_awb_mode"},
    {Video0Ctrl::AWB_ILLUM_INDEX, "isp_awb_illum_index"},
    {Video0Ctrl::HDR_RATIOS, "isp_hdr_ratio"}};

static const std::unordered_map<Video0Ctrl, min_max_isp_params_t> min_max_isp_params = {
    {Video0Ctrl::SHARPNESS_DOWN, {0, 65535}},
    {Video0Ctrl::SHARPNESS_UP, {0, 30000}},
    {Video0Ctrl::BRIGHTNESS, {-128, 127}},
    {Video0Ctrl::CONTRAST, {30, 199}},
    {Video0Ctrl::SATURATION, {0, 199}},
    {Video0Ctrl::WDR_CONTRAST, {-1023, 1023}},
    {Video0Ctrl::AE_GAIN, {0, 3890 * 1024}},
    {Video0Ctrl::AE_INTEGRATION_TIME, {8, 33000}}, // TODO revert to 33000 when merging //TODO pull this from 3aconfig
    {Video0Ctrl::AE_WDR_VALUES, {0, 255}}};

template <typename T> inline void ioctl_clear(T &v4l2_ctrl)
{
    std::memset(&v4l2_ctrl, 0, sizeof(T));
}

template <typename T> bool set(Video0Ctrl id, T val)
{
    struct v4l2_ext_control ctrl;
    struct v4l2_ext_controls ctrls;
    struct v4l2_query_ext_ctrl qctrl;
    ioctl_clear(ctrl);
    ioctl_clear(ctrls);
    ioctl_clear(qctrl);

    qctrl.id = static_cast<uint32_t>(get_ctrl_id(m_ctrl_id_to_name.at(id)));
    if (xioctl(VIDIOC_QUERY_EXT_CTRL, &qctrl) == -1)
    {
        return false;
    }

    ctrl.id = qctrl.id;
    if constexpr (std::is_pointer_v<T>)
    {
        ctrl.size = sizeof(*val);
        ctrl.ptr = val;
    }
    else if constexpr (std::is_integral_v<T>)
    {
        ctrl.size = sizeof(val);
        ctrl.value = val;
    }
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    ctrls.which = V4L2_CTRL_ID2WHICH(ctrl.id);

    if (xioctl(VIDIOC_S_EXT_CTRLS, &ctrls) == -1)
    {
        return false;
    }

    return true;
}
template <typename T> bool get(Video0Ctrl id, T &val)
{
    struct v4l2_ext_control ctrl;
    struct v4l2_ext_controls ctrls;
    struct v4l2_query_ext_ctrl qctrl;
    ioctl_clear(ctrl);
    ioctl_clear(ctrls);
    ioctl_clear(qctrl);

    qctrl.id = static_cast<uint32_t>(get_ctrl_id(m_ctrl_id_to_name.at(id)));
    if (xioctl(VIDIOC_QUERY_EXT_CTRL, &qctrl) == -1)
    {
        return false;
    }
    ctrl.id = qctrl.id;
    if constexpr (std::is_pointer_v<T>)
    {
        ctrl.size = qctrl.elem_size * qctrl.elems;
        ctrl.ptr = val;
    }
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    ctrls.which = V4L2_CTRL_ID2WHICH(ctrl.id);

    if (xioctl(VIDIOC_G_EXT_CTRLS, &ctrls) == -1)
    {
        return false;
    }
    if constexpr (std::is_integral_v<T>)
    {
        val = ctrl.value;
    }
    return true;
}

template <typename T> static T calculate_value_from_precentage(uint16_t precentage, Video0Ctrl ctrl_id, T calib_var)
{
    auto min_max_params = min_max_isp_params.at(ctrl_id);
    float val_after_formula;
    if (precentage >= 50)
    {
        val_after_formula = (precentage - 50) / 50.0f * (min_max_params.max - calib_var) + calib_var;
    }
    else
    {
        val_after_formula = (50 - precentage) / 50.0f * (min_max_params.min - calib_var) + calib_var;
    }

    return static_cast<T>(val_after_formula);
}

template <typename T> static uint16_t calculate_precentage_from_value(T value, Video0Ctrl ctrl_id, T calib_var)
{
    auto min_max_params = min_max_isp_params.at(ctrl_id);
    float precentage = 50;
    if (value > calib_var)
    {
        precentage += ((value - calib_var) / static_cast<float>(min_max_params.max - calib_var) * 50.0f);
    }
    else
    {
        precentage -= ((calib_var - value) / static_cast<float>(calib_var - min_max_params.min) * 50.0f);
    }

    return static_cast<uint16_t>(precentage);
}
} // namespace v4l2_ctrl
