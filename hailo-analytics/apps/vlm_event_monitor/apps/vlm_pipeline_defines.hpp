#pragma once

#include <string>

namespace vlm_app
{

namespace paths
{
inline const std::string medialib_config = "/etc/imaging/cfg/medialib_configs/vlm_event_monitor_medialib_config.json";

inline const std::string vlm_app_config = "/home/root/apps/vlm_event_monitor/resources/configs/vlm_app_config.yaml";
inline const std::string webfrontend_dir = "/home/root/apps/vlm_event_monitor/resources/webfrontend";
inline const std::string webfrontend_index = webfrontend_dir + "/index.html";
} // namespace paths

namespace medialib_profile
{
// Default profile name expected in the medialib config
inline const std::string daylight = "Daylight";
} // namespace medialib_profile

namespace stream_id
{
// Stream IDs as declared in the medialib profile's application_settings.json
inline const std::string stream_4k = "HighRes";   // H.264 → WebRTC
inline const std::string stream_vga = "VGA";      // NV12 → JPEG encoder
inline const std::string stream_336 = "VlmInput"; // 336x336 NV12 LETTERBOX_MIDDLE → VLM
} // namespace stream_id

namespace stage
{
// 4K WebRTC path
inline const std::string main_4k_webrtc = "main_4k_webrtc_stage";

// VGA -> JPEG
inline const std::string vga_jpeg_ring = "vga_jpeg_ring_stage";

// 336x336 NV12 -> RGB conversion
inline const std::string nv12_to_rgb = "nv12_to_rgb_stage";
} // namespace stage

} // namespace vlm_app
