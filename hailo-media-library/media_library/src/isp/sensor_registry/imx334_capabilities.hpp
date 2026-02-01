#pragma once
#include "sensor_types.hpp"
#include <linux/videodev2.h>

namespace sensor_config
{
namespace imx334
{

inline const SensorCapabilities capabilities{.sensor_name = "IMX334",
                                             .sub_dev_prefix = "imx334",
                                             .supported_resolutions = {Resolution::UHD_4K},
                                             .pixel_format = V4L2_PIX_FMT_SRGGB12,

                                             // 4K SDR
                                             .mode_mappings = {
                                                 {SensorModeKey(Resolution::UHD_4K),
                                                  SensorModeInfo{
                                                      .sensor_mode = 0,
                                                      .csi_mode = CSI_MODE_SDR,
                                                  }},
                                             }};

} // namespace imx334
} // namespace sensor_config
