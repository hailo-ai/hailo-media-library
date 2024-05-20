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
  static nlohmann::json encoder_config_schema = R"(
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
                  }
                },
                "required": [
                  "target_bitrate",
                  "bit_var_range_i",
                  "bit_var_range_p",
                  "bit_var_range_b",
                  "tolerance_moving_bitrate"
                ],
                "additionalProperties": false
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
    "title": "Media Library schema for on screen display configuration",
    "type": "object",
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
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 359
                },
                "rotation_policy":  {
                    "type": "string",
                    "enum": [
                      "CENTER",
                      "TOP_LEFT"
                    ]
                },
                "x": {
                  "type": "number"
                },
                "y": {
                  "type": "number"
                },
                "z-index": {
                  "type": "integer"
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
                "datetime_format": {
                  "type": "string"
                },
                "line_thickness": {
                  "type": "integer",
                  "minimum": 1
                },
                "rgb": {
                  "type": "array",
                  "items": {
                    "type": "integer"
                  }
                },
                "rgb_background": {
                  "type": "array",
                  "items": {
                    "type": "integer"
                  }
                },
                "angle": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 359
                },
                "rotation_policy":  {
                    "type": "string",
                    "enum": [
                      "CENTER",
                      "TOP_LEFT"
                    ]
                },
                "x": {
                  "type": "number"
                },
                "y": {
                  "type": "number"
                },
                "z-index": {
                  "type": "integer"
                }
              },
              "required": [
                "id",
                "font_size",
                "font_path",
                "line_thickness",
                "rgb",
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
                "line_thickness": {
                  "type": "integer",
                  "minimum": 1
                },
                "angle": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 359
                },
                "rotation_policy":  {
                    "type": "string",
                    "enum": [
                      "CENTER",
                      "TOP_LEFT"
                    ]
                },
                "rgb": {
                  "type": "array",
                  "items": {
                    "type": "integer"
                  }
                },
                "rgb_background": {
                  "type": "array",
                  "items": {
                    "type": "integer"
                  }
                },
                "x": {
                  "type": "number"
                },
                "y": {
                  "type": "number"
                },
                "z-index": {
                  "type": "integer"
                }
              },
              "required": [
                "id",
                "label",
                "font_size",
                "line_thickness",
                "font_path",
                "angle",
                "rotation_policy",
                "rgb",
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
    "required": [
      "osd"
    ],
    "additionalProperties": true
  }
  )"_json;

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
            "maximum": 359
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
            "minimum": 0,
            "maximum": 1
          },
          "decrement_coefficient_threshold": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "increment_coefficient_threshold": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "running_average_coefficient": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "std_multiplier": {
            "type": "number",
            "exclusiveMinimum": 0
          },
          "black_corners_correction_enabled": {
            "type": "boolean"
          },
          "black_corners_threshold": {
            "type": "number"
          },
          "angular_dis": {
            "type": "object",
            "properties": {
              "enabled": {
                "type": "boolean"
              },
              "vsm": {
                "type": "object",
                "properties": {
                  "hoffset": {
                    "type": "number"
                  },
                  "voffset": {
                    "type": "number"
                  },
                  "width": {
                    "type": "number"
                  },
                  "height": {
                    "type": "number"
                  },
                  "max_displacement": {
                    "type": "number"
                  }
                },
                "required": [
                  "hoffset",
                  "voffset",
                  "width",
                  "height",
                  "max_displacement"
                ]
              }
            },
            "required": [
              "enabled",
              "vsm"
            ]
          },
          "angular_dis": {
            "type": "object",
            "properties": {
              "enabled": {
                "type": "boolean"
              },
              "vsm": {
                "type": "object",
                "properties": {
                  "hoffset": {
                    "type": "number"
                  },
                  "voffset": {
                    "type": "number"
                  },
                  "width": {
                    "type": "number"
                  },
                  "height": {
                    "type": "number"
                  },
                  "max_displacement": {
                    "type": "number"
                  }
                },
                "required": [
                  "hoffset",
                  "voffset",
                  "width",
                  "height",
                  "max_displacement"
                ]
              }
            },
            "required": [
              "enabled",
              "vsm"
            ]
          },
          "debug": {
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
          "angular_dis",
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
      "optical_zoom": {
        "type": "object",
        "properties": {
          "enabled": {
            "type": "boolean"
          },
          "magnification": {
            "type": "number"
          }
        },
        "additionalProperties": false,
        "required": [
          "magnification",
          "enabled"
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
      "optical_zoom",
      "digital_zoom",
      "rotation",
      "flip"
    ]
  }
  )"_json;

  static nlohmann::json multi_resize_config_schema = R"(
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "Media Library schema for multi-resize configuration",
    "type": "object",
    "properties": {
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
      "output_video",
      "digital_zoom"
    ]
  }
  )"_json;

  static nlohmann::json ldc_config_schema = R"(
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "Media Library schema for LDC configuration",
    "type": "object",
    "properties": {
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
            "maximum": 359
          }
        },
        "additionalProperties": false,
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
            "minimum": 0,
            "maximum": 1
          },
          "decrement_coefficient_threshold": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "increment_coefficient_threshold": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "running_average_coefficient": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "std_multiplier": {
            "type": "number",
            "exclusiveMinimum": 0
          },
          "black_corners_correction_enabled": {
            "type": "boolean"
          },
          "black_corners_threshold": {
            "type": "number"
          },
          "average_luminance_threshold": {
            "type": "number",
            "minimum": 0,
            "maximum": 255
          },
          "angular_dis": {
            "type": "object",
            "properties": {
              "enabled": {
                "type": "boolean"
              },
              "vsm": {
                "type": "object",
                "properties": {
                  "hoffset": {
                    "type": "number"
                  },
                  "voffset": {
                    "type": "number"
                  },
                  "width": {
                    "type": "number"
                  },
                  "height": {
                    "type": "number"
                  },
                  "max_displacement": {
                    "type": "number"
                  }
                },
                "required": [
                  "hoffset",
                  "voffset",
                  "width",
                  "height",
                  "max_displacement"
                ]
              }
            },
            "required": [
              "enabled",
              "vsm"
            ]
          },
          "debug": {
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
          "angular_dis",
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
      "optical_zoom": {
        "type": "object",
        "properties": {
          "enabled": {
            "type": "boolean"
          },
          "magnification": {
            "type": "number"
          }
        },
        "additionalProperties": false,
        "required": [
          "magnification",
          "enabled"
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
    "required": [
      "dewarp",
      "dis",
      "gmv",
      "optical_zoom",
      "rotation",
      "flip"
    ]
  }
  )"_json;

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
              }
            },
            "additionalProperties": false,
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
        },
        "additionalProperties": false,
        "required": [
          "enabled",
          "sensor",
          "method",
          "loopback-count",
          "network"
        ]
      }
    },
    "required": [
      "denoise"
    ]
  }
  )"_json;

  static nlohmann::json defog_config_schema = R"(
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "Media Library schema for defog configuration",
    "type": "object",
    "properties": {
      "defog": {
        "type": "object",
        "properties": {
          "enabled": {
            "type": "boolean"
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
              "output_y_channel": {
                "type": "string"
              },
              "output_uv_channel": {
                "type": "string"
              }
            },
            "additionalProperties": false,
            "required": [
              "network_path",
              "y_channel",
              "uv_channel",
              "output_y_channel",
              "output_uv_channel"
            ]
          }
        },
        "additionalProperties": false,
        "required": [
          "enabled",
          "network"
        ]
      }
    },
    "required": [
      "defog"
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

} // namespace config_schemas