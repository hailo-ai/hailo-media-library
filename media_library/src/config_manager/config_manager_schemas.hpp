/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
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
static nlohmann::json encoding_config_schema = R"(
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "Media Library schema for encoder configuration",
    "type": "object",
    "definitions": {
      "roi": {
        "$id": "roi",
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
        ],
        "additionalProperties": false
      },
      "roi_area" : {
        "$id": "roi_area",
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
        ],
        "additionalProperties": false
      },
      "input_stream": {
        "$id": "input_stream",
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
        ],
        "additionalProperties": false
      },
      "hailo_encoder": {
        "$id": "hailo_encoder",
        "type": "object",
        "properties": {
          "config": {
            "type": "object",
            "properties": {
              "output_stream": {
                "type": "object",
                "properties": {
                  "codec": {
                    "type": "string",
                    "enum": [
                      "CODEC_TYPE_H264",
                      "CODEC_TYPE_HEVC"
                    ]
                  },
                  "profile": {
                    "type": "string",
                    "enum": [
                      "VCENC_HEVC_MAIN_PROFILE",
                      "VCENC_HEVC_MAIN_STILL_PICTURE_PROFILE",
                      "VCENC_H264_BASE_PROFILE",
                      "VCENC_H264_MAIN_PROFILE",
                      "VCENC_H264_HIGH_PROFILE",
                      "auto"
                    ]
                  },
                  "level": {
                    "type": "string"
                  }
                },
                "required": [
                  "codec"
                ],
                "additionalProperties": false,
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
                            "VCENC_HEVC_MAIN_STILL_PICTURE_PROFILE",
                            "auto"
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
                          "VCENC_H264_HIGH_PROFILE",
                          "auto"
                        ]
                      }
                    }
                  }
                }
              }
            },
            "required": [
              "output_stream"
            ],
            "additionalProperties": false
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
            ],
            "additionalProperties": false
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
                    "type": "string",
                    "enum": [
                      "DEBLOCKING_FILTER_ENABLED",
                      "DEBLOCKING_FILTER_DISABLED",
                      "DEBLOCKING_FILTER_DISABLED_ON_SLICE_EDGES"
                    ]
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
                ],
                "additionalProperties": false
              },
              "intra_area": {
                "$ref": "roi"
              },
              "ipcm_area1": {
                "$ref": "roi"
              },
              "ipcm_area2": {
                "$ref": "roi"
              },
              "roi_area1": {
                "$ref": "roi_area"
              },
              "roi_area2": {
                "$ref": "roi_area"
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
            ],
            "additionalProperties": false
          },
          "rate_control": {
            "type": "object",
            "properties": {
              "rc_mode": {
                "type": "string",
                "enum": [
                  "VBR",
                  "CVBR",
                  "HRD",
                  "CQP"
                ]
              },
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
              "padding": {
                "type": "boolean"
              },
              "cvbr": {
                "type": "integer"
              },
              "hrd_cpb_size": {
                "type": "integer"
              },
              "intra_pic_rate": {
                "type": "integer"
              },
              "gop_length": {
                "type": "integer"
              },
              "monitor_frames": {
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
                  "qp_hdr"
                ],
                "additionalProperties": false
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
                  },
                  "variation": {
                    "type": "integer"
                  }
                },
                "required": [
                  "target_bitrate"
                ],
                "additionalProperties": false
              }
            },
            "required": [
              "rc_mode",
              "picture_rc",
              "picture_skip",
              "intra_pic_rate",
              "quantization",
              "bitrate"
            ],
            "additionalProperties": false
          },
          "monitors_control": {
            "type": "object",
            "properties": {
              "bitrate_monitor": {
                "type": "object",
                "properties": {
                  "enable": {
                    "type": "boolean"
                  },
                  "period": {
                    "type": "integer"
                  },
                  "result_output_path": {
                    "type": "string"
                  },
                  "output_result_to_file": {
                    "type": "boolean"
                  }
                },
                "required": [
                  "enable",
                  "period",
                  "result_output_path",
                  "output_result_to_file"
                ],
                "additionalProperties": false
              },
              "cycle_monitor": {
                "type": "object",
                "properties": {
                  "enable": {
                    "type": "boolean"
                  },
                  "start_delay": {
                    "type": "integer"
                  },
                  "deviation_threshold": {
                    "type": "integer"
                  },
                  "result_output_path": {
                    "type": "string"
                  },
                  "output_result_to_file": {
                    "type": "boolean"
                  }
                },
                "required": [
                  "enable",
                  "start_delay",
                  "deviation_threshold",
                  "result_output_path",
                  "output_result_to_file"
                ],
                "additionalProperties": false
              }
            },
            "required": [
              "bitrate_monitor",
              "cycle_monitor"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "config",
          "gop_config",
          "coding_control",
          "rate_control"
        ],
        "additionalProperties": false
      },
      "jpeg_encoder": {
        "$id": "jpeg_encoder",
        "type": "object",
        "properties": {
          "n_threads": {
            "type": "integer",
            "minimum": 1,
            "maximum": 4
          },
          "quality": {
            "type": "integer",
            "minimum": 0,
            "maximum": 100
          }
        },
        "required": [
          "n_threads",
          "quality"
        ],
        "additionalProperties": false
      }
    },
    "properties": {
      "encoding": {
        "type": "object",
        "oneOf": [
          {
            "type": "object",
            "properties": {
              "hailo_encoder": {
                "$ref": "hailo_encoder"
              },
              "input_stream": {
                "$ref": "input_stream"
              }
            },
            "required": [
              "input_stream",
              "hailo_encoder"
            ],
            "additionalProperties": false
          },
          {
            "type": "object",
            "properties": {
              "jpeg_encoder": {
                "$ref": "jpeg_encoder"
              },
              "input_stream": {
                "$ref": "input_stream"
              }
            },
            "required": [
              "input_stream",
              "jpeg_encoder"
            ],
            "additionalProperties": false
          }
        ]
      }
    },
    "required" : [
      "encoding"
    ],
    "additionalProperties": true
  })"_json;

static nlohmann::json osd_config_schema = R"(
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "$id": "osdschema",
    "title": "Media Library schema for on screen display configuration",
    "type": "object",
    "$defs": {
      "color": {
        "type": "array",
        "items": {
          "type": "integer",
          "minimum": 0,
          "maximum": 255
        }
      },
      "angle": {
        "type": "integer",
        "minimum": 0,
        "maximum": 359
      },
      "rotation_policy": {
        "type": "string",
        "enum": [
          "CENTER",
          "TOP_LEFT"
        ]
      },
      "coordinate": {
        "type": "number",
        "minimum": 0,
        "maximum": 1
      },
      "z-index": {
        "type": "integer"
      },
      "horizontal_alignment": {
        "oneOf": [
          {
            "type": "string",
            "enum": [
              "RIGHT",
              "LEFT",
              "CENTER"
            ]
          },
          {
            "type": "number"
          }
        ]
      },
      "vertical_alignment": {
        "oneOf": [
          {
            "type": "string",
            "enum": [
              "TOP",
              "BOTTOM",
              "CENTER"
            ]
          },
          {
            "type": "number"
          }
        ]
      },
      "font_weight": {
        "type": "string",
        "enum": [
          "NORMAL",
          "BOLD"
        ]
      },
      "outline_size": {
        "type": "integer",
        "minimum": 0
      },
      "line_thickness": {
        "type": "integer",
        "minimum": 1
      }
    },
    "properties": {
      "osd": {
        "type": "object",
        "properties": {
          "image": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "id": {
                  "type": "string"
                },
                "image_path": {
                  "type": "string"
                },
                "width": {
                  "type": "number"
                },
                "height": {
                  "type": "number"
                },
                "angle": {
                  "$ref": "#/$defs/angle"
                },
                "rotation_policy": {
                  "$ref": "#/$defs/rotation_policy"
                },
                "x": {
                  "$ref": "#/$defs/coordinate"
                },
                "y": {
                  "$ref": "#/$defs/coordinate"
                },
                "z-index": {
                  "$ref": "#/$defs/z-index"
                },
                "horizontal_alignment": {
                  "$ref": "#/$defs/horizontal_alignment"
                },
                "vertical_alignment": {
                  "$ref": "#/$defs/vertical_alignment"
                }
              },
              "required": [
                "id",
                "image_path",
                "width",
                "height",
                "angle",
                "rotation_policy",
                "x",
                "y",
                "z-index"
              ],
              "additionalProperties": false
            }
          },
          "dateTime": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "id": {
                  "type": "string"
                },
                "font_size": {
                  "type": "integer",
                  "minimum": 1
                },
                "font_path": {
                  "type": "string"
                },
                "font_weight": {
                  "$ref": "#/$defs/font_weight"
                },
                "datetime_format": {
                  "type": "string"
                },
                "line_thickness": {
                  "$ref": "#/$defs/line_thickness"
                },
                "text_color": {
                  "$ref": "#/$defs/color"
                },
                "background_color": {
                  "$ref": "#/$defs/color"
                },
                "outline_size": {
                  "$ref": "#/$defs/outline_size"
                },
                "outline_color": {
                  "$ref": "#/$defs/color"
                },
                "angle": {
                  "$ref": "#/$defs/angle"
                },
                "rotation_policy": {
                  "$ref": "#/$defs/rotation_policy"
                },
                "x": {
                  "$ref": "#/$defs/coordinate"
                },
                "y": {
                  "$ref": "#/$defs/coordinate"
                },
                "z-index": {
                  "$ref": "#/$defs/z-index"
                },
                "shadow_color": {
                  "$ref": "#/$defs/color"
                },
                "shadow_offset_x": {
                  "type": "number"
                },
                "shadow_offset_y": {
                  "type": "number"
                },
                "horizontal_alignment": {
                  "$ref": "#/$defs/horizontal_alignment"
                },
                "vertical_alignment": {
                  "$ref": "#/$defs/vertical_alignment"
                }
              },
              "required": [
                "id",
                "font_size",
                "font_path",
                "text_color",
                "angle",
                "rotation_policy",
                "x",
                "y",
                "z-index"
              ],
              "additionalProperties": false
            }
          },
          "text": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "id": {
                  "type": "string"
                },
                "label": {
                  "type": "string"
                },
                "font_size": {
                  "type": "integer",
                  "minimum": 1
                },
                "font_path": {
                  "type": "string"
                },
                "font_weight": {
                  "$ref": "#/$defs/font_weight"
                },
                "line_thickness": {
                  "$ref": "#/$defs/line_thickness"
                },
                "text_color": {
                  "$ref": "#/$defs/color"
                },
                "background_color": {
                  "$ref": "#/$defs/color"
                },
                "outline_size": {
                  "$ref": "#/$defs/outline_size"
                },
                "outline_color": {
                  "$ref": "#/$defs/color"
                },
                "angle": {
                  "$ref": "#/$defs/angle"
                },
                "rotation_policy": {
                  "$ref": "#/$defs/rotation_policy"
                },
                "x": {
                  "$ref": "#/$defs/coordinate"
                },
                "y": {
                  "$ref": "#/$defs/coordinate"
                },
                "z-index": {
                  "$ref": "#/$defs/z-index"
                },
                "shadow_color": {
                  "$ref": "#/$defs/color"
                },
                "shadow_offset_x": {
                  "type": "number"
                },
                "shadow_offset_y": {
                  "type": "number"
                },
                "horizontal_alignment": {
                  "$ref": "#/$defs/horizontal_alignment"
                },
                "vertical_alignment": {
                  "$ref": "#/$defs/vertical_alignment"
                }
              },
              "required": [
                "id",
                "label",
                "font_size",
                "font_path",
                "angle",
                "rotation_policy",
                "text_color",
                "x",
                "y",
                "z-index"
              ],
              "additionalProperties": false
            }
          }
        },
        "additionalProperties": false
      }
    },
    "additionalProperties": true
  }
  )"_json;

static nlohmann::json multi_resize_application_input_streams_config_schema = R"(
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "Media Library schema for multi-resize configuration",
    "type": "object",
    "properties": {
      "application_input_streams": {
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
                },
                "keep_aspect_ratio": {
                  "type": "boolean"
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
      }
    }
  }
  )"_json;

static nlohmann::json multi_resize_digital_zoom_config_schema = R"(
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "Digital Zoom Configuration",
    "type": "object",
    "properties": {
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
      }
    },
    "required": [
      "digital_zoom"
    ]
  }
  )"_json;

static nlohmann::json multi_resize_motion_detection_config_schema = R"(
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "Motion Detection Configuration",
    "type": "object",
    "properties": {
      "motion_detection": {
        "type": "object",
        "properties": {
          "enabled": {
            "type": "boolean"
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
              "framerate"
            ]
          },
          "threshold": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "sensitivity_level": {
            "type": "string"
          }
        },
        "additionalProperties": false,
        "required": [
          "enabled",
          "roi",
          "resolution",
          "threshold",
          "sensitivity_level"
        ]
      }
    }
  }
  )"_json;

nlohmann::json multi_resize_config_schema = {
    {"allOf",
     {multi_resize_application_input_streams_config_schema, multi_resize_motion_detection_config_schema,
      multi_resize_digital_zoom_config_schema}}};

static nlohmann::json ldc_dewarp_config_schema = R"(
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "Dewarp Configuration",
  "type": "object",
  "properties": {
    "dewarp": {
      "type": "object",
      "properties": {
        "enabled": { "type": "boolean" },
        "color_interpolation": { "type": "string" },
        "sensor_calib_path": { "type": "string" },
        "camera_type": { "type": "string" }
      },
      "additionalProperties": false,
      "required": [
        "enabled",
        "color_interpolation",
        "sensor_calib_path",
        "camera_type"
      ]
    }
  },
  "required": ["dewarp"]
}
)"_json;

static nlohmann::json ldc_dis_config_schema = R"(
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "DIS Configuration",
  "type": "object",
  "properties": {
    "dis": {
      "type": "object",
      "properties": {
        "enabled": { "type": "boolean" },
        "minimun_coefficient_filter": { "type": "number", "minimum": 0, "maximum": 1 },
        "decrement_coefficient_threshold": { "type": "number", "minimum": 0, "maximum": 1 },
        "increment_coefficient_threshold": { "type": "number", "minimum": 0, "maximum": 1 },
        "running_average_coefficient": { "type": "number", "minimum": 0, "maximum": 1 },
        "std_multiplier": { "type": "number", "exclusiveMinimum": 0 },
        "black_corners_correction_enabled": { "type": "boolean" },
        "black_corners_threshold": { "type": "number" },
        "average_luminance_threshold": { "type": "number", "minimum": 0, "maximum": 255 },
        "camera_fov_factor": { "type": "number", "minimum": 0.1, "maximum": 1 },
        "angular_dis": {
          "type": "object",
          "properties": {
            "enabled": { "type": "boolean" },
            "vsm": {
              "type": "object",
              "properties": {
                "hoffset": { "type": "number" },
                "voffset": { "type": "number" },
                "width": { "type": "number" },
                "height": { "type": "number" },
                "max_displacement": { "type": "number" }
              },
              "required": [
                "hoffset", "voffset", "width", "height", "max_displacement"
              ]
            }
          },
          "required": ["enabled", "vsm"]
        },
        "debug": {
          "type": "object",
          "properties": {
            "generate_resize_grid": { "type": "boolean" },
            "fix_stabilization": { "type": "boolean" },
            "fix_stabilization_longitude": { "type": "number" },
            "fix_stabilization_latitude": { "type": "number" }
          },
          "additionalProperties": false,
          "required": [
            "generate_resize_grid",
            "fix_stabilization",
            "fix_stabilization_longitude",
            "fix_stabilization_latitude"
          ]
        }
      },
      "additionalProperties": false,
      "required": [
        "enabled",
        "minimun_coefficient_filter",
        "decrement_coefficient_threshold",
        "increment_coefficient_threshold",
        "running_average_coefficient",
        "std_multiplier",
        "black_corners_correction_enabled",
        "black_corners_threshold",
        "average_luminance_threshold",
        "camera_fov_factor",
        "angular_dis",
        "debug"
      ]
    }
  },
  "required": ["dis"]
}
)"_json;

static nlohmann::json ldc_eis_config_schema = R"(
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "EIS Configuration",
  "type": "object",
  "properties": {
    "eis": {
      "type": "object",
      "properties": {
        "enabled": { "type": "boolean" },
        "stabilize": { "type": "boolean" },
        "eis_config_path": { "type": "string" },
        "window_size": { "type": "number" },
        "rotational_smoothing_coefficient": { "type": "number" },
        "iir_hpf_coefficient": { "type": "number", "minimum": 0, "maximum": 1 },
        "camera_fov_factor": { "type": "number", "minimum": 0.1, "maximum": 1 },
        "line_readout_time": { "type": "number" },
        "hdr_exposure_ratio": { "type": "number" },
        "min_angle_deg": { "type": "number", "minimum": 0.0, "maximum": 360.0 },
        "max_angle_deg": { "type": "number", "minimum": 0.0, "maximum": 360.0 },
        "shakes_type_buff_size": { "type": "number", "minimum": 1 },
        "extensions_per_thr": { "type": "number" }
      },
      "additionalProperties": false,
      "required": [
        "enabled", "eis_config_path", "window_size",
        "rotational_smoothing_coefficient", "iir_hpf_coefficient",
        "camera_fov_factor", "line_readout_time", "hdr_exposure_ratio",
        "min_angle_deg", "max_angle_deg"
      ]
    }
  },
  "required": ["eis"]
}
)"_json;

static nlohmann::json ldc_gyro_config_schema = R"(
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "Gyro Configuration",
  "type": "object",
  "properties": {
    "gyro": {
      "type": "object",
      "properties": {
        "enabled": { "type": "boolean" },
        "sensor_name": { "type": "string" },
        "sensor_frequency": { "type": "string" },
        "scale": { "type": "number", "minimum": 0, "maximum": 1 }
      },
      "additionalProperties": false,
      "required": ["enabled", "sensor_name", "sensor_frequency", "scale"]
    }
  },
  "required": ["gyro"]
}
)"_json;

static nlohmann::json ldc_gmv_config_schema = R"(
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "GMV Configuration",
  "type": "object",
  "properties": {
    "gmv": {
      "type": "object",
      "properties": {
        "source": { "type": "string" },
        "frequency": { "type": "number" }
      },
      "additionalProperties": false,
      "required": ["source", "frequency"]
    }
  },
  "required": ["gmv"]
}
)"_json;

static nlohmann::json ldc_optical_zoom_config_schema = R"(
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "Optical Zoom Configuration",
  "type": "object",
  "properties": {
    "optical_zoom": {
      "type": "object",
      "properties": {
        "enabled": { "type": "boolean" },
        "magnification": { "type": "number" },
        "max_dewarping_magnification": { "type": "number" }
      },
      "additionalProperties": false,
      "required": ["magnification", "enabled"]
    }
  },
  "required": ["optical_zoom"]
}
)"_json;

static nlohmann::json ldc_rotation_config_schema = R"(
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "Rotation Configuration",
  "type": "object",
  "properties": {
    "rotation": {
      "type": "object",
      "properties": {
        "enabled": { "type": "boolean" },
        "angle": { "type": "string" }
      },
      "additionalProperties": false,
      "required": ["enabled", "angle"]
    }
  },
  "required": ["rotation"]
}
)"_json;

static nlohmann::json ldc_flip_config_schema = R"(
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "Flip Configuration",
  "type": "object",
  "properties": {
    "flip": {
      "type": "object",
      "properties": {
        "enabled": { "type": "boolean" },
        "direction": { "type": "string" }
      },
      "additionalProperties": false,
      "required": ["enabled", "direction"]
    }
  },
  "required": ["flip"]
}
)"_json;

nlohmann::json ldc_config_schema = {
    {"allOf",
     {ldc_dewarp_config_schema, ldc_dis_config_schema, ldc_eis_config_schema, ldc_gyro_config_schema,
      ldc_optical_zoom_config_schema, ldc_rotation_config_schema, ldc_flip_config_schema}}};

static nlohmann::json hailort_config_schema = R"(
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "Media Library schema for HailoRT configuration",
    "type": "object",
    "properties": {
      "hailort": {
        "type": "object",
        "properties": {
          "device-id": {
            "type": "string"
          }
        },
        "additionalProperties": false,
        "required": [
          "device-id"
        ]
      }
    },
    "required": [
      "hailort"
    ]
  }
  )"_json;

static nlohmann::json isp_config_schema = R"(
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "Media Library schema for ISP configuration",
    "type": "object",
    "properties": {
      "isp": {
        "type": "object",
        "properties": {
          "auto-configuration": {
            "type": "boolean"
          },
          "isp_config_files_path": {
            "type" : "string"
          }
        },
        "additionalProperties": false,
        "required": [
          "auto-configuration"
        ]
      }
    },
    "required": [
      "isp"
    ]
  }
  )"_json;

static nlohmann::json hdr_config_schema = R"(
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "Media Library schema for HDR configuration",
    "type": "object",
    "properties": {
      "hdr": {
        "type": "object",
        "properties": {
          "enabled": {
            "type": "boolean"
          },
          "dol": {
            "type": "integer",
            "minimum": 2,
            "maximum": 3
          },
          "lsRatio": {
            "type": "number"
          },
          "vsRatio": {
            "type": "number"
          }
        },
        "additionalProperties": false,
        "required": [
          "enabled",
          "dol"
        ]
      }
    },
    "required": [
      "hdr"
    ]
  }
  )"_json;

static nlohmann::json denoise_config_schema = R"(
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "Media Library schema for denoise configuration",
    "type": "object",
    "properties": {
      "denoise": {
        "type": "object",
        "properties": {
          "enabled": {
            "type": "boolean"
          },
          "bayer": {
            "type": "boolean"
          },
          "sensor": {
            "type": "string"
          },
          "method": {
            "type": "string"
          },
          "loopback-count": {
            "type": "number"
          },
          "network": {
            "type": "object",
            "properties": {
              "network_path": {
                "type": "string"
              },
              "y_channel": {
                "type": "string"
              },
              "uv_channel": {
                "type": "string"
              },
              "feedback_y_channel": {
                "type": "string"
              },
              "feedback_uv_channel": {
                "type": "string"
              },
              "output_y_channel": {
                "type": "string"
              },
              "output_uv_channel": {
                "type": "string"
              },
              "bayer_channel": {
                "type": "string"
              },
              "feedback_bayer_channel": {
                "type": "string"
              },
              "output_bayer_channel": {
                "type": "string"
              }
            },
            "additionalProperties": false
          }
        },
        "additionalProperties": false,
        "required": [
          "enabled",
          "sensor",
          "method",
          "loopback-count",
          "network"
        ],
        "if": {
          "properties": {
            "bayer": {"const": true}
          },
          "required": [
            "bayer"
          ]
        },
        "then": {
          "properties": {
            "network": {
              "required": [
                "network_path",
                "bayer_channel",
                "feedback_bayer_channel",
                "output_bayer_channel"
              ]
            }
          }
        },
        "else": {
          "properties": {
            "network": {
              "required": [
                "network_path",
                "y_channel",
                "uv_channel",
                "feedback_y_channel",
                "feedback_uv_channel",
                "output_y_channel",
                "output_uv_channel"
              ]
            }
          }
        }
      }
    },
    "required": [
      "denoise"
    ]
  }
  )"_json;

static nlohmann::json vsm_config_schema = R"(
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "Media Library schema for vsm media server configuration",
    "type": "object",
    "properties": {
      "vsm": {
        "type": "object",
        "properties": {
          "vsm_h_size": {
            "type": "number"
          },
          "vsm_h_offset": {
            "type": "number"
          },
          "vsm_v_size": {
            "type": "number"
          },
          "vsm_v_offset": {
            "type": "number"
          }
        },
        "additionalProperties": false,
        "required": [
          "vsm_h_size",
          "vsm_h_offset",
          "vsm_v_size",
          "vsm_v_offset"
        ]
      },
      "3dnr": {
        "type": "object",
        "properties": {
          "enable": {
            "type": "boolean"
          }
        },
        "additionalProperties": false,
        "required": [
          "enable"
        ]
      }
    },
    "required": [
      "vsm",
      "3dnr"
    ]
  }
  )"_json;

static nlohmann::json input_video_config_schema = R"(
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "Media Library schema for input resolution configuration",
  "type": "object",
  "properties": {
    "input_video": {
      "type": "object",
      "properties": {
        "source": {
          "type": "string"
        },
        "source_type":{
          "type": "string",
          "enum": ["V4L2SRC", "APPSRC"],
          "default": "V4L2SRC"
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
            }
          },
          "additionalProperties": false,
          "required": [
            "width",
            "height",
            "framerate"
          ]
        }
      },
      "additionalProperties": false,
      "required": [
        "source",
        "resolution"
      ]
    }
  },
  "required": [
    "input_video"
  ]
})"_json;

static nlohmann::json frontend_config_schema = {
    {"allOf",
     {multi_resize_config_schema, ldc_config_schema, hailort_config_schema, isp_config_schema, hdr_config_schema,
      denoise_config_schema, input_video_config_schema}}};

static nlohmann::json encoder_config_schema = {{"allOf", {encoding_config_schema, osd_config_schema}}};

static nlohmann::json medialib_config_schema = R"(
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "properties": {
    "default_profile": { "type": "string" },
    "profiles": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "name": { "type": "string" },
          "config_file": { "type": "string" }
        },
        "required": ["name", "config_file"]
      }
    }
  },
  "required": [
    "default_profile", "profiles"
  ]
}
)"_json;

static nlohmann::json codec_configs_schema = R"(
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "properties": {
    "encoded_output_streams": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "stream_id": { "type": "string" },
          "config_path": { "type": "string" }
        },
        "required": ["stream_id", "config_path"]
      }
    }
  },
  "required": [
    "encoded_output_streams"
  ]
}
)"_json;

static nlohmann::json isp_config_files_schema = R"(
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "properties": {
    "isp_config_files": {
      "type": "object",
      "properties": {
        "3a_config_path": { "type": "string" },
        "sensor_entry": { "type": "string" }
      },
      "required": ["3a_config_path", "sensor_entry"]
    }
  },
  "required": [
    "isp_config_files"
  ]
}
)"_json;

static nlohmann::json profile_config_schema = {
    {"allOf",
     {multi_resize_config_schema, ldc_config_schema, hailort_config_schema, isp_config_schema, hdr_config_schema,
      denoise_config_schema, input_video_config_schema, codec_configs_schema, isp_config_files_schema}}};

} // namespace config_schemas
