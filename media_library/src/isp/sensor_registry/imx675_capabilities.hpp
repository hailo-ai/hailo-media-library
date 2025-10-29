#pragma once
#include "sensor_types.hpp"
#include <linux/videodev2.h>

namespace sensor_config
{
namespace imx675
{

inline const SensorCapabilities capabilities{.sensor_name = "IMX675",
                                             .sub_dev_prefix = "imx675",
                                             .supported_resolutions = {Resolution::FIVE_MP, Resolution::FHD},
                                             .pixel_format = V4L2_PIX_FMT_SRGGB12,

                                             // 5MP SDR
                                             .mode_mappings = {
                                                 {SensorModeKey(Resolution::FIVE_MP),
                                                  SensorModeInfo{
                                                      .sensor_mode = 0,
                                                      .csi_mode = CSI_MODE_SDR,
                                                  }},

                                                 // 5MP HDR 2DOL
                                                 {SensorModeKey(Resolution::FIVE_MP, HDR_DOL_2),
                                                  SensorModeInfo{
                                                      .sensor_mode = 4,
                                                      .csi_mode = CSI_MODE_DEFAULT_HDR,
                                                  }},

                                                 // 5MP HDR 3DOL
                                                 {SensorModeKey(Resolution::FIVE_MP, HDR_DOL_3),
                                                  SensorModeInfo{
                                                      .sensor_mode = 3,
                                                      .csi_mode = CSI_MODE_DEFAULT_HDR,
                                                  }},

                                                 // FHD SDR
                                                 {SensorModeKey(Resolution::FHD),
                                                  SensorModeInfo{
                                                      .sensor_mode = 1,
                                                      .csi_mode = CSI_MODE_SDR,
                                                  }},
                                             }};

} // namespace imx675
} // namespace sensor_config
