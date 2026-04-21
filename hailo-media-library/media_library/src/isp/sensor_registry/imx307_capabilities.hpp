#pragma once
#include "sensor_types.hpp"
#include <linux/videodev2.h>

namespace sensor_config
{
namespace imx307
{

inline const SensorCapabilities capabilities{.sensor_name = "IMX307",
                                             .sub_dev_prefix = "imx307",
                                             .supported_resolutions = {Resolution::FHD},
                                             .pixel_format = V4L2_PIX_FMT_SRGGB12,

                                             .mode_mappings = {
                                                 // FHD SDR
                                                 {SensorModeKey(Resolution::FHD),
                                                  SensorModeInfo{
                                                      .sensor_mode = 1,
                                                      .csi_mode = CSI_MODE_SDR,
                                                  }},
                                             }};

} // namespace imx307
} // namespace sensor_config
