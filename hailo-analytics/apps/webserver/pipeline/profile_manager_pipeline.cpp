#include "profile_manager_pipeline.hpp"
#include "common/common.hpp"
#include "media_library/analytics_db.hpp"
#include "hailo_analytics/pipeline/routing/tee_stage.hpp"
#include "hailo_analytics/pipeline/sinks/app_sink_stage.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "hailo_analytics/pipeline/codecs/encoder_stage.hpp"
#include <iostream>
#include <mutex>
#include <thread>

using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline::overlay;
using namespace hailo_analytics::pipeline::routing;
using namespace webserver::pipeline;
using namespace webserver::resources;

#define PROFILE_MANAGER_SUPPORTED_PROFILES                                                                             \
    {ProfileType::Daylight, ProfileType::Lowlight, ProfileType::HighDynamicRange, ProfileType::LowlightBayer}

// [GLOBAL] Storage to pass the string name to the callback
static std::string g_target_profile_name;
static std::mutex g_target_profile_mutex;
static int g_enum_cycler = 0;

ProfileManagerPipeline::ProfileManagerPipeline(webserver::resources::ResourceRepository &resources,
                                               MediaLibrary &media_library, RTPConverterStage &webrtc_stage,
                                               Architecture platform)
    : BasePipeline(resources, media_library, webrtc_stage, platform, ProfileType::Daylight,
                   PROFILE_MANAGER_SUPPORTED_PROFILES)
{
}

ProfileManagerPipeline::~ProfileManagerPipeline()
{
    stop(true);
}

std::string ProfileManagerPipeline::pipeline_name() const
{
    return "ProfileManager";
}

std::string ProfileManagerPipeline::get_profile_name_by_type(ProfileType type) const
{
    if (m_app_resources)
    {
        auto current = m_app_resources->media_library.get_current_profile();
        if (current.has_value() && !current.value().name.empty())
        {
            return current.value().name;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_target_profile_mutex);
        if (!g_target_profile_name.empty())
        {
            return g_target_profile_name;
        }
    }

    return BASIC_DAYLIGHT_PROFILE_NAME;
}

ProfileType ProfileManagerPipeline::get_profile_type_by_name(const std::string &name) const
{
    std::string current_running_name = "";
    if (m_app_resources)
    {
        auto current = m_app_resources->media_library.get_current_profile();
        if (current.has_value())
            current_running_name = current.value().name;
    }

    // If the requested name matches what is already running, ignore it.
    // This filters out validation checks like get_profile_type_by_name("Daylight_Basic").
    if (name == current_running_name)
    {
        return ProfileType::Daylight;
    }

    // It is a NEW request! Capture it.
    {
        std::lock_guard<std::mutex> lock(g_target_profile_mutex);
        g_target_profile_name = name;
    }

    WEBSERVER_LOG_DEBUG("New Target Profile: {}", name);

    // Force state change by cycling the enum
    int &counter = const_cast<int &>(g_enum_cycler);
    counter = (counter + 1) % 3;

    if (counter == 0)
        return ProfileType::Daylight;
    if (counter == 1)
        return ProfileType::Lowlight;
    return ProfileType::HighDynamicRange;
}

void ProfileManagerPipeline::register_endpoints()
{
    // Register standard endpoints from BasePipeline
    BasePipeline::register_endpoints();

    // Define our custom route
    std::string custom_endpoint = "/profile/custom";

    WEBSERVER_LOG_INFO("Registering custom endpoint: {}", custom_endpoint);

    // Register PUT handler (Switch Profile)
    m_resources.m_srv.Put(
        custom_endpoint,
        std::function<nlohmann::json(const nlohmann::json &)>([this, custom_endpoint](const nlohmann::json &j_body) {
            WEBSERVER_LOG_INFO("PUT {} called", custom_endpoint);

            // Validate Input
            if (!j_body.contains("name"))
            {
                WEBSERVER_LOG_ERROR("Missing 'name' field in request");
                throw std::runtime_error("Missing 'name' field");
            }

            std::string target_name = j_body["name"].get<std::string>();
            WEBSERVER_LOG_INFO("Custom Profile Switch Requested: {}", target_name);
            std::cout << ">>> Custom API switching to: " << target_name << " <<<" << std::endl;

            auto target_profile_expected = m_app_resources->media_library.get_profile(target_name);
            if (!target_profile_expected.has_value())
            {
                WEBSERVER_LOG_ERROR("Profile '{}' not found", target_name);
                return nlohmann::json({{"status", "error"}, {"message", "Profile not found"}});
            }

            config_profile_t target_profile = target_profile_expected.value();

            // Disable dynamic_privacy_mask analytics - ProfileManager doesn't run AI analytics
            for (auto &[stream_id, stream_config] : target_profile.encoded_output_streams)
            {
                if (stream_config.masking.dynamic_privacy_mask_config.has_value())
                {
                    stream_config.masking.dynamic_privacy_mask_config->analytics.clear();
                }
            }

            // Perform Switch
            stop(false); // Pause pipeline

            // Update m_stream_4k_name — pick the first non-JPEG stream as the main
            // (WebRTC) stream. std::map iterates alphabetically, so a naive [0]
            // can pick a JPEG-only stream (e.g. "clip_vga") before "sink0".
            std::string main_stream_id;
            for (const auto &[stream_id, stream_config] : target_profile.encoded_output_streams)
            {
                if (target_profile.get_encoder_type(stream_id) != EncoderType::Jpeg)
                {
                    main_stream_id = stream_id;
                    break;
                }
            }
            if (main_stream_id.empty())
            {
                // Fallback: all streams are JPEG, pick the first one
                main_stream_id = target_profile.encoded_output_streams.begin()->first;
            }
            m_stream_4k_name = main_stream_id;

            // Set override parameters
            auto res = m_app_resources->media_library.set_override_parameters(target_profile);

            if (res != media_library_return::MEDIA_LIBRARY_SUCCESS)
            {
                WEBSERVER_LOG_ERROR("Failed to switch to '{}': error {}", target_name, res);
                return nlohmann::json({{"status", "error"}, {"message", "Failed to switch profile"}});
            }
            else
            {
                // Restart pipeline
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                try
                {
                    start();
                }
                catch (const std::exception &e)
                {
                    WEBSERVER_LOG_ERROR("Failed to restart pipeline: {}", e.what());
                    return nlohmann::json({{"status", "error"}, {"message", "Failed to restart pipeline"}});
                }
            }

            return nlohmann::json({{"status", "success"}, {"profile", target_name}});
        }));

    // Register GET handler
    m_resources.m_srv.Get(custom_endpoint, std::function<nlohmann::json()>([this]() {
                              std::string current_name = BASIC_DAYLIGHT_PROFILE_NAME;

                              // Check MediaLibrary for the actual running profile
                              if (m_app_resources)
                              {
                                  auto current = m_app_resources->media_library.get_current_profile();
                                  if (current.has_value() && !current.value().name.empty())
                                  {
                                      current_name = current.value().name;
                                  }
                              }
                              // Return JSON response
                              return nlohmann::json({{"name", current_name}});
                          }));
}

void ProfileManagerPipeline::unregister_endpoints()
{
    WEBSERVER_LOG_INFO("Unregistering custom endpoint");
    m_resources.m_srv.Unregister("/profile/custom");
    BasePipeline::unregister_endpoints();
}

void ProfileManagerPipeline::build_pipeline()
{
    WEBSERVER_LOG_INFO("Building ProfileManager pipeline (multi-stream support)");

    m_app_resources->valve_stage = std::make_shared<ValveStage>("valve", 1);
    m_app_resources->freeze_stage = std::make_shared<FreezeStage>("freeze", 1);

    try
    {
        auto current_profile = m_app_resources->media_library.get_current_profile();
        if (!current_profile.has_value())
            throw std::runtime_error("Failed to fetch the current profile");

        auto &profile = current_profile.value();

        // Disable dynamic_privacy_mask analytics - ProfileManager doesn't run AI analytics
        for (auto &[stream_id, stream_config] : profile.encoded_output_streams)
        {
            if (stream_config.masking.dynamic_privacy_mask_config.has_value())
            {
                stream_config.masking.dynamic_privacy_mask_config->analytics.clear();
            }
        }
        // Apply the modified profile to clear dynamic_privacy_mask analytics config
        m_app_resources->media_library.set_override_parameters(profile);

        WEBSERVER_LOG_INFO("Using profile: {}", profile.name);

        if (profile.encoded_output_streams.empty())
            throw std::runtime_error("Profile has no encoded_output_streams configured");

        std::vector<std::string> stream_ids;
        for (const auto &[stream_id, stream_config] : profile.encoded_output_streams)
        {
            stream_ids.push_back(stream_id);
        }

        // Pick the first non-JPEG stream as the main (WebRTC) stream.
        // std::map iterates alphabetically, so a naive [0] can pick a
        // JPEG-only stream (e.g. "clip_vga") before "sink0".
        std::string main_stream_id;
        for (const auto &sid : stream_ids)
        {
            if (profile.get_encoder_type(sid) != EncoderType::Jpeg)
            {
                main_stream_id = sid;
                break;
            }
        }
        if (main_stream_id.empty())
        {
            main_stream_id = stream_ids[0]; // fallback: all JPEG
        }
        m_stream_4k_name = main_stream_id;

        WEBSERVER_LOG_INFO("Profile '{}' has {} stream(s), main stream: '{}'", profile.name, stream_ids.size(),
                           m_stream_4k_name);
        std::cout << ">>> ProfileManager running " << stream_ids.size() << " stream(s) <<<" << std::endl;
        bool main_stream_is_jpeg = profile.get_encoder_type(m_stream_4k_name) == EncoderType::Jpeg;

        std::shared_ptr<AppSinkStage> main_sink_stage;
        if (!main_stream_is_jpeg)
        {
            main_sink_stage =
                AppSinkStageBuild::create()
                    .set_stage_name("main_sink")
                    .set_queue_size_opt(5)
                    .set_leaky_opt(false)
                    .set_process_func([&](hailo_analytics::pipeline::BufferPtr buf) { m_webrtc_stage.process(buf); })
                    .buildptr();
        }

        hailo_analytics::pipeline::PipelineBuilder builder;
        // Configure Frontend
        std::shared_ptr<FrontendStage> frontend_stage = configure_frontend();
        builder.add_stage("frontend", frontend_stage, hailo_analytics::pipeline::StageType::SOURCE);

        for (size_t i = 0; i < stream_ids.size(); i++)
        {
            const auto &stream_id = stream_ids[i];

            std::cout << "  - Configuring Stream ID: " << stream_id << std::endl;

            std::string encoder_name = "encoder_" + stream_id;
            std::string tee_name = "tee_" + stream_id;
            std::string udp_name = "udp_" + stream_id;

            bool is_main_stream = (stream_id == m_stream_4k_name);
            int num_tee_outputs = 1;
            if (is_main_stream && !main_stream_is_jpeg)
            {
                num_tee_outputs++;
            }

            builder.add_stage(encoder_name, configure_encoder_and_osd(stream_id));
            builder.add_stage(tee_name, std::make_shared<hailo_analytics::pipeline::routing::TeeStage>(
                                            tee_name, num_tee_outputs, true, true));
            builder.add_stage(udp_name, configure_udp(stream_id), hailo_analytics::pipeline::StageType::SINK);
        }

        if (!main_stream_is_jpeg)
        {
            builder.add_stage("main_sink", main_sink_stage, hailo_analytics::pipeline::StageType::SINK);
        }
        for (size_t i = 0; i < stream_ids.size(); i++)
        {
            const auto &stream_id = stream_ids[i];
            std::string encoder_name = "encoder_" + stream_id;
            std::string tee_name = "tee_" + stream_id;
            std::string udp_name = "udp_" + stream_id;
            builder.connect_frontend("frontend", stream_id, encoder_name);
            builder.connect(encoder_name, tee_name);
            builder.connect(tee_name, udp_name);

            bool is_main_stream = (stream_id == m_stream_4k_name);
            if (is_main_stream && !main_stream_is_jpeg)
            {
                builder.connect(tee_name, "main_sink");
                WEBSERVER_LOG_INFO("  Stream '{}' -> UDP + WebRTC", stream_id);
            }
            else if (is_main_stream && main_stream_is_jpeg)
            {
                WEBSERVER_LOG_INFO("  Stream '{}' -> UDP only (JPEG encoder, WebRTC skipped)", stream_id);
            }
            else
            {
                WEBSERVER_LOG_INFO("  Stream '{}' -> UDP only", stream_id);
            }
        }

        m_app_resources->pipeline = builder.build("ProfileManagerPipeline");
        WEBSERVER_LOG_INFO("✓ ProfileManager pipeline built with {} stream(s)", stream_ids.size());
    }
    catch (const std::exception &e)
    {
        WEBSERVER_LOG_ERROR("Exception in build_pipeline: {}", e.what());
        throw;
    }
}

void ProfileManagerPipeline::start()
{
    build_pipeline();

    // After build_pipeline() determines the actual main stream name, update the
    // encoder resource so that ConfigResourceMedialib uses the correct stream key
    // when extracting encoder config during the PIPELINE_READY event.
    // Without this, the default "sink0" from init() would be used, which does not
    // exist in configs with different stream naming (e.g. clip_example profiles
    // that use "HighRes"/"VGA" instead of "sink0").
    m_resources.m_event_bus->notify(EventType::ENCODER_CHANGE,
                                    std::make_shared<EncoderState>(EncoderState(m_stream_4k_name)));

    BasePipeline::start();
}

void ProfileManagerPipeline::stop()
{
    stop(true);
}

void ProfileManagerPipeline::stop(bool full_shutdown)
{
    WEBSERVER_LOG_INFO("Stopping ProfileManager pipeline...");

    if (m_app_resources->media_library.m_frontend)
    {
        WEBSERVER_LOG_INFO("Unsubscribing Frontend");
        m_app_resources->media_library.m_frontend->unsubscribe_all();
    }

    WEBSERVER_LOG_INFO("Stopping MediaLibrary Pipeline");
    m_app_resources->media_library.stop_pipeline();

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    if (m_app_resources->pipeline)
    {
        WEBSERVER_LOG_INFO("Stopping User Pipeline");
        m_app_resources->pipeline->stop();
    }

    if (!m_app_resources->media_library.m_encoders.empty())
    {
        WEBSERVER_LOG_INFO("Stopping Encoders");
        for (auto &[name, encoder] : m_app_resources->media_library.m_encoders)
        {
            if (encoder)
            {
                std::cout << ">>> Stopping encoder: " << name << " <<<" << std::endl;
                encoder->unsubscribe();
                encoder->stop();
            }
        }
    }

    if (m_app_resources->pipeline)
        m_app_resources->pipeline.reset();
    if (m_app_resources->frontend)
        m_app_resources->frontend.reset();
    if (!m_app_resources->encoders.empty())
        m_app_resources->encoders.clear();

    if (full_shutdown)
    {
        WEBSERVER_LOG_INFO("Shutting down MediaLibrary");
        std::cout << ">>> Shutting down MediaLibrary... <<<" << std::endl;
        m_app_resources->media_library.shutdown();
    }

    WEBSERVER_LOG_INFO("ProfileManager pipeline stopped successfully");
}

bool ProfileManagerPipeline::is_jpeg_encoder() const
{
    auto it = m_app_resources->media_library.m_encoders.find(m_stream_4k_name);
    if (it == m_app_resources->media_library.m_encoders.end())
    {
        return false;
    }
    return std::holds_alternative<jpeg_encoder_config_t>(it->second->get_config());
}

void ProfileManagerPipeline::update_fps(uint32_t fps, config_profile_t &profile_config)
{
    if (is_jpeg_encoder())
    {
        if (fps < 1 || fps > 30)
        {
            WEBSERVER_LOG_ERROR("Framerate out of range: {}", fps);
            throw std::runtime_error("Framerate out of range");
        }
        for (auto &resolution : profile_config.application_settings.application_input_streams.resolutions)
        {
            resolution.framerate = fps;
        }
        WEBSERVER_LOG_INFO("Updated frontend framerate to {} (JPEG encoder, encoder update skipped)", fps);
        return;
    }
    BasePipeline::update_fps(fps, profile_config);
}

void ProfileManagerPipeline::update_resolution(const std::string &resolution, config_profile_t &profile_config)
{
    if (is_jpeg_encoder())
    {
        WEBSERVER_LOG_INFO("Resolution update skipped for JPEG encoder profile");
        return;
    }
    BasePipeline::update_resolution(resolution, profile_config);
}

void ProfileManagerPipeline::update_rotation(const std::string &rotation, config_profile_t &profile_config)
{
    if (is_jpeg_encoder())
    {
        WEBSERVER_LOG_INFO("Updating rotation to {} (JPEG encoder, encoder update skipped)", rotation);
        if (rotation_string_map.find(rotation) == rotation_string_map.end())
        {
            WEBSERVER_LOG_ERROR("Invalid rotation angle: {}", rotation);
            throw std::runtime_error("Invalid rotation angle: " + rotation);
        }
        bool enable = (rotation_string_map.at(rotation) != rotation_angle_t::ROTATION_ANGLE_0);
        profile_config.application_settings.rotation.enabled = enable;
        profile_config.application_settings.rotation.angle = rotation_string_map.at(rotation);
        return;
    }
    BasePipeline::update_rotation(rotation, profile_config);
}

hailo_encoder_config_t ProfileManagerPipeline::get_encoder_config()
{
    if (is_jpeg_encoder())
    {
        WEBSERVER_LOG_WARNING("get_encoder_config called with JPEG encoder, returning default config");
        return hailo_encoder_config_t{};
    }
    return BasePipeline::get_encoder_config();
}

void ProfileManagerPipeline::callback_handle_encoder(ResourceStateChangeNotification notif)
{
    if (is_jpeg_encoder())
    {
        WEBSERVER_LOG_INFO("Encoder parameter update skipped for JPEG encoder");
        return;
    }
    BasePipeline::callback_handle_encoder(notif);
}
