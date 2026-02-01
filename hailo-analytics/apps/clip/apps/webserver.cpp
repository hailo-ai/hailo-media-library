#include "webserver.hpp"

// general includes
#include <functional>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <algorithm>
#include <chrono>
#include <filesystem>

#include "hailo_analytics/pipeline/sinks/rtp_converter_stage.hpp"
#include "clip_pipeline_ai.hpp"
#include "clip_pipeline_ai_defines.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace app;

using hailo_analytics::analytics::app_constructor::AppConfigOverride;
using hailo_analytics::analytics::app_constructor::CameraAppConstructor;

#define RTP_RECV_AS_WEBRTC_STREAMR_EXT(obj) (std::static_pointer_cast<WebRTCStreamerExt>(obj))

constexpr const char *WEBFRONTEND_PATH = "/home/root/apps/clip/resources/webfrontend/index.html";

// WebRtcStreamers constructor implementation
IntegratedWebServer::WebRtcStreamers::WebRtcStreamers(
    StreamType t, const std::string &id,
    std::shared_ptr<hailo_analytics::pipeline::sinks::RTPConverterStage::RTPReceiver> ext)
    : type(t), session_id(id), streamer_ext(ext)
{
}

// Private constructor
IntegratedWebServer::IntegratedWebServer(ClipAppConfig config)
    : server(std::make_unique<httplib::Server>()), web_server_config(std::make_unique<ClipAppConfig>(config))
{
}

// Base64 encode function
std::string IntegratedWebServer::base64_encode(const std::string &data)
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
tl::expected<std::string, ImageError> IntegratedWebServer::loadJpegFile(const std::string &filepath)
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
tl::expected<void, ImageError> IntegratedWebServer::addImage(const std::string &jpeg_path,
                                                             const std::string &description, int64_t timestamp,
                                                             float score)
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

void IntegratedWebServer::clearAllImages()
{
    m_images.clear();
}

// Helper function to search the type of WebRTC streamer
std::shared_ptr<IntegratedWebServer::WebRtcStreamers> IntegratedWebServer::findWebRtcStreamer(
    WebRtcStreamers::StreamType type)
{
    auto it = std::find_if(m_webrtc_streamers.begin(), m_webrtc_streamers.end(),
                           [type](const WebRtcStreamers &streamer) { return streamer.type == type; });
    if (it != m_webrtc_streamers.end())
    {
        return std::make_shared<WebRtcStreamers>(*it);
    }
    return nullptr;
}

// Helper function to search the session ID of WebRTC streamer
std::shared_ptr<IntegratedWebServer::WebRtcStreamers> IntegratedWebServer::findWebRtcStreamerBySessionId(
    const std::string &session_id)
{
    auto it =
        std::find_if(m_webrtc_streamers.begin(), m_webrtc_streamers.end(),
                     [&session_id](const WebRtcStreamers &streamer) { return streamer.session_id == session_id; });
    if (it != m_webrtc_streamers.end())
    {
        return std::make_shared<WebRtcStreamers>(*it);
    }
    return nullptr;
}

// Static factory method
tl::expected<std::shared_ptr<IntegratedWebServer>, std::string> IntegratedWebServer::create(const ClipAppConfig &config)
{
    // Use make_shared with a custom deleter approach since constructor is private
    auto instance = std::shared_ptr<IntegratedWebServer>(new IntegratedWebServer(config));

    // WebServer initialization
    instance->setupRoutes();
    instance->setupCORS();
    instance->validateModelFiles();

    // Create App
    auto app_config = AppConfigOverride();
    app_config.m_user_data = std::make_shared<ClipAppCustomData>(
        instance->web_server_config->clip_image_encoders, instance->web_server_config->pipeline_config,
        instance->web_server_config->hailort_device_config, instance->web_server_config->storage_config,
        instance->web_server_config->text_encoder_support_list, instance->web_server_config->faiss_config);

    if (instance->web_server_config->frontend_source_from_file.enabled)
    {
        // Override media config to play from file
        app_config.m_media_config_path = app::paths::medialib_config_play_from_file;

        // Set the file that we want to play from
        app_config.m_appsrc_file_path = instance->web_server_config->frontend_source_from_file.file_path;
        if (!std::filesystem::exists(app_config.m_appsrc_file_path))
        {
            return tl::make_unexpected("Stream source file does not exist under " + app_config.m_appsrc_file_path);
        }
    }

    auto app = CameraAppConstructor::create<ClipVideoPipeline>(app_config);
    if (!app)
    {
        return tl::make_unexpected("Failed to create ClipVideoPipeline application");
    }

    instance->m_app = app.value();

    // Get/Check Storage Monitor Service extension
    auto storage_monitor_service_ext = instance->m_app->get_extension<StorageMonitorServiceExt>();
    if (!storage_monitor_service_ext)
    {
        return tl::make_unexpected("StorageMonitorServiceExt extension is required but cannot be found");
    }
    instance->storage_monitor_service = storage_monitor_service_ext;

    // Get/Check Query Service extension
    auto query_service_ext = instance->m_app->get_extension<ClipQueryServiceExt>();
    if (!query_service_ext)
    {
        return tl::make_unexpected("ClipQueryServiceExt extension is required but cannot be found");
    }
    instance->clip_query_service = query_service_ext;

    // Get/Check WebRTC streamer extension
    auto webrtc_streamer_ext = instance->m_app->get_extension<WebRTCStreamerExt>();
    if (!webrtc_streamer_ext)
    {
        return tl::make_unexpected("WebRTCStreamerExt extension is required but cannot be found");
    }

    // Register WebRTC streamer for main live stream
    instance->m_webrtc_streamers.emplace_back(WebRtcStreamers::StreamType::MAIN_LIVE,
                                              webrtc_streamer_ext->get_session_id(), webrtc_streamer_ext);

    // Get/Check query video player streamer extension
    auto query_player_ext = instance->m_app->get_extension<VideoStreamingServiceExt>();
    if (!query_player_ext)
    {
        return tl::make_unexpected("VideoStreamingServiceExt extension is required but cannot be found");
    }
    instance->clip_query_player_streaming_service = query_player_ext;

    // Register WebRTC streamer for query playback
    instance->m_webrtc_streamers.emplace_back(WebRtcStreamers::StreamType::QUERY_PLAYBACK,
                                              instance->clip_query_player_streaming_service->get_session_id(),
                                              instance->clip_query_player_streaming_service->get_webrtc_streamer());

    return instance;
}

void IntegratedWebServer::validateModelFiles()
{
    std::cout << "Validating ONNX model files..." << std::endl;

    for (const auto &network : web_server_config->text_encoder_support_list)
    {
        const std::string &model_path = network.network_text_enc_onnx_file_path;
        if (fs::exists(model_path))
        {
            std::cout << "✓ Found ONNX model: " << network.network_name << " at " << model_path << std::endl;
        }
        else
        {
            std::cout << "✗ Missing ONNX model (Can still work if text encode on browser is disabled): "
                      << network.network_name << " at " << model_path << std::endl;
        }
    }
}

void IntegratedWebServer::setupCORS()
{
    server->set_pre_routing_handler([]([[maybe_unused]] const httplib::Request &req, httplib::Response &res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return httplib::Server::HandlerResponse::Unhandled;
    });

    server->Options(".*", [](const httplib::Request &, [[maybe_unused]] httplib::Response &res) { return; });
}

void IntegratedWebServer::setupRoutes()
{
    // Serve the main integrated HTML file
    server->Get("/", [this]([[maybe_unused]] const httplib::Request &req, httplib::Response &res) {
        std::cout << "GET /" << std::endl;
        serveIntegratedHTML(res);
    });

    // CLIP API endpoints
    server->Get("/api/status", []([[maybe_unused]] const httplib::Request &req, httplib::Response &res) {
        std::cout << "GET /api/status" << std::endl;
        json response;
        response["status"] = "ok";
        response["timestamp"] =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        res.set_content(response.dump(), "application/json");
    });

    server->Get("/api/config/embedded_refresh",
                [this]([[maybe_unused]] const httplib::Request &req, httplib::Response &res) {
                    std::cout << "GET /api/config/embedded_refresh" << std::endl;
                    json response;

                    // Set header at the start for all response paths
                    res.set_header("Content-Type", "application/json");

                    auto app_controlservice_ext = m_app->get_extension<AppControlServiceExt>();
                    if (!app_controlservice_ext)
                    {
                        std::cerr << "AppControlServiceExt extension is not available" << std::endl;
                        res.status = 500;
                        json error_response;
                        error_response["status"] = "error";
                        error_response["message"] = "AppControlServiceExt extension not available";
                        res.set_content(error_response.dump(), "application/json");
                        return;
                    }

                    response["rate"] = app_controlservice_ext->get_clip_embedding_refresh_rate().value();

                    res.set_content(response.dump(), "application/json");
                    res.status = 200;
                });

    // Receives a new refresh rate, validates it, and updates the setting.
    server->Post("/api/config/embedded_refresh", [this](const httplib::Request &req, httplib::Response &res) {
        std::cout << "POST /api/config/embedded_refresh" << std::endl;
        res.set_header("Content-Type", "application/json");

        try
        {
            json request_body = json::parse(req.body);

            int new_rate = request_body["rate"];

            auto app_controlservice_ext = m_app->get_extension<AppControlServiceExt>();
            if (!app_controlservice_ext)
            {
                std::cerr << "AppControlServiceExt extension is not available" << std::endl;
                res.status = 500;
                json error_response;
                error_response["status"] = "error";
                error_response["message"] = "AppControlServiceExt extension not available";
                res.set_content(error_response.dump(), "application/json");
                return;
            }

            auto result = app_controlservice_ext->set_clip_embedding_refresh_rate(new_rate);
            if (!result)
            {
                std::cerr << "Failed to set new refresh rate: " << result.error() << std::endl;
                res.status = 400; // Bad Request
                json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Invalid refresh rate";
                res.set_content(error_response.dump(), "application/json");
                return;
            }

            json success_response;
            success_response["status"] = "ok";
            success_response["new_rate"] = new_rate;
            res.set_content(success_response.dump(), "application/json");
            res.status = 200; // OK
        }
        catch (const json::parse_error &e)
        {
            json error_response;
            error_response["status"] = "error";
            error_response["message"] = "Invalid JSON format.";
            error_response["details"] = e.what();
            res.set_content(error_response.dump(), "application/json");
            res.status = 400; // Bad Request
        }
    });

    server->Get("/api/config/video_playback_total_length",
                [this]([[maybe_unused]] const httplib::Request &req, httplib::Response &res) {
                    std::cout << "GET /api/config/video_playback_total_length" << std::endl;
                    json response;

                    // Set header at the start for all response paths
                    res.set_header("Content-Type", "application/json");

                    if (!clip_query_service)
                    {
                        std::cerr << "ClipQueryService extension is not available" << std::endl;
                        res.status = 500;
                        json error_response;
                        error_response["status"] = "error";
                        error_response["message"] = "ClipQueryService extension not available";
                        res.set_content(error_response.dump(), "application/json");
                        return;
                    }

                    int64_t total_length = clip_query_service->get_query_video_total_length().value();
                    total_length /= 1000; // Convert to seconds
                    response["total_length"] = total_length;

                    res.set_content(response.dump(), "application/json");
                    res.status = 200;
                });

    server->Post(
        "/api/config/video_playback_total_length", [this](const httplib::Request &req, httplib::Response &res) {
            std::cout << "POST /api/config/video_playback_total_length" << std::endl;
            res.set_header("Content-Type", "application/json");

            try
            {
                json request_body = json::parse(req.body);

                int64_t new_total_length = request_body["total_length"];
                int64_t new_total_length_ms = new_total_length * 1000; // Convert seconds to milliseconds

                if (!clip_query_service)
                {
                    std::cerr << "QueryService extension is not available" << std::endl;
                    res.status = 500;
                    json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "QueryService extension not available";
                    res.set_content(error_response.dump(), "application/json");
                    return;
                }

                auto result = clip_query_service->set_query_video_total_length(new_total_length_ms);
                if (!result)
                {
                    std::cerr << "Failed to set new video playback total length: " << result.error() << std::endl;
                    res.status = 400; // Bad Request
                    json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "Invalid video playback total length";
                    res.set_content(error_response.dump(), "application/json");
                    return;
                }

                json success_response;
                success_response["status"] = "ok";
                success_response["new_total_length"] = new_total_length;
                res.set_content(success_response.dump(), "application/json");
                res.status = 200; // OK
            }
            catch (const json::parse_error &e)
            {
                json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Invalid JSON format.";
                error_response["details"] = e.what();
                res.set_content(error_response.dump(), "application/json");
                res.status = 400; // Bad Request
            }
        });

    server->Get("/api/networks", [this]([[maybe_unused]] const httplib::Request &req, httplib::Response &res) {
        std::cout << "GET /api/networks" << std::endl;
        json response = json::array();
        for (const auto &network : web_server_config->text_encoder_support_list)
        {
            json network_obj = {
                {"name", network.network_name},
                {"id", network.network_id}, // Convert name to ID
                {"onnx_file_path", network.network_text_enc_onnx_file_path},
                {"embedding_size", network.network_embedding_size},
                {"context_length", network.network_context_length},
            };
            response.push_back(network_obj);
        }
        res.set_content(response.dump(), "application/json");
    });

    server->Get("/api/models/([^/]+)", [this](const httplib::Request &req, httplib::Response &res) {
        std::string model_id = req.matches[1];
        std::cout << "GET /api/models/" << model_id << std::endl;

        std::string model_path = web_server_config->get_text_encoder_from_id(model_id)->network_text_enc_onnx_file_path;

        if (!fs::exists(model_path))
        {
            res.status = 404;
            json error_response;
            error_response["error"] = "Model file not found";
            res.set_content(error_response.dump(), "application/json");
            std::cout << "ERROR: Model file not found: " << model_path << std::endl;
            return;
        }

        std::ifstream file(model_path, std::ios::binary);
        if (!file)
        {
            res.status = 500;
            json error_response;
            error_response["error"] = "Failed to read model file";
            res.set_content(error_response.dump(), "application/json");
            std::cout << "ERROR: Failed to read model file: " << model_path << std::endl;
            return;
        }

        // Get file size
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        // Set headers for chunked transfer
        res.set_header("Content-Type", "application/octet-stream");
        res.set_header("Content-Length", std::to_string(file_size));
        res.set_header("Cache-Control", "public, max-age=3600");
        res.set_header("Accept-Ranges", "bytes");

        // Stream the file in chunks
        const size_t chunk_size = 64 * 1024; // 64KB chunks
        res.set_content_provider(
            file_size, "application/octet-stream",
            [model_path, file_size, chunk_size](size_t offset, size_t length, httplib::DataSink &sink) {
                std::ifstream file(model_path, std::ios::binary);
                if (!file)
                {
                    return false;
                }

                file.seekg(offset);
                std::vector<char> buffer(std::min(chunk_size, length));

                size_t remaining = length;
                while (remaining > 0 && file.good())
                {
                    size_t to_read = std::min(chunk_size, remaining);
                    file.read(buffer.data(), to_read);
                    size_t bytes_read = file.gcount();

                    if (bytes_read > 0)
                    {
                        sink.write(buffer.data(), bytes_read);
                        remaining -= bytes_read;

                        // Yield CPU briefly between chunks
                        std::this_thread::sleep_for(std::chrono::microseconds(500));
                    }
                    else
                    {
                        break;
                    }
                }

                return remaining == 0;
            });

        std::cout << "Streaming ONNX model: " << model_path << " (" << file_size << " bytes) in chunks" << std::endl;
    });

    server->Post("/api/embedding", [this](const httplib::Request &req, httplib::Response &res) {
        std::cout << "POST /api/embedding" << std::endl;

        try
        {
            json request_data = json::parse(req.body);

            EmbeddingInfo embedding_info;
            embedding_info.network_id = request_data["network_id"];

            // Parse positive prompt
            if (request_data.contains("positive_prompt"))
            {
                auto &pos_json = request_data["positive_prompt"];
                embedding_info.positive_embedding.prompt = pos_json["text"];
                embedding_info.positive_embedding.embedding = pos_json["embedding"].get<std::vector<float>>();
            }

            // Parse negative prompts
            if (request_data.contains("negative_prompts") && request_data["negative_prompts"].is_array())
            {
                for (const auto &neg_json : request_data["negative_prompts"])
                {
                    EmbeddingInfo::EmbeddingData neg_data;
                    neg_data.prompt = neg_json["text"];
                    neg_data.embedding = neg_json["embedding"].get<std::vector<float>>();
                    embedding_info.negative_embeddings.push_back(neg_data);
                }
            }

            // Parse other query controls
            embedding_info.score_threshold = request_data.value("score_threshold", 0.8f);
            embedding_info.max_query = request_data.value("max_query", 10);
            embedding_info.remove_duplicate_within_sec = request_data.value("remove_duplicate_within_sec", 60);

            // Parse text decode on device flag
            embedding_info.text_decode_on_device =
                request_data.value("text_decode_on_device", embedding_info.text_decode_on_device);

            json response;
            clearAllImages();
            if (processEmbedding(embedding_info))
            {
                response["status"] = "success";
                json image_array = json::array();

                for (const auto &image : m_images)
                {
                    json img_obj;
                    img_obj["jpeg_data"] = image.jpeg_data;
                    img_obj["description"] = image.description;
                    img_obj["timestamp"] = image.timestamp;
                    img_obj["score"] = image.score;
                    image_array.push_back(img_obj);
                }

                response["images"] = image_array;
            }
            else
            {
                response["status"] = "error";
                response["message"] = "Embedding Query Failed";
                res.status = 400;
            }
            res.set_header("Content-Type", "application/json");
            res.set_content(response.dump(), "application/json");
        }
        catch (const std::exception &e)
        {
            json error_response;
            error_response["status"] = "error";
            error_response["message"] = e.what();
            res.status = 400;
            res.set_content(error_response.dump(), "application/json");
        }
    });

    server->Post("/api/video-thumbnail-clicked", [this](const httplib::Request &req, httplib::Response &res) {
        std::cout << "POST /api/video-thumbnail-clicked" << std::endl;

        try
        {
            json root = json::parse(req.body);

            if (!root.contains("timestamp") || !root.contains("description"))
            {
                res.status = 400;
                json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Invalid request";
                res.set_content(error_response.dump(), "application/json");
                return;
            }

            int64_t timestamp = root["timestamp"].get<int64_t>();
            std::string description = root["description"].get<std::string>();
            std::string rtc_session_id = root["session_id"].get<std::string>();

            std::cout << "Image clicked - Timestamp: " << timestamp << ", Description: " << description
                      << ", Session ID:" << rtc_session_id << std::endl;

            auto video_query_result = clip_query_service->query_videos(timestamp);
            std::vector<VideoFile> video_files;
            if (video_query_result.has_value())
            {

                // Convert VideoQueryResult to VideoFile vector
                for (const auto &video : video_query_result.value())
                {
                    video_files.emplace_back(video.m_video_path, video.m_timestamp_start, video.m_duration);
                }

                clip_query_player_streaming_service->start_streaming(video_files);
            }
            else
            {
                std::cout << "No video found for timestamp: " << timestamp << std::endl;
            }

            res.set_header("Content-Type", "application/json");
            json success_response;
            success_response["status"] = "ok";
            res.set_content(success_response.dump(), "application/json");
        }
        catch (const json::exception &e)
        {
            res.status = 400;
            json error_response;
            error_response["status"] = "error";
            error_response["message"] = "JSON parse error";
            error_response["details"] = e.what();
            res.set_content(error_response.dump(), "application/json");
        }
    });

    // WebRTC API endpoints
    server->Post("/api/webrtc/session-live-main",
                 [this]([[maybe_unused]] const httplib::Request &req, httplib::Response &res) {
                     std::cout << "POST /api/webrtc/session-live-main" << std::endl;
                     try
                     {
                         json response;
                         auto webrtc_streamer_ext = findWebRtcStreamer(WebRtcStreamers::StreamType::MAIN_LIVE);
                         if (!webrtc_streamer_ext)
                         {
                             res.status = 400;
                             response["status"] = "error";
                             response["message"] = "WebRTCStreamerExt not found for Main live streaming view";
                             res.set_content(response.dump(), "application/json");
                             std::cerr << "WebRTCStreamerExt not found for Main live streaming view" << std::endl;
                             return;
                         }

                         res.status = 200;
                         response["status"] = "success";
                         response["session_id"] = webrtc_streamer_ext->session_id;
                         res.set_content(response.dump(), "application/json");
                     }
                     catch (const std::exception &e)
                     {
                         res.status = 500;
                         json error_response;
                         error_response["status"] = "error";
                         error_response["message"] = "Invalid request";
                         res.set_content(error_response.dump(), "application/json");
                     }
                 });

    server->Post("/api/webrtc/session-video-thumbnail",
                 [this]([[maybe_unused]] const httplib::Request &req, httplib::Response &res) {
                     std::cout << "POST /api/webrtc/session-video-thumbnail" << std::endl;
                     try
                     {
                         json response;
                         auto webrtc_streamer_ext = findWebRtcStreamer(WebRtcStreamers::StreamType::QUERY_PLAYBACK);
                         if (!webrtc_streamer_ext)
                         {
                             res.status = 400;
                             response["status"] = "error";
                             response["message"] = "WebRTCStreamerExt not found for Main live streaming view";
                             res.set_content(response.dump(), "application/json");
                             std::cerr << "WebRTCStreamerExt not found for Main live streaming view" << std::endl;
                             return;
                         }

                         res.status = 200;
                         response["status"] = "success";
                         response["session_id"] = webrtc_streamer_ext->session_id;
                         res.set_content(response.dump(), "application/json");
                     }
                     catch (const std::exception &e)
                     {
                         res.status = 500;
                         json error_response;
                         error_response["status"] = "error";
                         error_response["message"] = "Invalid request";
                         res.set_content(error_response.dump(), "application/json");
                     }
                 });

    server->Post("/api/webrtc/offer", [this](const httplib::Request &req, httplib::Response &res) {
        std::cout << "POST /api/webrtc/offer" << std::endl;
        try
        {

            auto body = nlohmann::json::parse(req.body);
            std::string session_id = body["session_id"];

            auto webrtc_streamer_ext = findWebRtcStreamerBySessionId(session_id);
            if (!webrtc_streamer_ext)
            {
                res.status = 400;
                json error_response;
                error_response["status"] = "error";
                error_response["message"] = "WebRTCStreamerExt not found for session ID: " + session_id;
                res.set_content(error_response.dump(), "application/json");
                std::cerr << "WebRTCStreamerExt not found for session ID: " << session_id << std::endl;
                return;
            }

            std::string offer = RTP_RECV_AS_WEBRTC_STREAMR_EXT(webrtc_streamer_ext->streamer_ext)->create_offer();
            res.set_content(offer, "application/json");
            res.set_header("Access-Control-Allow-Origin", "*");
        }
        catch (const std::exception &e)
        {
            res.status = 500;
            json error_response;
            error_response["status"] = "error";
            error_response["message"] = "Internal server error";
            error_response["details"] = e.what();
            res.set_content(error_response.dump(), "application/json");
        }
    });

    server->Post("/api/webrtc/answer", [this](const httplib::Request &req, httplib::Response &res) {
        std::cout << "POST /api/webrtc/answer" << std::endl;
        try
        {
            json answerJson = json::parse(req.body);
            std::string session_id = answerJson["session_id"];

            auto webrtc_streamer_ext = findWebRtcStreamerBySessionId(session_id);
            if (!webrtc_streamer_ext)
            {
                res.status = 400;
                json error_response;
                error_response["status"] = "error";
                error_response["message"] = "WebRTCStreamerExt not found for session ID: " + session_id;
                res.set_content(error_response.dump(), "application/json");
                std::cerr << "WebRTCStreamerExt not found for session ID: " << session_id << std::endl;
                return;
            }

            RTP_RECV_AS_WEBRTC_STREAMR_EXT(webrtc_streamer_ext->streamer_ext)->handle_answer(answerJson["sdp"]);
            res.set_content("OK", "text/plain");
            res.set_header("Access-Control-Allow-Origin", "*");
        }
        catch (const std::exception &e)
        {
            res.status = 500;
            json error_response;
            error_response["status"] = "error";
            error_response["message"] = "Internal server error";
            error_response["details"] = e.what();
            res.set_content(error_response.dump(), "application/json");
        }
    });

    server->Post("/api/webrtc/ice-candidate", [this](const httplib::Request &req, httplib::Response &res) {
        std::cout << "POST /api/webrtc/ice-candidate" << std::endl;
        try
        {
            json candidateJson = json::parse(req.body);
            std::string session_id = candidateJson["session_id"];

            auto webrtc_streamer_ext = findWebRtcStreamerBySessionId(session_id);
            if (!webrtc_streamer_ext)
            {
                res.status = 400;
                json error_response;
                error_response["status"] = "error";
                error_response["message"] = "WebRTCStreamerExt not found for session ID: " + session_id;
                res.set_content(error_response.dump(), "application/json");
                std::cerr << "WebRTCStreamerExt not found for session ID: " << session_id << std::endl;
                return;
            }

            // Handle ICE candidate
            RTP_RECV_AS_WEBRTC_STREAMR_EXT(webrtc_streamer_ext->streamer_ext)
                ->handle_ice_candidate(candidateJson["candidate"], candidateJson["sdpMid"],
                                       candidateJson["sdpMLineIndex"]);
            res.set_content("OK", "text/plain");
            res.set_header("Access-Control-Allow-Origin", "*");
        }
        catch (const std::exception &e)
        {
            res.status = 500;
            json error_response;
            error_response["status"] = "error";
            error_response["message"] = "Internal server error";
            error_response["details"] = e.what();
            res.set_content(error_response.dump(), "application/json");
        }
    });

    server->Get("/api/webrtc/status", [this]([[maybe_unused]] const httplib::Request &req, httplib::Response &res) {
        json statusJson;

        auto webrtc_streamer_ext = findWebRtcStreamer(WebRtcStreamers::StreamType::MAIN_LIVE);

        statusJson["connected"] =
            (webrtc_streamer_ext)
                ? !RTP_RECV_AS_WEBRTC_STREAMR_EXT(webrtc_streamer_ext->streamer_ext)->is_connection_closed()
                : false;
        res.set_content(statusJson.dump(), "application/json");
        res.set_header("Access-Control-Allow-Origin", "*");
    });

    server->Post("/api/webrtc/video-thumbnail-stop", [this](const httplib::Request &req, httplib::Response &res) {
        std::cout << "POST /api/webrtc/video-thumbnail-stop" << std::endl;
        auto body = nlohmann::json::parse(req.body);
        std::string session_id = body["session_id"];

        auto webrtc_streamer_ext = findWebRtcStreamerBySessionId(session_id);
        if (!webrtc_streamer_ext)
        {
            res.status = 400;
            json error_response;
            error_response["status"] = "error";
            error_response["message"] = "WebRTCStreamerExt not found for session ID: " + session_id;
            res.set_content(error_response.dump(), "application/json");
            std::cerr << "WebRTCStreamerExt not found for session ID: " << session_id << std::endl;
            return;
        }

        clip_query_player_streaming_service->stop_streaming();
        RTP_RECV_AS_WEBRTC_STREAMR_EXT(webrtc_streamer_ext->streamer_ext)->close_connection();
        res.set_content("Connection closed", "text/plain");
        res.set_header("Access-Control-Allow-Origin", "*");
    });

    server->Get("/api/storage/status", [this]([[maybe_unused]] const httplib::Request &req, httplib::Response &res) {
        json response;

        // Get storage info from StorageMonitorService
        auto storage_info = storage_monitor_service->get_storage_info();
        if (storage_info.has_value())
        {
            response["total_space"] = storage_info->mount_total_space;
            response["available_space"] = storage_info->mount_free_space;
            response["used_space"] = storage_info->root_directory_size;

            // Breakdown of storage usage
            response["breakdown"]["database"] = storage_info->database_directory_size;
            response["breakdown"]["faissdb"] = storage_info->faissdb_directory_size;
            response["breakdown"]["thumbnail"] = storage_info->thumbnail_directory_size;
            response["breakdown"]["video"] = storage_info->video_directory_size;
            response["timestamp"] = std::time(nullptr);
            response["status"] = "success";
        }
        else
        {

            response["total_space"] = "N/A";
            response["available_space"] = "N/A";
            response["used_space"] = "N/A";
            response["breakdown"]["database"] = "N/A";
            response["breakdown"]["faissdb"] = "N/A";
            response["breakdown"]["thumbnail"] = "N/A";
            response["breakdown"]["video"] = "N/A";

            response["status"] = "error";
            response["message"] = "Failed to retrieve storage info";
            std::cerr << "Failed to retrieve storage info" << std::endl;
        }

        res.set_content(response.dump(), "application/json");
    });

    // Handle CORS preflight for WebRTC endpoints
    server->Options("/api/webrtc/.*", [](const httplib::Request &, httplib::Response &res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        return;
    });
}

void IntegratedWebServer::serveIntegratedHTML(httplib::Response &res)
{
    // Load integrated HTML file
    std::string html_path = WEBFRONTEND_PATH;
    std::ifstream file(html_path);

    if (file.good())
    {
        std::stringstream buffer;
        buffer << file.rdbuf();
        res.set_content(buffer.str(), "text/html");
        std::cout << "Served integrated HTML from: " << html_path << std::endl;
    }
    else
    {
        // Server fallback HTML
        std::string html = R"(
            <!DOCTYPE html>
            <html>
            <head>
                <title>404 - Clip App Page Not Found</title>
                <style>
                    body {
                        font-family: Arial, sans-serif;
                        text-align: center;
                        padding: 5rem;
                        background-color: #f8f9fa;
                    }
                    h1 {
                        font-size: 3rem;
                        color: #dc3545;
                    }
                    p {
                        font-size: 1.2rem;
                        color: #6c757d;
                    }
                </style>
            </head>
            <body>
                <h1>404 - Clip App Page Not Found</h1>
                <p>Please make sure you have index.html under your App's resources/webfrontend/ directory.</p>
            </body>
            </html>
            )";

        res.set_content(html, "text/html");
        std::cout << "Served fallback HTML (integrated file not found)" << std::endl;
    }
}

bool IntegratedWebServer::processEmbedding(const EmbeddingInfo &embedding_info)
{
    bool success = false;

    // Print all embedding_info for debugging except for embedding vector
    std::cout << "Received embedding data:" << std::endl;
    std::cout << "  Network ID: " << embedding_info.network_id << std::endl;
    std::cout << "  Positive Prompt: " << embedding_info.positive_embedding.prompt << std::endl;
    std::cout << "  Positive Embedding Size: " << embedding_info.positive_embedding.embedding.size() << std::endl;
    for (const auto &neg : embedding_info.negative_embeddings)
    {
        std::cout << "  Negative Prompt: " << neg.prompt << std::endl;
        std::cout << "  Negative Embedding Size: " << neg.embedding.size() << std::endl;
    }
    std::cout << "  Score Threshold: " << embedding_info.score_threshold << std::endl;
    std::cout << "  Max Query: " << embedding_info.max_query << std::endl;
    std::cout << "  Remove Duplicate Within Sec: " << embedding_info.remove_duplicate_within_sec << std::endl;

    ClipQueryServiceExt::QueryEmbeddingInfo query_info;
    if (embedding_info.text_decode_on_device)
    {
        query_info.m_embedding_type = ClipQueryServiceExt::EmbeddingVectorType::DEVICE_TO_ENCODE_TEXT_EMBEDDING;
        std::cout << "  Text Decode On Device: true" << std::endl;
    }
    else
    {
        query_info.m_embedding_type = ClipQueryServiceExt::EmbeddingVectorType::FULLY_ENCODED_TEXT_EMBEDDING;
        std::cout << "  Text Decode On Device: false" << std::endl;
    }
    query_info.m_max_result = embedding_info.max_query;
    query_info.m_score_threshold = embedding_info.score_threshold;
    query_info.m_network_id = embedding_info.network_id;
    query_info.m_remove_duplicate_within_sec = embedding_info.remove_duplicate_within_sec;

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

    auto thumb_query_result = clip_query_service->query_thumbnails(query_info);

    if (thumb_query_result)
    {
        std::cout << "Query results found: " << thumb_query_result.value().size() << " matches" << std::endl;
        for (const auto &result : thumb_query_result.value())
        {
            addImage(result.m_jpeg_path, result.m_description, result.m_timestamp, result.m_score);
        }
        success = true;
    }
    else
    {
        std::cout << "ClipQueryService no result: " << thumb_query_result.error() << std::endl;
    }

    return success;
}

void IntegratedWebServer::start(std::string host, int port)
{
    std::cout << "Starting Integrated WebServer on port " << port << std::endl;
    std::cout << "Available CLIP networks: ";
    for (const auto &network : web_server_config->text_encoder_support_list)
    {
        std::cout << network.network_name << " ";
    }
    std::cout << std::endl;
    std::cout << "WebRTC streaming: enabled" << std::endl;

    m_app->start();

    server->listen(host, port);
}

void IntegratedWebServer::stop()
{
    m_app->stop();
    m_app->release();
    server->stop();
}
