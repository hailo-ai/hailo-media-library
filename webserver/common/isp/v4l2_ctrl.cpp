#include "v4l2_ctrl.hpp"
#include <cstring>
#include <stdio.h>
#include <unordered_map>

using namespace webserver::common;

std::unordered_map<v4l2_ctrl_id, v4l2ControlHelper::min_max_isp_params> v4l2ControlHelper::m_min_max_isp_params = {
    {v4l2_ctrl_id::V4L2_CTRL_SHARPNESS_DOWN, {0, 65535, v4l2_ctrl_id::V4L2_CTRL_SHARPNESS_DOWN}},
    {v4l2_ctrl_id::V4L2_CTRL_SHARPNESS_UP, {0, 30000, v4l2_ctrl_id::V4L2_CTRL_SHARPNESS_UP}},
    {v4l2_ctrl_id::V4L2_CTRL_BRIGHTNESS, {-128, 127, v4l2_ctrl_id::V4L2_CTRL_BRIGHTNESS}},
    {v4l2_ctrl_id::V4L2_CTRL_CONTRAST, {30, 199, v4l2_ctrl_id::V4L2_CTRL_CONTRAST}},
    {v4l2_ctrl_id::V4L2_CTRL_SATURATION, {0, 199, v4l2_ctrl_id::V4L2_CTRL_SATURATION}},
    {v4l2_ctrl_id::V4L2_CTRL_WDR_CONTRAST, {-1023, 1023, v4l2_ctrl_id::V4L2_CTRL_WDR_CONTRAST}},
    {v4l2_ctrl_id::V4L2_CTRL_AE_GAIN, {0, 3890 * 1024, v4l2_ctrl_id::V4L2_CTRL_AE_GAIN}},
    {v4l2_ctrl_id::V4L2_CTRL_AE_INTEGRATION_TIME, {8, 33000, v4l2_ctrl_id::V4L2_CTRL_AE_INTEGRATION_TIME}},
    {v4l2_ctrl_id::V4L2_CTRL_AE_WDR_VALUES, {0, 255, v4l2_ctrl_id::V4L2_CTRL_AE_WDR_VALUES}},
};