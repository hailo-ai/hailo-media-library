#include "clip_pipeline.hpp"

#include "clip_app_config_parser.hpp"
#include "common/common.hpp"
#include "resources/configs.hpp"
#include "resources/webrtc.hpp"
#include "hailo_analytics/pipeline/sinks/app_sink_stage.hpp"
#include "service/app_control_service_ext.hpp"
#include "service/player_service_ext.hpp"
#include "service/query_service/query_service_ext.hpp"
#include "service/storage_cleanup_service_ext.hpp"
#include "service/storage_monitor_service_ext.hpp"
#include "streaming/mkv_concatenator.hpp"
#include <httplib.h>
#include <fstream>
#include <iostream>

#define TEE_STAGE "vision_tee"

#define CLIP_AI_STREAM_ID "clip_ai"
#define CLIP_VGA_STREAM_ID "clip_vga"

using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline::overlay;
using namespace hailo_analytics::pipeline::routing;
using namespace hailo_analytics::analytics::app_constructor;
using namespace webserver::pipeline;
using namespace webserver::resources;
using namespace app;

// Endpoint route constants
namespace clip_endpoints
{
const std::string CONFIG_EMBEDDED_REFRESH = "/clip/config/embedded-refresh";
const std::string CONFIG_VIDEO_PLAYBACK_TOTAL_LENGTH = "/clip/config/video-playback-total-length";
const std::string NETWORKS = "/clip/networks";
const std::string EMBEDDING = "/clip/embedding";
const std::string VIDEO_THUMBNAIL_CLICKED = "/clip/video-thumbnail-clicked";
const std::string VIDEO_SEGMENTS_DOWNLOAD = "/clip/video-segments/download";
const std::string VIDEO_THUMBNAIL_STOP = "/clip/video-thumbnail-stop";
const std::string STORAGE_STATUS = "/clip/storage/status";

const std::vector<std::string> ALL_ENDPOINTS = {
    CONFIG_EMBEDDED_REFRESH,
    CONFIG_VIDEO_PLAYBACK_TOTAL_LENGTH,
    NETWORKS,
    EMBEDDING,
    VIDEO_THUMBNAIL_CLICKED,
    VIDEO_SEGMENTS_DOWNLOAD,
    VIDEO_THUMBNAIL_STOP,
    STORAGE_STATUS,
};
} // namespace clip_endpoints

ClipPipeline::ClipPipeline(webserver::resources::ResourceRepository &resources, MediaLibraryPtr media_library,
                           RTPConverterStage &webrtc_stage, Architecture platform)
    : BasePipeline(resources, media_library, webrtc_stage, platform, ProfileType::Daylight, {ProfileType::Daylight})
{
    m_stream_4k_name = DEFAULT_STREAM_4K_NAME;

    //  change clip internals stream IDs to match profiles
    app::stream_id::highres = DEFAULT_STREAM_4K_NAME;
    app::stream_id::stream_vga = CLIP_VGA_STREAM_ID;
    app::stream_id::stream_ai = CLIP_AI_STREAM_ID;
}

bool ClipPipeline::is_supported(webserver::resources::ResourceRepository &resources)
{
    // Verify Clip profile is available in media library
    auto config = std::static_pointer_cast<ConfigResourceMedialib>(resources.get(RESOURCE_CONFIG_MANAGER));
    tl::expected<nlohmann::json, std::string> profile = config->get_profile(CLIP_PROFILE_NAME);
    if (!profile.has_value())
    {
        WEBSERVER_LOG_WARNING("ClipPipeline not supported: Profile {} not found in media library configuration",
                              CLIP_PROFILE_NAME);
        return false;
    }

    // verify pipeline is supported by system
    ClipAppConfigParser config_parser;
    if (!config_parser.parse_from_file(paths::clip_app_config))
    {
        std::cerr << "Unable to load " << paths::clip_app_config << std::endl;
        throw std::runtime_error("Failed to load clip app config");
    }
    bool system_support = ClipVideoPipeline::is_supported(config_parser.get_config().storage_config);
    if (!system_support)
    {
        WEBSERVER_LOG_WARNING("ClipPipeline not supported: System does not meet ClipVideoPipeline requirements");
        return false;
    }

    return true;
}

std::string ClipPipeline::pipeline_name() const
{
    return "Clip";
}

std::string ClipPipeline::get_profile_name_by_type(ProfileType type) const
{
    switch (type)
    {
    case ProfileType::Daylight:
        return CLIP_PROFILE_NAME;
    default:
        throw std::runtime_error("profile type not supported in CLIP Pipeline");
    }
}

ProfileType ClipPipeline::get_profile_type_by_name(const std::string &name) const
{
    if (name == CLIP_PROFILE_NAME)
        return ProfileType::Daylight;
    else
        throw std::runtime_error("profile name not supported in CLIP Pipeline");
}

// Base64 encode function
inline std::string base64_encode(const std::string &data)
{
    static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : data)
    {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0)
        {
            out.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

// Load JPEG file and convert to base64
inline tl::expected<std::string, ImageError> loadJpegFile(const std::string &filepath)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open())
    {
        return tl::unexpected(ImageError::FILE_NOT_FOUND);
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    if (content.empty())
    {
        return tl::unexpected(ImageError::INVALID_FORMAT);
    }

    std::string encoded = base64_encode(content);
    if (encoded.empty())
    {
        return tl::unexpected(ImageError::ENCODING_ERROR);
    }

    return encoded;
}

// Add image to gallery
tl::expected<void, app::ImageError> ClipPipeline::addImage(const std::string &jpeg_path, const std::string &description,
                                                           int64_t timestamp, float score)
{
    auto encoded_result = loadJpegFile(jpeg_path);
    if (!encoded_result)
    {
        return tl::unexpected(encoded_result.error());
    }

    ImageData image;
    image.jpeg_data = encoded_result.value();
    image.description = description;
    image.timestamp = timestamp;
    image.score = score;

    m_images.push_back(image);
    return {};
}

void ClipPipeline::clearAllImages()
{
    m_images.clear();
}

void ClipPipeline::build_pipeline()
{
    WEBSERVER_LOG_INFO("Building clip pipeline");

    std::shared_ptr<webserver::resources::WebRtcResource> webrtc_resource =
        std::static_pointer_cast<WebRtcResource>(m_resources.get(RESOURCE_WEBRTC));

    std::shared_ptr<AppSinkStage> main_sink_stage =
        AppSinkStageBuild::create()
            .set_stage_name("main_sink")
            .set_queue_size_opt(1)
            .set_leaky_opt(false)
            .set_process_func([&](hailo_analytics::pipeline::BufferPtr buf) { m_webrtc_stage.process(buf); })
            .buildptr();

    // Create App
    auto app_config = AppConfigOverride();
    app_config.m_user_data =
        std::make_shared<ClipAppCustomData>(m_clip_app_config->clip_image_encoders, m_clip_app_config->pipeline_config,
                                            m_clip_app_config->hailort_device_config, m_clip_app_config->storage_config,
                                            m_clip_app_config->text_encoder_support_list,
                                            m_clip_app_config->faiss_config, main_sink_stage, webrtc_resource, true);

    if (m_clip_app_config->frontend_source_from_file.enabled)
    {
        WEBSERVER_LOG_ERROR("CLIP streaming from file is not supported in HCV");
        throw std::runtime_error("CLIP streaming from file is not supported in HCV");
    }

    CameraAppConstructor::InitializerParams params;
    params.media_library_component = m_app_resources->media_library.get();
    params.initialize_media_library_configuration = false;
    params.initialize_media_library_profile = false;

    auto app = CameraAppConstructor::create<ClipVideoPipeline>(app_config, params);
    if (!app)
    {
        throw std::runtime_error("Failed to create ClipVideoPipeline: " + app.error());
    }

    m_app = app.value();

    // Get/Check query video player streamer extension
    auto query_player_ext = m_app->get_extension<VideoStreamingServiceExt>();
    if (!query_player_ext)
    {
        throw std::runtime_error("VideoStreamingServiceExt extension is required but cannot be found");
    }
}

void ClipPipeline::init(ProfileType profile_type)
{
    // Base initialization
    BasePipeline::init(profile_type);

    // Load clip app config
    WEBSERVER_LOG_INFO("Loading CLIP application configuration");
    ClipAppConfigParser config_parser;
    if (!config_parser.parse_from_file(paths::clip_app_config))
    {
        std::cerr << "Unable to load " << paths::clip_app_config << std::endl;
        throw std::runtime_error("Failed to load clip app config");
    }
    m_clip_app_config = std::make_shared<ClipAppConfig>(config_parser.get_config());
    WEBSERVER_LOG_INFO("CLIP Pipeline initialized successfully");
}

void ClipPipeline::start()
{
    WEBSERVER_LOG_INFO("Starting CLIP Pipeline");
    build_pipeline();

    m_app->start();

    // Create pipeline
    sleep(1);
    auto encoder_resource = std::static_pointer_cast<EncoderResource>(m_resources.get(RESOURCE_ENCODER));
    encoder_resource->set_encoder_query([this]() { return this->get_encoder_config(); });

    m_resources.m_event_bus->notify(EventType::RESET_ISP, std::make_shared<EmptyState>(EmptyState()));

    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    config_profile_t current_profile = expected_profile.value();
    ProfileStateData current_profile_data{current_profile, this->get_profile_type_by_name(current_profile.name),
                                          current_profile.name, this->get_supported_profiles()};
    m_resources.m_event_bus->notify(EventType::PIPELINE_READY,
                                    std::make_shared<ProfileState>(ProfileState(current_profile_data)));

    WEBSERVER_LOG_INFO("CLIP Pipeline started successfully");
}

void ClipPipeline::stop()
{
    WEBSERVER_LOG_INFO("Stopping CLIP Pipeline");

    if (m_app)
    {
        // Stop storage services BEFORE stopping the pipeline to avoid deadlock.
        // The cleanup worker thread may hold an exclusive lock on the Faiss database
        // (m_rw_mutex via remove_partition/rebuild_search_index), which blocks pipeline
        // stage threads that need a shared lock for search/insert. If we call
        // Pipeline::stop() first (via m_app->stop()), it tries to join those blocked
        // stage threads and deadlocks because the cleanup worker is never told to stop.
        auto storage_monitor = m_app->get_extension<StorageMonitorServiceExt>();
        if (storage_monitor)
        {
            WEBSERVER_LOG_INFO("Stopping StorageMonitorServiceExt before pipeline shutdown");
            storage_monitor->stop();
        }
        auto storage_cleanup = m_app->get_extension<StorageCleanupServiceExt>();
        if (storage_cleanup)
        {
            WEBSERVER_LOG_INFO("Stopping StorageCleanupServiceExt before pipeline shutdown");
            storage_cleanup->stop();
        }

        m_app->stop();
        m_app->release();
        WEBSERVER_LOG_INFO("Clip app stopped and released successfully");
    }
}

void ClipPipeline::register_endpoints()
{
    BasePipeline::register_endpoints();

    WEBSERVER_LOG_INFO("Registering CLIP Pipeline endpoints");
    register_config_embedded_refresh_endpoint();
    register_config_video_playback_total_length_endpoint();
    register_networks_endpoint();
    register_embedding_endpoint();
    register_video_thumbnail_clicked_endpoint();
    register_video_segments_download_endpoint();
    register_video_thumbnail_stop_endpoint();
    register_storage_status_endpoint();
}

void ClipPipeline::register_config_embedded_refresh_endpoint()
{
    m_resources.m_srv.Get(clip_endpoints::CONFIG_EMBEDDED_REFRESH, std::function<nlohmann::json()>([this]() {
                              WEBSERVER_LOG_INFO("GET {} called", clip_endpoints::CONFIG_EMBEDDED_REFRESH);
                              if (!m_app)
                              {
                                  WEBSERVER_LOG_ERROR("ClipVideoPipeline is not available");
                                  throw std::runtime_error("ClipVideoPipeline not available");
                              }
                              auto app_controlservice_ext = m_app->get_extension<AppControlServiceExt>();
                              if (!app_controlservice_ext)
                              {
                                  WEBSERVER_LOG_ERROR("AppControlServiceExt extension is not available");
                                  throw std::runtime_error("AppControlServiceExt extension not available");
                              }
                              nlohmann::json j;
                              j["rate"] = app_controlservice_ext->get_clip_embedding_refresh_rate().value();
                              WEBSERVER_LOG_INFO("GET {} completed", clip_endpoints::CONFIG_EMBEDDED_REFRESH);
                              return j;
                          }));

    // Receives a new refresh rate, validates it, and updates the setting.
    m_resources.m_srv.Post(clip_endpoints::CONFIG_EMBEDDED_REFRESH,
                           std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                               WEBSERVER_LOG_INFO("POST {} called", clip_endpoints::CONFIG_EMBEDDED_REFRESH);
                               int new_rate = j_body["rate"];
                               if (!m_app)
                               {
                                   WEBSERVER_LOG_ERROR("ClipVideoPipeline is not available");
                                   throw std::runtime_error("ClipVideoPipeline not available");
                               }
                               auto app_controlservice_ext = m_app->get_extension<AppControlServiceExt>();
                               if (!app_controlservice_ext)
                               {
                                   WEBSERVER_LOG_ERROR("AppControlServiceExt extension is not available");
                                   throw std::runtime_error("AppControlServiceExt extension not available");
                               }

                               if (auto result = app_controlservice_ext->set_clip_embedding_refresh_rate(new_rate);
                                   !result)
                               {
                                   WEBSERVER_LOG_ERROR("Failed to set new refresh rate: {}", result.error());
                                   throw std::runtime_error("Failed to set new refresh rate");
                               }
                               nlohmann::json j;
                               j["new_rate"] = new_rate;
                               return j;
                           }));
}

void ClipPipeline::register_config_video_playback_total_length_endpoint()
{
    m_resources.m_srv.Get(clip_endpoints::CONFIG_VIDEO_PLAYBACK_TOTAL_LENGTH, std::function<nlohmann::json()>([this]() {
                              WEBSERVER_LOG_INFO("GET {} called", clip_endpoints::CONFIG_VIDEO_PLAYBACK_TOTAL_LENGTH);
                              if (!m_app)
                              {
                                  WEBSERVER_LOG_ERROR("ClipVideoPipeline is not available");
                                  throw std::runtime_error("ClipVideoPipeline not available");
                              }
                              auto clip_query_service = m_app->get_extension<ClipQueryServiceExt>();
                              if (!clip_query_service)
                              {
                                  WEBSERVER_LOG_ERROR("ClipQueryService extension is not available");
                                  throw std::runtime_error("ClipQueryService extension not available");
                              }

                              nlohmann::json response;
                              response["total_length"] = clip_query_service->get_query_video_total_length().value() /
                                                         1000; // divide to seconds;
                              return response;
                          }));

    m_resources.m_srv.Post(
        clip_endpoints::CONFIG_VIDEO_PLAYBACK_TOTAL_LENGTH,
        std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
            WEBSERVER_LOG_INFO("POST {} called", clip_endpoints::CONFIG_VIDEO_PLAYBACK_TOTAL_LENGTH);

            int64_t new_total_length = j_body["total_length"].get<int64_t>() * 1000; // Convert seconds to milliseconds
            if (!m_app)
            {
                WEBSERVER_LOG_ERROR("ClipVideoPipeline is not available");
                throw std::runtime_error("ClipVideoPipeline not available");
            }
            auto clip_query_service = m_app->get_extension<ClipQueryServiceExt>();
            if (!clip_query_service)
            {
                WEBSERVER_LOG_ERROR("ClipQueryService extension is not available");
                throw std::runtime_error("ClipQueryService extension not available");
            }

            auto result = clip_query_service->set_query_video_total_length(new_total_length);
            if (!result)
            {
                WEBSERVER_LOG_ERROR("Failed to set new video playback total length: {}", result.error());
                throw std::runtime_error("Failed to set new video playback total length");
            }

            nlohmann::json j;
            j["new_total_length"] = new_total_length;
            return j;
        }));
}

void ClipPipeline::register_networks_endpoint()
{
    m_resources.m_srv.Get(clip_endpoints::NETWORKS, std::function<nlohmann::json()>([this]() {
                              WEBSERVER_LOG_INFO("GET {} called", clip_endpoints::NETWORKS);
                              nlohmann::json response = nlohmann::json::array();
                              for (const auto &network : m_clip_app_config->text_encoder_support_list)
                              {
                                  nlohmann::json network_obj = {
                                      {"name", network.network_name},
                                      {"id", network.network_id}, // Convert name to ID
                                      {"onnx_file_path", network.network_text_enc_onnx_file_path},
                                      {"embedding_size", network.network_embedding_size},
                                      {"context_length", network.network_context_length},
                                  };
                                  response.push_back(network_obj);
                              }
                              return response;
                          }));
}

void ClipPipeline::register_embedding_endpoint()
{
    m_resources.m_srv.Post(clip_endpoints::EMBEDDING,
                           std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                               WEBSERVER_LOG_INFO("POST {} called", clip_endpoints::EMBEDDING);

                               EmbeddingInfo embedding_info;

                               // Parse positive prompt
                               if (j_body.contains("positive_prompt"))
                               {
                                   auto &pos_json = j_body["positive_prompt"];
                                   embedding_info.positive_embedding.prompt = pos_json["text"];
                                   embedding_info.positive_embedding.embedding =
                                       pos_json["embedding"].get<std::vector<float>>();
                               }

                               // Parse negative prompts
                               if (j_body.contains("negative_prompts"))
                               {
                                   for (const auto &neg_json : j_body["negative_prompts"])
                                   {
                                       EmbeddingInfo::EmbeddingData neg_data;
                                       neg_data.prompt = neg_json["text"];
                                       neg_data.embedding = neg_json["embedding"].get<std::vector<float>>();
                                       embedding_info.negative_embeddings.push_back(neg_data);
                                   }
                               }

                               // Parse text decode on device flag
                               embedding_info.text_decode_on_device =
                                   j_body.value("text_decode_on_device", embedding_info.text_decode_on_device);

                               nlohmann::json response;
                               clearAllImages();
                               if (!processEmbedding(embedding_info))
                               {
                                   WEBSERVER_LOG_INFO("No embedding query results found");
                                   throw std::runtime_error("No embedding query results found");
                               }
                               response["status"] = "success";
                               nlohmann::json image_array = nlohmann::json::array();

                               for (const auto &image : m_images)
                               {
                                   nlohmann::json img_obj;
                                   img_obj["jpeg_data"] = "data:image/jpeg;base64," + image.jpeg_data;
                                   img_obj["description"] = image.description;
                                   img_obj["timestamp"] = image.timestamp;
                                   img_obj["score"] = image.score;
                                   image_array.push_back(img_obj);
                               }

                               response["images"] = image_array;
                               return response;
                           }));
}

void ClipPipeline::register_video_thumbnail_clicked_endpoint()
{
    m_resources.m_srv.Post(
        clip_endpoints::VIDEO_THUMBNAIL_CLICKED,
        std::function<void(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
            WEBSERVER_LOG_INFO("POST {} called", clip_endpoints::VIDEO_THUMBNAIL_CLICKED);

            if (!j_body.contains("timestamp") || !j_body.contains("description"))
            {
                WEBSERVER_LOG_ERROR("Invalid request: missing timestamp or description");
                throw std::runtime_error("Invalid request: missing timestamp or description");
            }

            int64_t timestamp = j_body["timestamp"].get<int64_t>();
            std::string description = j_body["description"].get<std::string>();

            WEBSERVER_LOG_INFO("Image clicked - Timestamp: {}, Description: {}", timestamp, description);
            if (!m_app)
            {
                WEBSERVER_LOG_ERROR("ClipVideoPipeline is not available");
                throw std::runtime_error("ClipVideoPipeline not available");
            }
            auto clip_query_service = m_app->get_extension<ClipQueryServiceExt>();
            if (!clip_query_service)
            {
                WEBSERVER_LOG_ERROR("ClipQueryService extension is not available");
                throw std::runtime_error("ClipQueryService extension not available");
            }
            auto video_query_result = clip_query_service->query_videos(timestamp);
            std::vector<VideoFile> video_files;
            if (!video_query_result.has_value())
            {
                WEBSERVER_LOG_INFO("No video found for timestamp: {}", timestamp);
                throw std::runtime_error("No video found for query timestamp");
            }

            if (video_query_result.value().empty())
            {
                WEBSERVER_LOG_INFO("The video file is no longer available for timestamp: {}", timestamp);
                throw std::runtime_error("The video file is no longer available. It may have been automatically "
                                         "removed from storage to make room for new recordings");
            }

            // Convert VideoQueryResult to VideoFile vector
            for (const auto &video : video_query_result.value())
            {
                video_files.emplace_back(video.m_video_path, video.m_timestamp_start, video.m_duration);
            }
            auto query_player_ext = m_app->get_extension<VideoStreamingServiceExt>();
            if (!query_player_ext)
            {
                WEBSERVER_LOG_ERROR("VideoStreamingServiceExt extension is not available");
                throw std::runtime_error("VideoStreamingServiceExt extension not available");
            }

            query_player_ext->start_streaming(video_files);
            WEBSERVER_LOG_INFO("Video streaming for clicked thumbnail started");
            return;
        }));
}

void ClipPipeline::register_video_segments_download_endpoint()
{
    m_resources.m_srv.Get(clip_endpoints::VIDEO_SEGMENTS_DOWNLOAD,
                          [this](const httplib::Request &req, httplib::Response &res) {
                              WEBSERVER_LOG_INFO("GET {} called", clip_endpoints::VIDEO_SEGMENTS_DOWNLOAD);

                              if (!req.has_param("timestamp"))
                              {
                                  res.status = 400;
                                  nlohmann::json error_response;
                                  error_response["status"] = "error";
                                  error_response["message"] = "Missing required parameter: timestamp";
                                  res.set_content(error_response.dump(), "application/json");
                                  return;
                              }

                              int64_t timestamp = std::stoll(req.get_param_value("timestamp"));

                              if (!m_app)
                              {
                                  res.status = 500;
                                  nlohmann::json error_response;
                                  error_response["status"] = "error";
                                  error_response["message"] = "ClipVideoPipeline not available";
                                  res.set_content(error_response.dump(), "application/json");
                                  return;
                              }

                              auto clip_query_service = m_app->get_extension<ClipQueryServiceExt>();
                              if (!clip_query_service)
                              {
                                  res.status = 500;
                                  nlohmann::json error_response;
                                  error_response["status"] = "error";
                                  error_response["message"] = "ClipQueryService extension not available";
                                  res.set_content(error_response.dump(), "application/json");
                                  return;
                              }

                              auto video_query_result = clip_query_service->query_videos(timestamp);
                              if (!video_query_result.has_value() || video_query_result.value().empty())
                              {
                                  res.status = 500;
                                  nlohmann::json error_response;
                                  error_response["status"] = "error";
                                  error_response["message"] = "No video segments found for the given timestamp";
                                  res.set_content(error_response.dump(), "application/json");
                                  return;
                              }

                              std::vector<std::string> file_paths;
                              for (const auto &segment : video_query_result.value())
                              {
                                  file_paths.push_back(segment.m_video_path);
                              }

                              std::string filename = "clip_" + std::to_string(timestamp) + ".mkv";

                              auto concatenator = std::make_shared<MkvConcatenator>();
                              if (!concatenator->start(file_paths))
                              {
                                  res.status = 500;
                                  nlohmann::json error_response;
                                  error_response["status"] = "error";
                                  error_response["message"] = "Failed to start video concatenation pipeline";
                                  res.set_content(error_response.dump(), "application/json");
                                  return;
                              }

                              res.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");

                              res.set_chunked_content_provider(
                                  "video/x-matroska",
                                  [concatenator](size_t /*offset*/, httplib::DataSink &sink) -> bool {
                                      auto chunk = concatenator->pull_chunk();
                                      if (concatenator->is_done() && chunk.empty())
                                      {
                                          sink.done();
                                          return true;
                                      }
                                      if (chunk.empty())
                                      {
                                          return true;
                                      }
                                      return sink.write(reinterpret_cast<const char *>(chunk.data()), chunk.size());
                                  },
                                  [concatenator](bool /*success*/) { concatenator->stop(); });
                          });
}

void ClipPipeline::register_video_thumbnail_stop_endpoint()
{
    m_resources.m_srv.Post(clip_endpoints::VIDEO_THUMBNAIL_STOP,
                           std::function<void(const nlohmann::json &)>([this](const nlohmann::json & /*j_body*/) {
                               WEBSERVER_LOG_INFO("POST {} called", clip_endpoints::VIDEO_THUMBNAIL_STOP);
                               if (!m_app)
                               {
                                   WEBSERVER_LOG_ERROR("ClipVideoPipeline is not available");
                                   throw std::runtime_error("ClipVideoPipeline not available");
                               }
                               auto query_player_ext = m_app->get_extension<VideoStreamingServiceExt>();
                               if (!query_player_ext)
                               {
                                   WEBSERVER_LOG_ERROR("VideoStreamingServiceExt extension is not available");
                                   throw std::runtime_error("VideoStreamingServiceExt extension not available");
                               }
                               query_player_ext->stop_streaming();
                               WEBSERVER_LOG_INFO("Video streaming for clicked thumbnail stopped");
                               return;
                           }));
}

void ClipPipeline::register_storage_status_endpoint()
{

    m_resources.m_srv.Get(clip_endpoints::STORAGE_STATUS, std::function<nlohmann::json()>([this]() {
                              WEBSERVER_LOG_INFO("GET {} called", clip_endpoints::STORAGE_STATUS);
                              if (!m_app)
                              {
                                  WEBSERVER_LOG_ERROR("ClipVideoPipeline is not available");
                                  throw std::runtime_error("ClipVideoPipeline not available");
                              }
                              auto storage_monitor_service = m_app->get_extension<StorageMonitorServiceExt>();
                              if (!storage_monitor_service)
                              {
                                  WEBSERVER_LOG_ERROR("StorageMonitorServiceExt extension is not available");
                                  throw std::runtime_error("StorageMonitorServiceExt extension not available");
                              }

                              json response;
                              // Get storage info from StorageMonitorService
                              auto storage_info = storage_monitor_service->get_storage_info();
                              if (!storage_info.has_value())
                              {
                                  WEBSERVER_LOG_ERROR("Failed to retrieve storage info");
                                  throw std::runtime_error("Failed to retrieve storage info");
                              }

                              response["total_space"] = storage_info->mount_total_space;
                              response["available_space"] = storage_info->mount_free_space;
                              response["used_space"] = storage_info->root_directory_size;

                              // Breakdown of storage usage
                              response["breakdown"]["database"] = storage_info->database_directory_size;
                              response["breakdown"]["faissdb"] = storage_info->faissdb_directory_size;
                              response["breakdown"]["thumbnail"] = storage_info->thumbnail_directory_size;
                              response["breakdown"]["video"] = storage_info->video_directory_size;
                              response["timestamp"] = std::time(nullptr);

                              return response;
                          }));
}

void ClipPipeline::unregister_endpoints()
{
    WEBSERVER_LOG_INFO("Unregistering CLIP Pipeline endpoints");

    // Remove all CLIP-specific endpoints
    for (const auto &endpoint : clip_endpoints::ALL_ENDPOINTS)
    {
        m_resources.m_srv.Unregister(endpoint);
    }

    // Call parent's unregister function
    BasePipeline::unregister_endpoints();
}

bool ClipPipeline::processEmbedding(const EmbeddingInfo &embedding_info)
{
    bool success = false;

    if (!m_app)
    {
        WEBSERVER_LOG_ERROR("ClipVideoPipeline is not available");
        return false;
    }

    ClipQueryServiceExt::QueryEmbeddingInfo query_info;
    query_info.m_max_result = m_clip_app_config->query_defaults.max_query;
    query_info.m_score_threshold = m_clip_app_config->query_defaults.score_threshold;
    query_info.m_network_id = !embedding_info.network_id.empty()
                                  ? embedding_info.network_id
                                  : m_clip_app_config->text_encoder_support_list.front().network_id;
    query_info.m_remove_duplicate_within_sec = m_clip_app_config->query_defaults.remove_duplicate_within_sec;

    // Set both positive and negative embeddings if we are using the encoded embeddings from the client (browser)
    query_info.m_positive_embedding.embedding = embedding_info.positive_embedding.embedding;
    query_info.m_positive_embedding.prompt = embedding_info.positive_embedding.prompt;
    for (const auto &neg : embedding_info.negative_embeddings)
    {
        ClipQueryServiceExt::QueryEmbeddingInfo::EmbeddingData neg_data;
        neg_data.embedding = neg.embedding;
        neg_data.prompt = neg.prompt;
        query_info.m_negative_embeddings.push_back(neg_data);
    }

    // Print all embedding_info for debugging except for embedding vector
    WEBSERVER_LOG_DEBUG("CLIP Pipeline- Received embedding data:");
    if (!query_info.m_network_id.empty())
        WEBSERVER_LOG_DEBUG("  Network ID: {}", query_info.m_network_id);
    WEBSERVER_LOG_DEBUG("  Positive Prompt: {}", query_info.m_positive_embedding.prompt);
    WEBSERVER_LOG_DEBUG("  Positive Embedding Size: {}", query_info.m_positive_embedding.embedding.size());

    if (!query_info.m_negative_embeddings.empty())
    {
        for (const auto &neg : query_info.m_negative_embeddings)
        {
            WEBSERVER_LOG_DEBUG("  Negative Prompt: {}", neg.prompt);
            WEBSERVER_LOG_DEBUG("  Negative Embedding Size: {}", neg.embedding.size());
        }
    }
    else
    {
        WEBSERVER_LOG_DEBUG("  No Negative Prompts from user, will automatically handle during search");
    }

    WEBSERVER_LOG_DEBUG("  Score Threshold: {}", query_info.m_score_threshold);
    WEBSERVER_LOG_DEBUG("  Max Query: {}", query_info.m_max_result);
    WEBSERVER_LOG_DEBUG("  Remove Duplicate Within Sec: {}", query_info.m_remove_duplicate_within_sec);

    if (embedding_info.text_decode_on_device)
    {
        query_info.m_embedding_type = ClipQueryServiceExt::EmbeddingVectorType::DEVICE_TO_ENCODE_TEXT_EMBEDDING;
        WEBSERVER_LOG_DEBUG("  Text Decode On Device: true");
    }
    else
    {
        query_info.m_embedding_type = ClipQueryServiceExt::EmbeddingVectorType::FULLY_ENCODED_TEXT_EMBEDDING;
        WEBSERVER_LOG_DEBUG("  Text Decode On Device: false");
    }

    auto query_player_ext = m_app->get_extension<ClipQueryServiceExt>();
    auto thumb_query_result = query_player_ext->query_thumbnails(query_info);

    if (thumb_query_result)
    {
        WEBSERVER_LOG_DEBUG("CLIP Pipeline- Query results found: {} matches", thumb_query_result.value().size());
        for (const auto &result : thumb_query_result.value())
        {
            addImage(result.m_jpeg_path, result.m_description, result.m_timestamp, result.m_score);
        }
        success = true;
    }
    else
    {
        WEBSERVER_LOG_DEBUG("CLIP Pipeline- No results found: {}", thumb_query_result.error());
    }

    return success;
}
