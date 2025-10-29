#pragma once
#include "sensor_types.hpp"
#include <linux/videodev2.h>

namespace sensor_config
{
namespace imx678
{

inline const SensorCapabilities capabilities{.sensor_name = "IMX678",
                                             .sub_dev_prefix = "imx678",
                                             .supported_resolutions = {Resolution::FHD, Resolution::UHD_4K},
                                             .pixel_format = V4L2_PIX_FMT_SRGGB12,

                                             .mode_mappings = {
                                                 // FHD SDR
                                                 {SensorModeKey(Resolution::FHD),
                                                  SensorModeInfo{
                                                      .sensor_mode = 1,
                                                      .csi_mode = CSI_MODE_SDR,
                                                  }},

                                                 // FHD HDR 3DOL
                                                 {SensorModeKey(Resolution::FHD, HDR_DOL_3),
                                                  SensorModeInfo{
                                                      .sensor_mode = 2,
                                                      .csi_mode = CSI_MODE_MERCURY_ISP_STITCH_HDR,
                                                  }},

                                                 // 4K SDR
                                                 {SensorModeKey(Resolution::UHD_4K),
                                                  SensorModeInfo{
                                                      .sensor_mode = 0,
                                                      .csi_mode = CSI_MODE_SDR,
                                                  }},

                                                 // 4K HDR 2DOL
                                                 {SensorModeKey(Resolution::UHD_4K, HDR_DOL_2),
                                                  SensorModeInfo{
                                                      .sensor_mode = 4,
                                                      .csi_mode = CSI_MODE_DEFAULT_HDR,
                                                  }},

                                                 // 4K HDR 3DOL
                                                 {SensorModeKey(Resolution::UHD_4K, HDR_DOL_3),
                                                  SensorModeInfo{
                                                      .sensor_mode = 3,
                                                      .csi_mode = CSI_MODE_DEFAULT_HDR,
                                                  }},
                                             }};

} // namespace imx678
} // namespace sensor_config
