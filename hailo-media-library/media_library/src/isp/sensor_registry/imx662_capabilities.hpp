#pragma once
#include "sensor_types.hpp"
#include <linux/videodev2.h>

namespace sensor_config
{
namespace imx662
{

inline const SensorCapabilities capabilities{.sensor_name = "IMX662",
                                             .sub_dev_prefix = "imx662",
                                             .supported_resolutions = {Resolution::FHD},
                                             .pixel_format = V4L2_PIX_FMT_SRGGB12,

                                             .mode_mappings = {
                                                 // FHD SDR
                                                 {SensorModeKey(Resolution::FHD),
                                                  SensorModeInfo{
                                                      .sensor_mode = 0,
                                                      .csi_mode = CSI_MODE_SDR,
                                                  }},

                                                 // FHD HDR 2DOL
                                                 {SensorModeKey(Resolution::FHD, HDR_DOL_2),
                                                  SensorModeInfo{
                                                      .sensor_mode = 1,
                                                      .csi_mode = CSI_MODE_DEFAULT_HDR,
                                                  }},
                                             }};

} // namespace imx662
} // namespace sensor_config
