/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <nlohmann/json-schema.hpp>

namespace config_schemas
{
  static nlohmann::json encoder_config_schema = R"(
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "Media Library schema for encoder configuration",
    "type": "object",
    "properties": {
      "config": {
        "type": "object",
        "properties": {
          "input_stream": {
            "type": "object",
            "properties": {
              "width": {
                "type": "integer"
              },
              "height": {
                "type": "integer"
              },
              "framerate": {
                "type": "integer"
              },
              "format": {
                "type": "string"
              }
            },
            "required": [
              "width",
              "height",
              "framerate",
              "format"
            ]
          },
          "output_stream": {
            "type": "object",
            "properties": {
              "codec": {
                "type": "string",
                "enum": [
                  "h264",
                  "hevc"
                ]
              },
              "profile": {
                "type": "string",
                "enum": [
                  "VCENC_HEVC_MAIN_PROFILE",
                  "VCENC_HEVC_MAIN_STILL_PICTURE_PROFILE",
                  "VCENC_H264_BASE_PROFILE",
                  "VCENC_H264_MAIN_PROFILE",
                  "VCENC_H264_HIGH_PROFILE"
                ]
              },
              "level": {
                "type": "string"
              },
              "bit_depth_luma": {
                "type": "integer"
              },
              "bit_depth_chroma": {
                "type": "integer"
              },
              "stream_type": {
                "type": "string"
              }
            },
            "required": [
              "codec",
              "profile",
              "level",
              "bit_depth_luma",
              "bit_depth_chroma",
              "stream_type"
            ],
            "if": {
              "properties": {
                "output_stream": {
                  "codec": "hevc"
                }
              },
              "then": {
                "properties": {
                  "output_stream": {
                    "profile": {
                      "enum": [
                        "VCENC_HEVC_MAIN_PROFILE",
                        "VCENC_HEVC_MAIN_STILL_PICTURE_PROFILE"
                      ]
                    }
                  }
                }
              }
            },
            "else": {
              "properties": {
                "output_stream": {
                  "profile": {
                    "enum": [
                      "VCENC_H264_BASE_PROFILE",
                      "VCENC_H264_MAIN_PROFILE",
                      "VCENC_H264_HIGH_PROFILE"
                    ]
                  }
                }
              }
            }
          }
        },
        "required": [
          "input_stream",
          "output_stream"
        ]
      },
      "gop_config": {
        "type": "object",
        "properties": {
          "gop_size": {
            "type": "integer",
            "minimum": 0
          },
          "b_frame_qp_delta": {
            "type": "integer"
          }
        },
        "required": [
          "gop_size",
          "b_frame_qp_delta"
        ]
      },
      "coding_control": {
        "type": "object",
        "properties": {
          "sei_messages": {
            "type": "boolean"
          },
          "deblocking_filter": {
            "type": "object",
            "properties": {
              "type": {
                "type": "string"
              },
              "tc_offset": {
                "type": "integer"
              },
              "beta_offset": {
                "type": "integer"
              },
              "deblock_override": {
                "type": "boolean"
              }
            },
            "required": [
              "type",
              "tc_offset",
              "beta_offset",
              "deblock_override"
            ]
          },
          "intra_area": {
            "type": "object",
            "properties": {
              "enable": {
                "type": "boolean"
              },
              "top": {
                "type": "integer"
              },
              "left": {
                "type": "integer"
              },
              "bottom": {
                "type": "integer"
              },
              "right": {
                "type": "integer"
              }
            },
            "required": [
              "enable",
              "top",
              "left",
              "bottom",
              "right"
            ]
          },
          "ipcm_area1": {
            "type": "object",
            "properties": {
              "enable": {
                "type": "boolean"
              },
              "top": {
                "type": "integer"
              },
              "left": {
                "type": "integer"
              },
              "bottom": {
                "type": "integer"
              },
              "right": {
                "type": "integer"
              }
            },
            "required": [
              "enable",
              "top",
              "left",
              "bottom",
              "right"
            ]
          },
          "ipcm_area2": {
            "type": "object",
            "properties": {
              "enable": {
                "type": "boolean"
              },
              "top": {
                "type": "integer"
              },
              "left": {
                "type": "integer"
              },
              "bottom": {
                "type": "integer"
              },
              "right": {
                "type": "integer"
              }
            },
            "required": [
              "enable",
              "top",
              "left",
              "bottom",
              "right"
            ]
          },
          "roi_area1": {
            "type": "object",
            "properties": {
              "enable": {
                "type": "boolean"
              },
              "top": {
                "type": "integer"
              },
              "left": {
                "type": "integer"
              },
              "bottom": {
                "type": "integer"
              },
              "right": {
                "type": "integer"
              },
              "qp_delta": {
                "type": "integer"
              }
            },
            "required": [
              "enable",
              "top",
              "left",
              "bottom",
              "right",
              "qp_delta"
            ]
          },
          "roi_area2": {
            "type": "object",
            "properties": {
              "enable": {
                "type": "boolean"
              },
              "top": {
                "type": "integer"
              },
              "left": {
                "type": "integer"
              },
              "bottom": {
                "type": "integer"
              },
              "right": {
                "type": "integer"
              },
              "qp_delta": {
                "type": "integer"
              }
            },
            "required": [
              "enable",
              "top",
              "left",
              "bottom",
              "right",
              "qp_delta"
            ]
          }
        },
        "required": [
          "sei_messages",
          "deblocking_filter",
          "intra_area",
          "ipcm_area1",
          "ipcm_area2",
          "roi_area1",
          "roi_area2"
        ]
      },
      "rate_control": {
        "type": "object",
        "properties": {
          "picture_rc": {
            "type": "boolean"
          },
          "picture_skip": {
            "type": "boolean"
          },
          "ctb_rc": {
            "type": "boolean"
          },
          "block_rc_size": {
            "type": "integer"
          },
          "hrd": {
            "type": "boolean"
          },
          "hrd_cpb_size": {
            "type": "integer"
          },
          "monitor_frames": {
            "type": "integer"
          },
          "gop_length": {
            "type": "integer"
          },
          "quantization": {
            "type": "object",
            "properties": {
              "qp_min": {
                "type": "integer"
              },
              "qp_max": {
                "type": "integer"
              },
              "qp_hdr": {
                "type": "integer"
              },
              "intra_qp_delta": {
                "type": "integer"
              },
              "fixed_intra_qp": {
                "type": "integer"
              }
            },
            "required": [
              "qp_min",
              "qp_max",
              "qp_hdr",
              "intra_qp_delta",
              "fixed_intra_qp"
            ]
          },
          "bitrate": {
            "type": "object",
            "properties": {
              "target_bitrate": {
                "type": "integer"
              },
              "bit_var_range_i": {
                "type": "integer"
              },
              "bit_var_range_p": {
                "type": "integer"
              },
              "bit_var_range_b": {
                "type": "integer"
              },
              "tolerance_moving_bitrate": {
                "type": "integer"
              }
            },
            "required": [
              "target_bitrate",
              "bit_var_range_i",
              "bit_var_range_p",
              "bit_var_range_b",
              "tolerance_moving_bitrate"
            ]
          }
        },
        "required": [
          "picture_rc",
          "picture_skip",
          "ctb_rc",
          "block_rc_size",
          "hrd",
          "hrd_cpb_size",
          "monitor_frames",
          "gop_length",
          "quantization",
          "bitrate"
        ]
      }
    }
  })"_json;

  static nlohmann::json vision_config_schema = R"(
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "Media Library schema for vision pre proc configuration",
    "type": "object",
    "properties": {
      "input_stream": {
        "type": "object",
        "properties": {
          "source": {
            "type": "string"
          },
          "format": {
            "type": "string"
          },
          "resolution": {
            "type": "object",
            "properties": {
              "width": {
                "type": "number"
              },
              "height": {
                "type": "number"
              },
              "framerate": {
                "type": "number"
              },
              "pool_max_buffers": {
                "type": "number"
              }
            },
            "additionalProperties": false,
            "required": [
              "width",
              "height",
              "framerate",
              "pool_max_buffers"
            ]
          }
        },
        "additionalProperties": false,
        "required": [
          "source",
          "format",
          "resolution"
        ]
      },
      "output_video": {
        "type": "object",
        "properties": {
          "method": {
            "type": "string"
          },
          "format": {
            "type": "string"
          },
          "grayscale": {
            "type": "boolean"
          },
          "resolutions": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "width": {
                  "type": "number"
                },
                "height": {
                  "type": "number"
                },
                "framerate": {
                  "type": "number"
                },
                "pool_max_buffers": {
                  "type": "number"
                }
              },
              "additionalProperties": false,
              "required": [
                "width",
                "height",
                "framerate",
                "pool_max_buffers"
              ]
            }
          }
        },
        "additionalProperties": false,
        "required": [
          "method",
          "format",
          "resolutions"
        ]
      },
      "dewarp": {
        "type": "object",
        "properties": {
          "enabled": {
            "type": "boolean"
          },
          "color_interpolation": {
            "type": "string"
          },
          "sensor_calib_path": {
            "type": "string"
          },
          "camera_type": {
            "type": "string"
          },
          "camera_fov": {
            "type": "number",
            "maximum": 359.0
          }
        },
        "required": [
          "enabled",
          "color_interpolation",
          "sensor_calib_path",
          "camera_type",
          "camera_fov"
        ]
      },
      "dis": {
        "type": "object",
        "properties": {
          "enabled": {
            "type": "boolean"
          },
          "minimun_coefficient_filter": {
            "type": "number",
            "minimum": 0.0,
            "maximum": 1.0
          },
          "decrement_coefficient_threshold": {
            "type": "number",
            "minimum": 0.0,
            "maximum": 1.0
          },
          "increment_coefficient_threshold": {
            "type": "number",
            "minimum": 0.0,
            "maximum": 1.0
          },
          "running_average_coefficient": {
            "type": "number",
            "minimum": 0.0,
            "maximum": 1.0
          },
          "std_multiplier": {
            "type": "number",
            "exclusiveMinimum": 0.0
          },
          "black_corners_correction_enabled": {
            "type": "boolean"
          },
          "black_corners_threshold": {
            "type": "number"
          },
          "debug":
          {
            "type": "object",
            "properties": {
              "generate_resize_grid": {
                "type": "boolean"
              },
              "fix_stabilization": {
                "type": "boolean"
              },
              "fix_stabilization_longitude": {
                "type": "number"
              },
              "fix_stabilization_latitude": {
                "type": "number"
              }
            },
            "required": [
              "generate_resize_grid",
              "fix_stabilization",
              "fix_stabilization_longitude",
              "fix_stabilization_latitude"
            ]
          }
        },
        "required": [
          "enabled",
          "minimun_coefficient_filter",
          "decrement_coefficient_threshold",
          "increment_coefficient_threshold",
          "running_average_coefficient",
          "std_multiplier",
          "black_corners_correction_enabled",
          "black_corners_threshold",
          "debug"
        ]
      },
      "gmv": {
        "type": "object",
        "properties": {
          "source": {
            "type": "string"
          },
          "frequency": {
            "type": "number"
          }
        },
        "additionalProperties": false,
        "required": [
          "source",
          "frequency"
        ]
      },
      "digital_zoom": {
        "type": "object",
        "properties": {
          "enabled": {
            "type": "boolean"
          },
          "mode": {
            "type": "string"
          },
          "magnification": {
            "type": "number"
          },
          "roi": {
            "type": "object",
            "properties": {
              "x": {
                "type": "number"
              },
              "y": {
                "type": "number"
              },
              "width": {
                "type": "number"
              },
              "height": {
                "type": "number"
              }
            },
            "additionalProperties": false,
            "required": [
              "x",
              "y",
              "width",
              "height"
            ]
          }
        },
        "additionalProperties": false,
        "required": [
          "enabled",
          "mode",
          "magnification",
          "roi"
        ]
      },
      "rotation": {
        "type": "object",
        "properties": {
          "enabled": {
            "type": "boolean"
          },
          "angle": {
            "type": "string"
          }
        },
        "additionalProperties": false,
        "required": [
          "enabled",
          "angle"
        ]
      },
      "flip": {
        "type": "object",
        "properties": {
          "enabled": {
            "type": "boolean"
          },
          "direction": {
            "type": "string"
          }
        },
        "additionalProperties": false,
        "required": [
          "enabled",
          "direction"
        ]
      }
    },
    "additionalProperties": false,
    "required": [
      "input_stream",
      "output_video",
      "dewarp",
      "dis",
      "gmv",
      "digital_zoom",
      "rotation",
      "flip"
    ]
  }
  )"_json;

} // namespace config_schemas