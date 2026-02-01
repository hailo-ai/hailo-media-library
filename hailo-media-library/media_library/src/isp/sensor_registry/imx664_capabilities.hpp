#pragma once
#include "sensor_types.hpp"
#include <linux/videodev2.h>

namespace sensor_config
{
namespace imx664
{

inline const SensorCapabilities capabilities{.sensor_name = "IMX664",
                                             .sub_dev_prefix = "imx664",
                                             .supported_resolutions = {Resolution::FHD, Resolution::FOUR_MP},
                                             .pixel_format = V4L2_PIX_FMT_SRGGB12,

                                             // 4MP SDR
                                             .mode_mappings = {
                                                 {SensorModeKey(Resolution::FOUR_MP),
                                                  SensorModeInfo{
                                                      .sensor_mode = 0,
                                                      .csi_mode = CSI_MODE_SDR,
                                                  }},

                                                 // 4MP HDR 2DOL
                                                 {SensorModeKey(Resolution::FOUR_MP, HDR_DOL_2),
                                                  SensorModeInfo{
                                                      .sensor_mode = 4,
                                                      .csi_mode = CSI_MODE_DEFAULT_HDR,
                                                  }},

                                                 // 4MP HDR 3DOL
                                                 {SensorModeKey(Resolution::FOUR_MP, HDR_DOL_3),
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

                                                 // FHD HDR 3DOL
                                                 {SensorModeKey(Resolution::FHD, HDR_DOL_3),
                                                  SensorModeInfo{
                                                      .sensor_mode = 2,
                                                      .csi_mode = CSI_MODE_DEFAULT_HDR,
                                                  }},
                                             }};

} // namespace imx664
} // namespace sensor_config
