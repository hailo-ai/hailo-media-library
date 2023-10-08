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
              }
            },
            "required": [
              "width",
              "height",
              "framerate"
            ]
          }
        },
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
              "required": [
                "width",
                "height",
                "framerate"
              ]
            }
          }
        },
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
          "dewarp_config_path": {
            "type": "string"
          }
        },
        "required": [
          "enabled",
          "color_interpolation",
          "sensor_calib_path",
          "dewarp_config_path"
        ]
      },
      "dis": {
        "type": "object",
        "properties": {
          "enabled": {
            "type": "boolean"
          }
        },
        "required": [
          "enabled"
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
            "required": [
              "x",
              "y",
              "width",
              "height"
            ]
          }
        },
        "required": [
          "enabled",
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
        "required": [
          "enabled",
          "direction"
        ]
      }
    },
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