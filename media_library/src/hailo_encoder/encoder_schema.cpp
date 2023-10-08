#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

// Own includes
#include "encoder_config.hpp"
#include "encoder_internal.hpp"

EncoderConfig::EncoderConfig(const std::string& json_string)
    : m_json_string(json_string)
{
    m_doc.Parse(m_json_string.c_str());
}
const Document& EncoderConfig::get_doc() const
{
    return m_doc;
}
const Value::ConstObject EncoderConfig::get_gop_config() const
{
    return m_doc["gop_config"].GetObject();
}
const Value::ConstObject EncoderConfig::get_input_stream() const
{
    return m_doc["config"]["input_stream"].GetObject();
}

const Value::ConstObject EncoderConfig::get_coding_control() const
{
    return m_doc["coding_control"].GetObject();
}

const Value::ConstObject EncoderConfig::get_rate_control() const
{
    return m_doc["rate_control"].GetObject();
}

const Value::ConstObject EncoderConfig::get_output_stream() const
{
    return m_doc["config"]["output_stream"].GetObject();
}

// const char *json_schema const get_json_schema() const {
//     return m_impl->get_json_schema();
// }

// const char *json_schema const get_json_schema() const {
//     std::cout << "get_json_schema()" << std::endl;
//     return m_json_schema
// }

// const char * const load_json_schema() const {
//     std::cout << "load_json_schema()" << std::endl;
//     return R""""({
//         "$schema": "http://json-schema.org/draft-04/schema#",
//         "type": "object",
//         "properties": {
//             "config": {
//                 "type": "object",
//                 "properties": {
//                     "input_stream": {
//                         "type": "object",
//                         "properties": {
//                             "width": {
//                                 "type": "number"
//                             },
//                             "height": {
//                                 "type": "number"
//                             },
//                             "format": {
//                                 "type": "string"
//                             },
//                             "framerate": {
//                                 "type": "number"
//                             },
//                         },
//                         "required": ["width", "height", "format", "framerate"]
//                     },
//                     "output_stream": {
//                         "type": "object",
//                         "properties": {
//                             "codec": {
//                                 "type": "string"
//                             },
//                             "profile": {
//                                 "type": "string"
//                             },
//                             "level": {
//                                 "type": "string"
//                             },
//                             "bit_depth_luma": {
//                                 "type": "integer"
//                             },
//                             "bit_depth_chroma": {
//                                 "type": "integer"
//                             }
//                         },
//                         "required": ["codec", "profile", "level", "bit_depth_luma", "bit_depth_chroma"]
//                     }
//                 },
//                 "required": ["input_stream", "output_stream"]
// 			},
//             "gop_config": {
//                 "type": "object",
//                 "properties": {
//                     "gop_size": {
//                         "type": "integer"
//                     },
//                     "bframe_qp_delta": {
//                         "type": "integer"
//                     }
//                 },
//                 "required": ["gop_size", "bframe_qp_delta"]
//             },
//             "coding_control": {
//                 "type": "object",
//                 "properties": {
//                     "add_sei_messages": {
//                         "type": "boolean"
//                     },
//                     "deblocking_filter": {
//                         "type": "object",
//                         "properties": {
//                             "enabled": {
//                                 "type": "boolean"
//                             },
//                             "disabled_on_edges": {
//                                 "type": "boolean"
//                             },
//                             "tc_offset": {
//                                 "type": "integer"
//                             },
//                             "beta_offset": {
//                                 "type": "integer"
//                             },
//                             "deblock_override": {
//                                 "type": "boolean"
//                             }
//                         },
//                         "required": ["enabled", "disabled_on_edges", "tc_offset", "beta_offset", "deblock_override"]
//                     },
//                     "intra_area": {
//                         "type": "object",
//                         "properties": {
//                             "enabled": {
//                                 "type": "boolean"
//                             },
//                             "left": {
//                                 "type": "integer"
//                             },
//                             "right": {
//                                 "type": "integer"
//                             },
//                             "top": {
//                                 "type": "integer"
//                             },
//                             "bottom": {
//                                 "type": "integer"
//                             }
//                         },
//                         "required": ["enabled", "left", "right", "top", "bottom"]
//                     },
//                     "ipcm_area_1": {
//                         "type": "object",
//                         "properties": {
//                             "enabled": {
//                                 "type": "boolean"
//                             },
//                             "left": {
//                                 "type": "integer"
//                             },
//                             "right": {
//                                 "type": "integer"
//                             },
//                             "top": {
//                                 "type": "integer"
//                             },
//                             "bottom": {
//                                 "type": "integer"
//                             }
//                         },
//                         "required": ["enabled", "left", "right", "top", "bottom"]
//                     },
//                     "ipcm_area_2": {
//                         "type": "object",
//                         "properties": {
//                             "enabled": {
//                                 "type": "boolean"
//                             },
//                             "left": {
//                                 "type": "integer"
//                             },
//                             "right": {
//                                 "type": "integer"
//                             },
//                             "top": {
//                                 "type": "integer"
//                             },
//                             "bottom": {
//                                 "type": "integer"
//                             }
//                         },
//                         "required": ["enabled", "left", "right", "top", "bottom"]
//                     },
//                     "roi_area_1": {
//                         "type": "object",
//                         "properties": {
//                             "enabled": {
//                                 "type": "boolean"
//                             },
//                             "left": {
//                                 "type": "integer"
//                             },
//                             "right": {
//                                 "type": "integer"
//                             },
//                             "top": {
//                                 "type": "integer"
//                             },
//                             "bottom": {
//                                 "type": "integer"
//                             }
//                         },
//                         "required": ["enabled", "left", "right", "top", "bottom"]
//                     },
//                     "roi_area_2": {
//                         "type": "object",
//                         "properties": {
//                             "enabled": {
//                                 "type": "boolean"
//                             },
//                             "left": {
//                                 "type": "integer"
//                             },
//                             "right": {
//                                 "type": "integer"
//                             },
//                             "top": {
//                                 "type": "integer"
//                             },
//                             "bottom": {
//                                 "type": "integer"
//                             }
//                         },
//                         "required": ["enabled", "left", "right", "top", "bottom"]
//                     }
//                 },
//                 "required": ["add_sei_messages", "deblocking_filter", "intra_area", "ipcm_area_1", "ipcm_area_2", "roi_area_1", "roi_area_2"]
//             },
//             "rate_control":
//                 "type": "object",
//                 "properties": {
//                     "picture_rc": {
//                         "type": "boolean"
//                     },
//                     "picture_skip": {
//                         "type": "boolean"
//                     },
//                     "ctb_rc": {
//                         "type": "boolean"
//                     },
//                     "block_rc_size": {
//                         "type": "integer"
//                     },
//                     "hrd": {
//                         "type": "object",
//                         "properties": {
//                             "enabled": {
//                                 "type": "boolean"
//                             },
//                             "cpb_size": {
//                                 "type": "integer"
//                             }
//                         },
//                         "required": ["enabled", "cpb_size"]
//                     },
//                     "quantization": {
//                         "type": "object",
//                         "properties": {
//                             "qp_min": {
//                                 "type": "integer"
//                             },
//                             "qp_max": {
//                                 "type": "integer"
//                             },
//                             init_qp: {
//                                 "type": "integer"
//                             },
//                             "intra_qp_delta": {
//                                 "type": "integer"
//                             },
//                             "fixed_intra_qp": {
//                                 "type": "integer"
//                             }
//                         },
//                         "required": ["qp_min", "qp_max", "init_qp", "intra_qp_delta", "fixed_intra_qp"]
//                     },
//                     "bitrate": {
//                         "type": "object",
//                         "properties": {
//                             "target_bitrate": {
//                                 "type": "integer"
//                             },
//                             "bitvar_range_i": {
//                                 "type": "integer"
//                             },
//                             "bitvar_range_p": {
//                                 "type": "integer"
//                             },
//                             "bitvar_range_b": {
//                                 "type": "integer"
//                             },
//                             "tolerance_moving_bitrate": {
//                                 "type": "integer"
//                             }
//                         },
//                         "required": ["target_bitrate", "bitvar_range_i", "bitvar_range_p", "bitvar_range_b", "tolerance_moving_bitrate"]
//                     },
//                     "monitor_frames": {
//                         "type": "integer"
//                     },
//                     "gop_length": {
//                         "type": "integer"
//                     },
//                 },
//                 "required": ["picture_rc", "picture_skip", "ctb_rc", "block_rc_size", "hrd", "quantization", "bitrate", "monitor_frames", "gop_length"]
//             }
//         },
//         "required": ["config", "gop_config", "coding_control", "rate_control"]
//     })"""";
// }