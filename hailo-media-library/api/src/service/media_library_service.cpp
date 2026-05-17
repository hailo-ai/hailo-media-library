#include "media_library/media_library.hpp"
#include "media_library/media_library_logger.hpp"
#include "media_library/env_vars.hpp"
#include "media_library/common.hpp"
#include "media_library/utils.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/privacy_mask.hpp"
#include "media_library/privacy_mask_types.hpp"
#include "media_library/signal_utils.hpp"
#include "media_library/files_utils.hpp"
#include "hailo_media_library_perfetto.hpp"
#include <filesystem>
#include <atomic>
#include <fstream>
#include <optional>
#include <iostream>
#include <sstream>
#include <thread>
#include <tl/expected.hpp>
#include <signal.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include "media_library.pb.h"      // Defines Messages (InitRequest, etc.)
#include "media_library.grpc.pb.h" // Defines Service (MediaLibraryService::Service)
#include "proto_converters.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <mutex>
#include <queue>
#include <unordered_map>
#include "media_library_service_buffer_uds.hpp"
#include "event_stream_reactors.hpp"
#include "client_session_manager.hpp"

/** @brief Default service binding address */
constexpr const char *DEFAULT_SERVICE_BIND_ADDRESS = "0.0.0.0";

/** @brief Default path for persisting service configuration across restarts */
constexpr const char *DEFAULT_SERVICE_CONFIG_BACKUP_PATH = "/etc/media-library-service/";

/** @brief Filename for persisted media library configuration */
constexpr const char *MEDIALIB_CONFIG_FILENAME = "medialib_config.json";

#ifdef HAVE_PERFETTO
class ServiceTraceScope
{
  public:
    explicit ServiceTraceScope(const char *name)
    {
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN(perfetto::DynamicString(name), SERVICE_TRACK, MEDIA_LIBRARY_CATEGORY);
    }
    ~ServiceTraceScope()
    {
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(SERVICE_TRACK, MEDIA_LIBRARY_CATEGORY);
    }
};
#define SERVICE_TRACE_RPC(name) ServiceTraceScope _trace_scope(name)
#else
#define SERVICE_TRACE_RPC(name)
#endif

// Forward declarations — avoid including config_type_conversions.hpp which has
// non-inline definitions that conflict with config_parser_internal at link time.
void to_json(nlohmann::json &j, const config_profile_t &profile);
void from_json(const nlohmann::json &j, config_profile_t &profile);

#ifdef USE_JPEG_JSONS
#define JPEG_SINK1 true
#define IS_JPEG(id) (id != "sink0")
#define FILE_ID(id) (IS_JPEG(id) ? "jpeg_" + id : id)
#else
#define JPEG_SINK1 false
#define FILE_ID(id) (id)
#define IS_JPEG(id) (false)
#endif
#define OUTPUT_FILE(id) get_output_file(FILE_ID(id), IS_JPEG(id))

using grpc::CallbackServerContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerUnaryReactor;
using grpc::Status;

using media_library_service::GetPipelineStateReply;
using media_library_service::GetPipelineStateRequest;
using media_library_service::InitRequest;
using media_library_service::MediaLibraryService;
using media_library_service::MediaLibraryStatus;
using media_library_service::MedialibraryStatusReply;
using media_library_service::SetProfileRequest;
using media_library_service::StartPipelineRequest;
using media_library_service::StopPipelineRequest;

using media_library_proto_converters::convert_to_proto_pipeline_state;
using media_library_proto_converters::convert_to_proto_status;
using media_library_proto_converters::convert_to_proto_throttling_state;

// Thresholds for live-buffer cap and slow-path warnings.
//
// The cap limits the total number of frame buffers held by a single client
// across ALL sinks. Each frame we send to the client (via SCM_RIGHTS) keeps
// a shared_ptr alive on the server until the client calls ReleaseBuffer.
static constexpr size_t LIVE_BUFFER_DROP_THRESHOLD = 40;
static constexpr size_t LIVE_BUFFER_WARN_THRESHOLD = 36;
static constexpr long SLOW_BUFFER_WARN_MS = 40;
static constexpr int HEARTBEAT_INTERVAL_SEC = 5;

// Pure utility functions — no state dependency

inline std::string get_output_file(const std::string &id, bool is_jpeg)
{
    std::string suffix = (is_jpeg ? ".jpegenc" : ".h264");
    return "/var/volatile/tmp/service_example_" + id + suffix;
}

// Server-side buffer tracking entry: holds a shared_ptr to the buffer while
// the client process is using it, preventing pool reclamation.
struct SharedBufferEntry
{
    uint64_t buffer_id;
    HailoMediaLibraryBufferPtr buffer;
    std::string client_id;
};
template <typename T> static T get_env_var(const std::string &env_var_name, const T &default_value)
{
    auto env_result = get_env_variable<T>(env_var_name);
    if (!env_result.has_value())
    {
        return default_value;
    }

    LOGGER__MODULE__INFO(LoggerType::Service, "Using {} from environment: {}", env_var_name, env_result.value());
    return env_result.value();
}

class MediaLibraryServiceImpl final : public MediaLibraryService::CallbackService
{
  public:
    explicit MediaLibraryServiceImpl(MediaLibraryPtr media_lib)
        : m_media_lib(std::move(media_lib)), m_user_profile(std::nullopt),
          m_frontend_buffer_uds_server(media_library_service::DEFAULT_FRONTEND_UDS_SOCKET_PATH),
          m_encoder_buffer_uds_server(media_library_service::DEFAULT_ENCODER_UDS_SOCKET_PATH)
    {
        if (m_frontend_buffer_uds_server.setup() != MEDIA_LIBRARY_SUCCESS)
            LOGGER__CRITICAL("Frontend BufferUdsServer setup failed — buffer streaming will not work");
        if (m_encoder_buffer_uds_server.setup() != MEDIA_LIBRARY_SUCCESS)
            LOGGER__CRITICAL("Encoder BufferUdsServer setup failed — buffer streaming will not work");
        start_heartbeat_thread();
    }

    ~MediaLibraryServiceImpl()
    {
        graceful_shutdown();
    }

    void cleanup_resources()
    {
        // Finish all active stream reactors so gRPC knows the streams are done.
        {
            std::lock_guard<std::mutex> lock(m_subscribe_mutex);
            auto finish_reactor = [](auto *&reactor) {
                if (reactor)
                {
                    reactor->finish();
                    reactor = nullptr;
                }
            };
            finish_reactor(m_pipeline_state_reactor);
            finish_reactor(m_profile_restricted_reactor);
            finish_reactor(m_profile_restriction_done_reactor);
            finish_reactor(m_throttling_state_reactor);
            finish_reactor(m_frontend_buffer_stream_reactor);
            finish_reactor(m_encoder_buffer_stream_reactor);
        }

        // Unsubscribe from callbacks to prevent invocation on destroyed objects
        if (m_media_lib)
        {
            m_media_lib->unsubscribe_from_profile_restriction_callbacks();
            m_media_lib->unsubscribe_from_throttling_state_change();
            m_media_lib->unsubscribe_all_from_frontend();
            m_media_lib->shutdown();
        }

        // close all output files
        for (auto &entry : m_output_files)
        {
            entry.second.close();
        }

        m_output_files.clear();

        // Clean up shared buffers
        {
            std::lock_guard<std::mutex> lock(m_shared_buffers_mutex);
            if (!m_shared_buffers.empty())
            {
                LOGGER__MODULE__WARN(LoggerType::Service, "cleanup_resources: dropping {} unreleased shared buffers",
                                     m_shared_buffers.size());
            }
            m_shared_buffers.clear();
        }

        // Clean up UDS
        m_frontend_buffer_uds_server.close();
        m_encoder_buffer_uds_server.close();

        m_initialized = false;
        m_media_lib.reset();
        m_media_lib = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_profile_mutex);
            m_user_profile.reset();
        }
    }

    void graceful_shutdown()
    {
        stop_heartbeat_thread();
        if (m_media_lib)
        {
            m_media_lib->stop_pipeline();
        }
        cleanup_resources();
    }

    bool try_self_initialize()
    {
        std::string backup_path = get_backup_path();
        std::string config_file_path = (std::filesystem::path(backup_path) / MEDIALIB_CONFIG_FILENAME).string();
        if (!std::filesystem::exists(config_file_path))
        {
            LOGGER__MODULE__INFO(LoggerType::Service, "No persisted config found at {}", config_file_path);
            return false;
        }

        LOGGER__MODULE__INFO(LoggerType::Service, "Found persisted config at {}, attempting self-initialization",
                             config_file_path);

        auto config_string = files_utils::read_string_from_file(config_file_path);
        if (!config_string.has_value())
        {
            LOGGER__MODULE__WARN(LoggerType::Service, "Failed to read persisted config from {}", config_file_path);
            return false;
        }

        media_library_return ret = initialize(config_string.value(), true);
        if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__WARN(LoggerType::Service, "Self-initialization failed (error {})", static_cast<int>(ret));
            return false;
        }

        media_library_return start_ret = m_media_lib->start_pipeline();
        if (start_ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__WARN(LoggerType::Service,
                                 "Self-initialization succeeded but pipeline start failed (error {})",
                                 static_cast<int>(start_ret));
            // Roll back the initialized flag so a subsequent client Initialize()
            m_initialized = false;
            return false;
        }

        LOGGER__MODULE__INFO(LoggerType::Service, "Self-initialization from persisted config succeeded");
        return true;
    }

  private:
    bool set_profile(const std::string &profile_name)
    {
        media_library_return profile_ret = m_media_lib->set_profile(profile_name);
        if (profile_ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            if (profile_ret == media_library_return::MEDIA_LIBRARY_PROFILE_IS_RESTRICTED)
            {
                LOGGER__MODULE__WARN(LoggerType::Service, "Profile is restricted at this moment, skipping");
            }
            else
            {
                LOGGER__MODULE__ERROR(LoggerType::Service, "Failed to set profile to {}", profile_name);
                return false;
            }
        }

        auto get_profile_exp = m_media_lib->get_profile(profile_name);
        if (!get_profile_exp.has_value())
        {
            LOGGER__MODULE__ERROR(LoggerType::Service, "Failed to get profile {}", profile_name);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_profile_mutex);
            m_user_profile = get_profile_exp.value();
        }

        return true;
    }

    bool set_override_parameters(config_profile_t override_profile)
    {
        media_library_return profile_ret = m_media_lib->set_override_parameters(override_profile);
        if (profile_ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            if (profile_ret == media_library_return::MEDIA_LIBRARY_PROFILE_IS_RESTRICTED)
            {
                LOGGER__MODULE__WARN(LoggerType::Service, "Profile is restricted at this moment, skipping");
            }
            else
            {
                LOGGER__MODULE__ERROR(LoggerType::Service, "Failed to override profile");
                return false;
            }
        }

        auto profile_exp = m_media_lib->get_current_profile();
        if (!profile_exp.has_value())
        {
            LOGGER__MODULE__ERROR(LoggerType::Service, "Failed to get current profile name");
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(m_profile_mutex);
            m_user_profile = profile_exp.value();
        }
        return true;
    }

    // Hold a shared_ptr copy in tracking map and send FDs to client.
    // Returns buffer_id on success, or media_library_return error on failure.
    // Sets client_broken=true if the failure indicates a dead/disconnected client.
    tl::expected<uint64_t, media_library_return> share_buffer_with_client(
        HailoMediaLibraryBufferPtr buffer, const std::string &client_id,
        media_library_service::BufferUdsServer &uds_server, bool &client_broken)
    {
        client_broken = false;
        uint64_t buffer_id;
        size_t live_count;
        {
            std::lock_guard<std::mutex> lock(m_shared_buffers_mutex);
            live_count = m_shared_buffers.size();
            if (live_count >= LIVE_BUFFER_DROP_THRESHOLD)
            {
                LOGGER__MODULE__WARN(LoggerType::Service,
                                     "share_buffer_with_client: DROP stream={} live_buffers={} >= cap={}", client_id,
                                     live_count, LIVE_BUFFER_DROP_THRESHOLD);
                return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
            }
            buffer_id = m_next_buffer_id.fetch_add(1, std::memory_order_relaxed);
            m_shared_buffers[buffer_id] = {buffer_id, buffer, client_id};
            live_count = m_shared_buffers.size();
        }

        if (live_count > LIVE_BUFFER_WARN_THRESHOLD)
        {
            LOGGER__MODULE__WARN(LoggerType::Service, "share_buffer_with_client: HIGH live_buffers={} buffer_id={}",
                                 live_count, buffer_id);
        }
        else
        {
            LOGGER__MODULE__INFO(LoggerType::Service,
                                 "share_buffer_with_client: buffer_id={} client={} live_buffers={}", buffer_id,
                                 client_id, live_count);
        }

        // Collect all plane FDs
        std::vector<int> plane_fds;
        for (uint32_t i = 0; i < buffer->get_num_of_planes(); i++)
        {
            plane_fds.push_back(buffer->get_plane_fd(i));
        }

        // Send FDs over Unix socket (non-blocking)
        auto status = uds_server.send_buffer_fds(plane_fds, buffer_id);
        if (status != media_library_service::uds_buffer_send_status::OK)
        {
            client_broken = (status == media_library_service::uds_buffer_send_status::Broken);
            LOGGER__MODULE__ERROR(LoggerType::Service, "Failed to send FDs for buffer_id={} broken={}", buffer_id,
                                  client_broken);
            // Move buffer out before erasing so its destructor runs outside m_shared_buffers_mutex.
            HailoMediaLibraryBufferPtr buffer_to_free;
            {
                std::lock_guard<std::mutex> lock(m_shared_buffers_mutex);
                auto it = m_shared_buffers.find(buffer_id);
                if (it != m_shared_buffers.end())
                {
                    buffer_to_free = std::move(it->second.buffer);
                    m_shared_buffers.erase(it);
                }
            }
            // buffer_to_free destroyed here, outside the mutex.
            return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
        }

        return buffer_id;
    }

    // Build a BufferMetadata proto from a hailo_media_library_buffer.
    // Single point of truth for buffer-to-proto conversion.
    media_library_service::BufferMetadata build_buffer_metadata(uint64_t buffer_id,
                                                                const HailoMediaLibraryBufferPtr &buffer,
                                                                const std::string &stream_id,
                                                                uint32_t encoded_data_size)
    {
        media_library_service::BufferMetadata metadata;

        // Buffer identification and stream routing
        metadata.set_buffer_id(buffer_id);
        metadata.set_stream_id(stream_id);
        metadata.set_encoded_data_size(encoded_data_size);

        // Frame dimensions and format
        metadata.set_width(buffer->buffer_data->width);
        metadata.set_height(buffer->buffer_data->height);
        metadata.set_format(static_cast<uint32_t>(buffer->buffer_data->format));
        metadata.set_planes_count(buffer->get_num_of_planes());

        // Per-plane metadata
        for (uint32_t i = 0; i < buffer->get_num_of_planes(); i++)
        {
            auto *plane = metadata.add_planes();
            plane->set_bytesperline(buffer->get_plane_stride(i));
            plane->set_bytesused(buffer->get_plane_size(i));
        }

        // Timestamps
        metadata.set_pts(buffer->pts);
        metadata.set_isp_timestamp_ns(buffer->isp_timestamp_ns);

        // ISP / Auto-Exposure
        metadata.set_isp_ae_fps(buffer->isp_ae_fps);
        metadata.set_isp_ae_converged(buffer->isp_ae_converged);
        metadata.set_isp_ae_integration_time(buffer->isp_ae_integration_time);
        metadata.set_isp_ae_average_luma(buffer->isp_ae_average_luma);

        // Video Stabilization Measurement
        metadata.set_vsm_dx(buffer->vsm.dx);
        metadata.set_vsm_dy(buffer->vsm.dy);

        // Buffer index and motion detection
        metadata.set_buffer_index(buffer->buffer_index);
        metadata.set_motion_detected(buffer->motion_detected);

        // Optical zoom
        metadata.set_optical_zoom_magnification(buffer->optical_zoom_magnification);

        // Concurrent stream IDs
        for (const auto &id : buffer->concurrent_stream_ids)
        {
            metadata.add_concurrent_stream_ids(id);
        }

        return metadata;
    }

    /* Called from both frontend and encoder callbacks. Sends FDs via UDS then
        pushes metadata via gRPC EventStreamBroker
        UDS send and push_event MUST be atomic (both under send_mutex) because:
        - All streams share one UDS socket (FIFO).
        - gRPC brokers are per-stream and deliver concurrently to client threads.
        - If push_event ran outside the mutex, two client threads could race to
          call recvmsg, pulling UDS messages in the wrong order relative to the
          gRPC metadata events they received — causing buffer_id mismatches.
        This is safe even under backpressure.
        makes the UDS send take ~0ms and bounds the
        gRPC write queue so push_event stays fast.
    */
    enum class BufferSendResult
    {
        SEND_OK,
        SEND_FAILED,
        NEED_CLIENT_CLEANUP,
    };

    void on_buffer_ready(HailoMediaLibraryBufferPtr buffer, uint32_t size, const std::string &stream_id,
                         EventStreamBroker<media_library_service::BufferMetadata> *broker,
                         media_library_service::BufferUdsServer &uds_server, std::mutex &send_mutex,
                         const std::string &client_id)
    {
        BufferSendResult result_status = BufferSendResult::SEND_FAILED;
        uint64_t buffer_id = 0;

        {
            std::lock_guard<std::mutex> lock(send_mutex);
            bool client_broken = false;
            auto result = share_buffer_with_client(buffer, client_id, uds_server, client_broken);

            if (!result.has_value())
            {
                result_status = client_broken ? BufferSendResult::NEED_CLIENT_CLEANUP : BufferSendResult::SEND_FAILED;
            }
            else
            {
                result_status = BufferSendResult::SEND_OK;
                buffer_id = result.value();
                auto metadata = build_buffer_metadata(buffer_id, buffer, stream_id, size);
                LOGGER__MODULE__DEBUG(LoggerType::Service,
                                      "on_buffer_ready: buffer_id={} stream_id={} pushing gRPC buffer metadata",
                                      buffer_id, stream_id);
                broker->push_event(std::move(metadata));
            }
        }

        if (result_status == BufferSendResult::NEED_CLIENT_CLEANUP)
        {
            LOGGER__MODULE__WARN(LoggerType::Service,
                                 "on_buffer_ready: client_id={} is dead — triggering on_client_death", client_id);
            on_client_death(client_id);
            // Close only the client connection — keep the listening socket
            // alive so a new client can reconnect without a re-setup race.
            uds_server.disconnect_client();
            // Unsubscribe frontend callbacks to stop the dead-client callback
            // loop.  The pipeline keeps running (samples are drained) and a
            // new client can re-wire callbacks via SubscribeToFrontendBufferStream.
            m_media_lib->unsubscribe_all_from_frontend();
            return;
        }

        if (result_status != BufferSendResult::SEND_OK)
            return;
    }

    // Release all shared buffers unconditionally (e.g., when a new client subscribes
    // and orphaned buffers from a dead previous client must be cleared).
    void flush_shared_buffers()
    {
        std::lock_guard<std::mutex> lock(m_shared_buffers_mutex);
        size_t count = m_shared_buffers.size();
        if (count > 0)
        {
            LOGGER__MODULE__INFO(LoggerType::Service, "flush_shared_buffers: releasing {} orphaned buffers", count);
            m_shared_buffers.clear();
        }
    }

    // Clean up buffers held by a dead client (called from keep-alive death handler)
    void on_client_death(const std::string &client_id)
    {
        std::lock_guard<std::mutex> lock(m_shared_buffers_mutex);
        std::vector<uint64_t> buffers_to_release;

        for (const auto &[buffer_id, entry] : m_shared_buffers)
        {
            if (entry.client_id == client_id)
            {
                buffers_to_release.push_back(buffer_id);
            }
        }

        LOGGER__MODULE__WARN(LoggerType::Service,
                             "on_client_death: client={} has {} orphaned buffers (live_buffers={} before cleanup)",
                             client_id, buffers_to_release.size(), m_shared_buffers.size());

        for (uint64_t buffer_id : buffers_to_release)
        {
            LOGGER__MODULE__INFO(LoggerType::Service, "Releasing orphaned buffer_id={} from dead client {}", buffer_id,
                                 client_id);
            m_shared_buffers.erase(buffer_id);
        }

        LOGGER__MODULE__INFO(LoggerType::Service, "on_client_death: cleanup complete, live_buffers={}",
                             m_shared_buffers.size());
    }

    void start_heartbeat_thread()
    {
        m_heartbeat_running = true;
        m_heartbeat_thread = std::thread([this]() {
            // Poll in 100ms slices so stop is responsive
            for (int tick = 0; m_heartbeat_running; ++tick)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (tick % (HEARTBEAT_INTERVAL_SEC * 10) != 0)
                    continue;

                size_t live_buffers;
                {
                    std::lock_guard<std::mutex> lock(m_shared_buffers_mutex);
                    live_buffers = m_shared_buffers.size();
                }

                if (live_buffers > LIVE_BUFFER_WARN_THRESHOLD)
                {
                    LOGGER__MODULE__WARN(LoggerType::Service, "HEARTBEAT: live_buffers={} (HIGH >= threshold={})",
                                         live_buffers, LIVE_BUFFER_WARN_THRESHOLD);
                }
                else
                {
                    LOGGER__MODULE__INFO(LoggerType::Service, "HEARTBEAT: live_buffers={}", live_buffers);
                }
            }
        });
    }

    void stop_heartbeat_thread()
    {
        m_heartbeat_running = false;
        if (m_heartbeat_thread.joinable())
            m_heartbeat_thread.join();
    }

    void subscribe_elements()
    {
        auto streams = m_media_lib->get_frontend_output_streams();
        if (!streams.has_value())
        {
            LOGGER__MODULE__ERROR(LoggerType::Service, "Failed to get stream ids");
            throw std::runtime_error("Failed to get stream ids");
        }

        FrontendCallbacksMap fe_callbacks;
        for (auto s : streams.value())
        {
            auto encoder_it = m_media_lib->m_encoders.find(s.id);
            if (encoder_it != m_media_lib->m_encoders.end())
            {
                auto encoder = encoder_it->second;
                fe_callbacks[s.id] = [encoder](HailoMediaLibraryBufferPtr buffer, size_t) {
                    encoder->add_buffer(buffer);
                };
            }
            else
            {
                LOGGER__MODULE__WARN(LoggerType::Service, "No encoder found for stream '{}', skipping buffer", s.id);
                fe_callbacks[s.id] = [](HailoMediaLibraryBufferPtr, size_t) {};
            }
        }
        m_media_lib->subscribe_to_frontend_output(fe_callbacks);
    }

    media_library_return reconfigure(const std::string &config_string)
    {
        // Parse the new config to extract the requested default profile.
        nlohmann::json config_json = nlohmann::json::parse(config_string, nullptr, false);
        if (config_json.is_discarded())
        {
            LOGGER__MODULE__ERROR(LoggerType::Service, "Failed to parse new configuration JSON");
            return media_library_return::MEDIA_LIBRARY_INVALID_ARGUMENT;
        }

        // Persist the new configuration for future restarts.
        media_library_return persist_ret = persist_config_string(config_string);
        if (persist_ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__WARN(LoggerType::Service, "Failed to persist new configuration");
        }

        // Don't switch profiles here — default_profile in the config is only used
        // for the initial startup. On re-init the service keeps whatever profile is
        // currently active. The user can explicitly call set_profile() if they want
        // a different one.
        auto current_profile_exp = m_media_lib->get_current_profile();
        std::string current_profile_name = current_profile_exp.has_value() ? current_profile_exp.value().name : "";
        LOGGER__MODULE__INFO(LoggerType::Service, "reconfigure: keeping current profile '{}'", current_profile_name);

        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    media_library_return initialize(const std::string &config_string, bool from_persisted_config)
    {
        if (m_initialized)
        {
            LOGGER__MODULE__INFO(LoggerType::Service, "Already initialized, applying new configuration");
            return reconfigure(config_string);
        }

        m_media_lib->set_default_backup_folder_path(get_backup_path());

        media_library_return init_ret = m_media_lib->initialize(config_string, from_persisted_config);
        if (init_ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            return init_ret;
        }

        auto profile_result = m_media_lib->get_current_profile();
        if (profile_result.has_value() && !profile_result.value().application_settings.hailort.use_hailort_service)
        {
            auto profile = profile_result.value();
            profile.application_settings.hailort.use_hailort_service = true;
            m_media_lib->set_override_parameters(profile);
            LOGGER__MODULE__INFO(LoggerType::Service, "Set use_hailort_service=true for service mode");
        }

        // Only wire frontend→encoder subscriptions when a client initiates
        // initialization (from_persisted_config == false)
        if (!from_persisted_config)
        {
            media_library_return setup_ret = setup_output_and_subscriptions();
            if (setup_ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
            {
                return setup_ret;
            }
        }

        if (!from_persisted_config)
        {
            media_library_return persist_ret = persist_configuration();
            if (persist_ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
            {
                return persist_ret;
            }
            // Persist the original client config string AFTER backup_profiles so it
            // overwrites the backup-format medialib_config.json.  On restart,
            // try_self_initialize() reads this file and the MediaLibrary fallback
            // mechanism uses it when the backup profile files fail validation.
            persist_config_string(config_string);
        }

        m_initialized = true;
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    std::string get_backup_path()
    {
        return get_env_var<std::string>(MEDIALIB_SERVICE_CONFIG_BACKUP_PATH_ENV_VAR,
                                        DEFAULT_SERVICE_CONFIG_BACKUP_PATH);
    }

    media_library_return setup_output_and_subscriptions()
    {
        subscribe_elements();
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    media_library_return persist_config_string(const std::string &config_string)
    {
        std::string backup_path = get_backup_path();
        std::string config_file_path = (std::filesystem::path(backup_path) / MEDIALIB_CONFIG_FILENAME).string();
        media_library_return ret = files_utils::write_string_to_file_atomic(config_file_path, config_string);
        if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__WARN(LoggerType::Service, "Failed to persist config string to {}", config_file_path);
        }
        return ret;
    }

    media_library_return persist_configuration()
    {
        media_library_return ret = m_media_lib->backup_profiles();
        if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__WARN(LoggerType::Service, "Failed to persist configuration (backup_profiles returned {})",
                                 static_cast<int>(ret));
        }
        return ret;
    }

    // ========== RPC Handlers ==========

    // Macro: creates reactor, checks m_media_lib, returns early with UNINITIALIZED if null.
    // Declares `ServerUnaryReactor *reactor` in the calling scope.
#define INIT_REACTOR(context, reply)                                                                                   \
    SERVICE_TRACE_RPC(__func__);                                                                                       \
    LOGGER__MODULE__TRACE(LoggerType::Service, "{} RPC called", __func__);                                             \
    ServerUnaryReactor *reactor = (context)->DefaultReactor();                                                         \
    if (!m_media_lib)                                                                                                  \
    {                                                                                                                  \
        (reply)->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_UNINITIALIZED));               \
        (reply)->set_error_message("Media library not created");                                                       \
        reactor->Finish(Status::OK);                                                                                   \
        return reactor;                                                                                                \
    }

    // Macro: creates an EventStreamBroker, replaces the previous reactor (finishing it first), and
    // declares `reactor` in the calling scope.
#define INIT_STREAM_BROKER(event_type, member_reactor)                                                                 \
    SERVICE_TRACE_RPC(__func__);                                                                                       \
    LOGGER__MODULE__TRACE(LoggerType::Service, "{} stream RPC called", __func__);                                      \
    auto *reactor = new EventStreamBroker<event_type>();                                                               \
    {                                                                                                                  \
        std::lock_guard<std::mutex> lock(m_subscribe_mutex);                                                           \
        if (member_reactor)                                                                                            \
            (member_reactor)->finish();                                                                                \
        (member_reactor) = reactor;                                                                                    \
    }

    // Macro: checks return code, sets error message on failure, sets status, finishes and returns reactor.
#define FINISH_WITH_STATUS(reactor, reply, ret, error_msg)                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        LOGGER__MODULE__DEBUG(LoggerType::Service, "{} RPC completed with status {}", __func__,                        \
                              static_cast<int>(ret));                                                                  \
        if ((ret) != media_library_return::MEDIA_LIBRARY_SUCCESS)                                                      \
            (reply)->set_error_message(error_msg);                                                                     \
        (reply)->set_status(convert_to_proto_status(ret));                                                             \
        (reactor)->Finish(Status::OK);                                                                                 \
        return (reactor);                                                                                              \
    } while (0)

    ServerUnaryReactor *Initialize(CallbackServerContext *context, const InitRequest *request,
                                   MedialibraryStatusReply *reply) override
    {
        SERVICE_TRACE_RPC("Initialize");
        LOGGER__MODULE__TRACE(LoggerType::Service, "Initialize RPC called, client_id={}, config_size={} bytes",
                              request->client_id(), request->config_string().size());
        ServerUnaryReactor *reactor = context->DefaultReactor();

        if (!m_session_manager.try_acquire(request->client_id()))
        {
            LOGGER__MODULE__WARN(LoggerType::Service, "Initialize: session busy, rejecting client_id='{}'",
                                 request->client_id());
            reactor->Finish(grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED, "Session busy, retry later"));
            return reactor;
        }

        if (!m_media_lib)
        {
            LOGGER__MODULE__DEBUG(LoggerType::Service, "Initialize: media library not created");
            reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_UNINITIALIZED));
            reply->set_error_message("Media library not created");
            reactor->Finish(Status::OK);
            return reactor;
        }

        media_library_return ret = initialize(request->config_string(), false);
        LOGGER__MODULE__DEBUG(LoggerType::Service, "Initialize RPC completed with status {}", static_cast<int>(ret));
        if (ret == media_library_return::MEDIA_LIBRARY_SENSOR_BUSY)
        {
            reply->set_error_message(
                "Sensor already in use by another media library instance, close it before initializing");
        }
        else if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            reply->set_error_message("Failed to initialize media library");
        }
        reply->set_status(convert_to_proto_status(ret));
        reactor->Finish(Status::OK);
        return reactor;
    }
    ServerUnaryReactor *StartPipeline(CallbackServerContext *context, const StartPipelineRequest * /*request*/,
                                      MedialibraryStatusReply *reply) override
    {
        INIT_REACTOR(context, reply);
        media_library_return ret = m_media_lib->start_pipeline();
        FINISH_WITH_STATUS(reactor, reply, ret, "Failed to start pipeline");
    }

    ServerUnaryReactor *StopPipeline(CallbackServerContext *context, const StopPipelineRequest * /*request*/,
                                     MedialibraryStatusReply *reply) override
    {
        INIT_REACTOR(context, reply);
        media_library_return ret = m_media_lib->stop_pipeline();
        FINISH_WITH_STATUS(reactor, reply, ret, "Failed to stop pipeline");
    }

    ServerUnaryReactor *SetProfile(CallbackServerContext *context, const SetProfileRequest *request,
                                   MedialibraryStatusReply *reply) override
    {
        INIT_REACTOR(context, reply);
        LOGGER__MODULE__DEBUG(LoggerType::Service, "SetProfile: profile_name={}", request->profile_name());
        std::string profile_name = request->profile_name();
        media_library_return ret = m_media_lib->set_profile(profile_name);
        if (ret == media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            media_library_return persist_ret = persist_configuration();
            if (persist_ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__MODULE__WARN(LoggerType::Service,
                                     "Profile set successfully but failed to persist configuration");
            }
        }
        FINISH_WITH_STATUS(reactor, reply, ret, "Failed to set profile to " + profile_name);
    }
    ServerUnaryReactor *GetPipelineState(CallbackServerContext *context, const GetPipelineStateRequest * /*request*/,
                                         GetPipelineStateReply *reply) override
    {
        SERVICE_TRACE_RPC("GetPipelineState");
        LOGGER__MODULE__TRACE(LoggerType::Service, "GetPipelineState RPC called");
        ServerUnaryReactor *reactor = context->DefaultReactor();
        if (!m_media_lib)
        {
            reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_UNINITIALIZED));
            reply->set_pipeline_state(media_library_service::PipelineState::PIPELINE_STATE_UNINITIALIZED);
            reactor->Finish(Status::OK);
            return reactor;
        }
        media_library_pipeline_state_t state = m_media_lib->get_pipeline_state();
        reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_SUCCESS));
        reply->set_pipeline_state(convert_to_proto_pipeline_state(state));
        reactor->Finish(Status::OK);
        return reactor;
    }

    ServerUnaryReactor *Shutdown(CallbackServerContext *context, const media_library_service::ShutdownRequest *request,
                                 MedialibraryStatusReply *reply) override
    {
        SERVICE_TRACE_RPC("Shutdown");
        LOGGER__MODULE__TRACE(LoggerType::Service, "Shutdown RPC called, client_id={}", request->client_id());
        ServerUnaryReactor *reactor = context->DefaultReactor();

        // Release the client session — pipeline continues streaming.
        if (!request->client_id().empty())
        {
            m_session_manager.release(request->client_id());
        }
        else
        {
            m_session_manager.release_active();
        }

        LOGGER__MODULE__INFO(LoggerType::Service, "Client disconnected via Shutdown, pipeline continues streaming");
        reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_SUCCESS));
        reactor->Finish(Status::OK);
        return reactor;
    }

    ServerUnaryReactor *Uninitialize(CallbackServerContext *context,
                                     const media_library_service::UninitializeRequest *request,
                                     MedialibraryStatusReply *reply) override
    {
        SERVICE_TRACE_RPC("Uninitialize");
        LOGGER__MODULE__TRACE(LoggerType::Service, "Uninitialize RPC called, client_id={}", request->client_id());
        ServerUnaryReactor *reactor = context->DefaultReactor();
        m_session_manager.release(request->client_id());
        reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_SUCCESS));
        reactor->Finish(Status::OK);
        return reactor;
    }

    ServerUnaryReactor *KeepAlive(CallbackServerContext *context,
                                  const media_library_service::KeepAliveRequest *request,
                                  MedialibraryStatusReply *reply) override
    {
        LOGGER__MODULE__TRACE(LoggerType::Service, "KeepAlive RPC called, client_id={}", request->client_id());
        ServerUnaryReactor *reactor = context->DefaultReactor();
        if (m_session_manager.keepalive(request->client_id()))
        {
            reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_SUCCESS));
        }
        else
        {
            reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_ERROR));
            reply->set_error_message("No active session for this client");
        }
        reactor->Finish(Status::OK);
        return reactor;
    }

    ServerUnaryReactor *SetAutomaticAlgorithmConfiguration(
        CallbackServerContext *context, const media_library_service::SetAutomaticAlgorithmConfigRequest *request,
        MedialibraryStatusReply *reply) override
    {
        INIT_REACTOR(context, reply);
        LOGGER__MODULE__DEBUG(LoggerType::Service, "SetAutomaticAlgorithmConfiguration: received config");
        media_library_return ret = m_media_lib->set_automatic_algorithm_configuration(request->config());
        FINISH_WITH_STATUS(reactor, reply, ret, "Failed to set automatic algorithm configuration");
    }

    ServerUnaryReactor *SetOverridePersistentSettings(
        CallbackServerContext *context, const media_library_service::SetOverridePersistentSettingsRequest *request,
        MedialibraryStatusReply *reply) override
    {
        INIT_REACTOR(context, reply);
        LOGGER__MODULE__DEBUG(LoggerType::Service, "SetOverridePersistentSettings: value={}", request->value());
        m_media_lib->set_override_persistent_settings(request->value());
        reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_SUCCESS));
        reactor->Finish(Status::OK);
        return reactor;
    }

    ServerUnaryReactor *ResetProfiles(CallbackServerContext *context,
                                      const media_library_service::ResetProfilesRequest * /*request*/,
                                      MedialibraryStatusReply *reply) override
    {
        INIT_REACTOR(context, reply);
        media_library_return ret = m_media_lib->reset_profiles();
        FINISH_WITH_STATUS(reactor, reply, ret, "Failed to reset profiles");
    }

    ServerUnaryReactor *SetAutoProfileRestrictionEnabled(
        CallbackServerContext *context, const media_library_service::SetAutoProfileRestrictionEnabledRequest *request,
        MedialibraryStatusReply *reply) override
    {
        INIT_REACTOR(context, reply);
        LOGGER__MODULE__DEBUG(LoggerType::Service, "SetAutoProfileRestrictionEnabled: enabled={}", request->enabled());
        media_library_return ret = m_media_lib->set_auto_profile_restriction_enabled(request->enabled());
        FINISH_WITH_STATUS(reactor, reply, ret, "Failed to set auto profile restriction enabled");
    }

    ServerUnaryReactor *SetRestrictionFallbackProfile(
        CallbackServerContext *context, const media_library_service::SetRestrictionFallbackProfileRequest *request,
        MedialibraryStatusReply *reply) override
    {
        INIT_REACTOR(context, reply);
        LOGGER__MODULE__DEBUG(LoggerType::Service, "SetRestrictionFallbackProfile: profile_name={}",
                              request->profile_name());
        media_library_return ret = m_media_lib->set_restriction_fallback_profile(request->profile_name());
        FINISH_WITH_STATUS(reactor, reply, ret, "Failed to set restriction fallback profile");
    }

    ServerUnaryReactor *GetAutoProfileRestrictionEnabled(
        CallbackServerContext *context,
        const media_library_service::GetAutoProfileRestrictionEnabledRequest * /*request*/,
        media_library_service::GetAutoProfileRestrictionEnabledReply *reply) override
    {
        // Note: GetAutoProfileRestrictionEnabledReply has no error_message field,
        // so we use manual init here instead of INIT_REACTOR.
        SERVICE_TRACE_RPC("GetAutoProfileRestrictionEnabled");
        LOGGER__MODULE__TRACE(LoggerType::Service, "GetAutoProfileRestrictionEnabled RPC called");
        ServerUnaryReactor *reactor = context->DefaultReactor();
        if (!m_media_lib)
        {
            reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_UNINITIALIZED));
            reactor->Finish(Status::OK);
            return reactor;
        }
        bool enabled = m_media_lib->get_auto_profile_restriction_enabled();
        reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_SUCCESS));
        reply->set_enabled(enabled);
        reactor->Finish(Status::OK);
        return reactor;
    }

    ServerUnaryReactor *BackupProfiles(CallbackServerContext *context,
                                       const media_library_service::BackupProfilesRequest * /*request*/,
                                       MedialibraryStatusReply *reply) override
    {
        INIT_REACTOR(context, reply);
        media_library_return ret = m_media_lib->backup_profiles();
        FINISH_WITH_STATUS(reactor, reply, ret, "Failed to backup profiles");
    }

    ServerUnaryReactor *SetDefaultBackupFolderPath(
        CallbackServerContext *context, const media_library_service::SetDefaultBackupFolderPathRequest *request,
        MedialibraryStatusReply *reply) override
    {
        INIT_REACTOR(context, reply);
        LOGGER__MODULE__DEBUG(LoggerType::Service, "SetDefaultBackupFolderPath: path={}", request->path());
        m_media_lib->set_default_backup_folder_path(request->path());
        reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_SUCCESS));
        reactor->Finish(Status::OK);
        return reactor;
    }

    ServerUnaryReactor *GetFrontendOutputStreams(
        CallbackServerContext *context, const media_library_service::GetFrontendOutputStreamsRequest * /*request*/,
        media_library_service::GetFrontendOutputStreamsReply *reply) override
    {
        INIT_REACTOR(context, reply);
        auto result = m_media_lib->get_frontend_output_streams();
        if (result.has_value())
        {
            reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_SUCCESS));
            for (const auto &stream : result.value())
            {
                auto *proto_stream = reply->add_streams();
                proto_stream->set_id(stream.id);
                proto_stream->set_width(stream.width);
                proto_stream->set_height(stream.height);
                proto_stream->set_target_fps(stream.target_fps);
                proto_stream->set_current_fps(stream.current_fps);
                proto_stream->set_srcpad_name(stream.srcpad_name);
            }
        }
        else
        {
            reply->set_status(convert_to_proto_status(result.error()));
            reply->set_error_message("Failed to get frontend output streams");
        }
        reactor->Finish(Status::OK);
        return reactor;
    }

    ServerUnaryReactor *UnsubscribeAllFromFrontend(
        CallbackServerContext *context, const media_library_service::UnsubscribeAllFromFrontendRequest * /*request*/,
        MedialibraryStatusReply *reply) override
    {
        INIT_REACTOR(context, reply);
        media_library_return ret = m_media_lib->unsubscribe_all_from_frontend();
        FINISH_WITH_STATUS(reactor, reply, ret, "Failed to unsubscribe all from frontend");
    }

    ServerUnaryReactor *UnsubscribeFromProfileRestrictionCallbacks(
        CallbackServerContext *context,
        const media_library_service::UnsubscribeFromProfileRestrictionCallbacksRequest * /*request*/,
        MedialibraryStatusReply *reply) override
    {
        INIT_REACTOR(context, reply);
        media_library_return ret = m_media_lib->unsubscribe_from_profile_restriction_callbacks();
        FINISH_WITH_STATUS(reactor, reply, ret, "Failed to unsubscribe from profile restriction callbacks");
    }

    ServerUnaryReactor *UnsubscribeFromThrottlingStateChange(
        CallbackServerContext *context,
        const media_library_service::UnsubscribeFromThrottlingStateChangeRequest * /*request*/,
        MedialibraryStatusReply *reply) override
    {
        INIT_REACTOR(context, reply);
        media_library_return ret = m_media_lib->unsubscribe_from_throttling_state_change();
        FINISH_WITH_STATUS(reactor, reply, ret, "Failed to unsubscribe from throttling state change");
    }

    ServerUnaryReactor *SetOverrideParameters(CallbackServerContext *context,
                                              const media_library_service::SetOverrideParametersRequest *request,
                                              MedialibraryStatusReply *reply) override
    {
        INIT_REACTOR(context, reply);
        LOGGER__MODULE__DEBUG(LoggerType::Service, "SetOverrideParameters: received profile JSON ({} bytes)",
                              request->profile_json().size());
        try
        {
            nlohmann::json j = nlohmann::json::parse(request->profile_json());
            config_profile_t profile = j.get<config_profile_t>();
            media_library_return ret = m_media_lib->set_override_parameters(profile);
            if (ret == media_library_return::MEDIA_LIBRARY_SUCCESS)
            {
                media_library_return persist_ret = persist_configuration();
                if (persist_ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
                {
                    LOGGER__MODULE__WARN(LoggerType::Service,
                                         "Override parameters set successfully but failed to persist configuration");
                }
            }
            else
            {
                reply->set_error_message("Failed to set override parameters");
            }
            reply->set_status(convert_to_proto_status(ret));
        }
        catch (const std::exception &e)
        {
            reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_INVALID_ARGUMENT));
            reply->set_error_message(std::string("Failed to parse profile JSON: ") + e.what());
        }
        reactor->Finish(Status::OK);
        return reactor;
    }

    ServerUnaryReactor *GetProfile(CallbackServerContext *context,
                                   const media_library_service::GetProfileRequest *request,
                                   media_library_service::GetProfileReply *reply) override
    {
        INIT_REACTOR(context, reply);
        LOGGER__MODULE__DEBUG(LoggerType::Service, "GetProfile: profile_name={}", request->profile_name());
        auto result = m_media_lib->get_profile(request->profile_name());
        if (result.has_value())
        {
            reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_SUCCESS));
            nlohmann::json j = result.value();
            reply->set_profile_json(j.dump());
        }
        else
        {
            reply->set_status(convert_to_proto_status(result.error()));
            reply->set_error_message("Failed to get profile");
        }
        reactor->Finish(Status::OK);
        return reactor;
    }

    ServerUnaryReactor *GetCurrentProfile(CallbackServerContext *context,
                                          const media_library_service::GetCurrentProfileRequest * /*request*/,
                                          media_library_service::GetCurrentProfileReply *reply) override
    {
        INIT_REACTOR(context, reply);
        auto result = m_media_lib->get_current_profile();
        if (result.has_value())
        {
            reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_SUCCESS));
            nlohmann::json j = result.value();
            reply->set_profile_json(j.dump());
        }
        else
        {
            reply->set_status(convert_to_proto_status(result.error()));
            reply->set_error_message("Failed to get current profile");
        }
        reactor->Finish(Status::OK);
        return reactor;
    }

    ServerUnaryReactor *GetThrottlingState(CallbackServerContext *context,
                                           const media_library_service::GetThrottlingStateRequest * /*request*/,
                                           media_library_service::GetThrottlingStateReply *reply) override
    {
        INIT_REACTOR(context, reply);
        auto result = m_media_lib->get_throttling_state();
        if (result.has_value())
        {
            reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_SUCCESS));
            reply->set_throttling_state(convert_to_proto_throttling_state(result.value()));
        }
        else
        {
            reply->set_status(convert_to_proto_status(result.error()));
            reply->set_error_message("Failed to get throttling state");
        }
        reactor->Finish(Status::OK);
        return reactor;
    }

    grpc::ServerWriteReactor<media_library_service::PipelineStateChangeEvent> *SubscribeToPipelineStateChange(
        CallbackServerContext * /*context*/,
        const media_library_service::SubscribeToPipelineStateChangeRequest * /*request*/) override
    {
        INIT_STREAM_BROKER(media_library_service::PipelineStateChangeEvent, m_pipeline_state_reactor);
        if (m_media_lib)
        {
            m_media_lib->subscribe_to_pipeline_state_change([reactor](media_library_pipeline_state_t state) {
                LOGGER__MODULE__DEBUG(LoggerType::Service, "Received pipeline state change notification with state: {}",
                                      (int)state);
                media_library_service::PipelineStateChangeEvent event;
                event.set_state(convert_to_proto_pipeline_state(state));
                reactor->push_event(std::move(event));
            });
        }
        return reactor;
    }

    grpc::ServerWriteReactor<media_library_service::ProfileRestrictedEvent> *SubscribeToProfileRestricted(
        CallbackServerContext * /*context*/,
        const media_library_service::SubscribeToProfileRestrictedRequest * /*request*/) override
    {
        INIT_STREAM_BROKER(media_library_service::ProfileRestrictedEvent, m_profile_restricted_reactor);
        if (m_media_lib)
        {
            m_media_lib->subscribe_to_profile_restricted(
                [reactor](const config_profile_t &previous_profile, const config_profile_t &new_profile) {
                    media_library_service::ProfileRestrictedEvent event;
                    nlohmann::json j_prev = previous_profile;
                    nlohmann::json j_new = new_profile;
                    event.set_previous_profile_json(j_prev.dump());
                    event.set_new_profile_json(j_new.dump());
                    reactor->push_event(std::move(event));
                });
        }
        return reactor;
    }

    grpc::ServerWriteReactor<media_library_service::ProfileRestrictionDoneEvent> *SubscribeToProfileRestrictionDone(
        CallbackServerContext * /*context*/,
        const media_library_service::SubscribeToProfileRestrictionDoneRequest * /*request*/) override
    {
        INIT_STREAM_BROKER(media_library_service::ProfileRestrictionDoneEvent, m_profile_restriction_done_reactor);
        if (m_media_lib)
        {
            m_media_lib->subscribe_to_profile_restriction_done([reactor]() {
                media_library_service::ProfileRestrictionDoneEvent event;
                reactor->push_event(std::move(event));
            });
        }
        return reactor;
    }

    grpc::ServerWriteReactor<media_library_service::ThrottlingStateChangeEvent> *SubscribeToThrottlingStateChange(
        CallbackServerContext * /*context*/,
        const media_library_service::SubscribeToThrottlingStateChangeRequest * /*request*/) override
    {
        INIT_STREAM_BROKER(media_library_service::ThrottlingStateChangeEvent, m_throttling_state_reactor);
        if (m_media_lib)
        {
            m_media_lib->subscribe_to_throttling_state_change([reactor](media_library_throttling_state_t state) {
                media_library_service::ThrottlingStateChangeEvent event;
                event.set_state(convert_to_proto_throttling_state(state));
                reactor->push_event(std::move(event));
            });
        }
        return reactor;
    }

    // ========== Buffer Streaming RPC Handlers ==========

    grpc::ServerWriteReactor<media_library_service::BufferMetadata> *SubscribeToFrontendBufferStream(
        CallbackServerContext * /*context*/,
        const media_library_service::SubscribeToFrontendBufferStreamRequest *request) override
    {
        INIT_STREAM_BROKER(media_library_service::BufferMetadata, m_frontend_buffer_stream_reactor);
        std::string client_id = request->client_id();

        LOGGER__MODULE__INFO(LoggerType::Service, "SubscribeToFrontendBufferStream: client={} connecting UDS...",
                             client_id);

        // 1. Unsubscribe old frontend callbacks — prevents NEW on_new_sample
        //    invocations from dispatching to old callbacks.
        m_media_lib->unsubscribe_all_from_frontend();

        // 2. Take the send mutex — this waits for any in-flight
        //    on_buffer_ready (old callbacks) to finish, then holds the
        //    lock so no stale send can race with disconnect/accept.
        {
            std::lock_guard<std::mutex> lock(m_frontend_buffer_send_mutex);
            flush_shared_buffers();
            m_frontend_buffer_uds_server.disconnect_client();
        }

        // 3. Accept the new UDS client (blocking — safe outside mutex
        //    because no callback can send: old callbacks are unsubscribed
        //    and new ones aren't wired yet).
        m_frontend_buffer_uds_server.accept_client();
        LOGGER__MODULE__INFO(LoggerType::Service,
                             "SubscribeToFrontendBufferStream: client={} UDS connected, wiring callbacks", client_id);

        if (m_media_lib)
        {
            // Wire frontend output callbacks -- send raw frame buffers to client.
            // This overwrites the default subscribe_elements() wiring (frontend -> encoder),
            // so the encoder will no longer receive input from the frontend directly.
            // The client receives these, processes them, and calls
            // add_buffer_to_encoder() when ready.
            auto streams = m_media_lib->m_frontend->get_outputs_streams();
            if (streams.has_value())
            {
                FrontendCallbacksMap fe_callbacks;
                for (const auto &s : streams.value())
                {
                    fe_callbacks[s.id] = [this, reactor, stream_id = s.id, client_id](HailoMediaLibraryBufferPtr buffer,
                                                                                      size_t size) {
                        on_buffer_ready(buffer, size, stream_id, reactor, m_frontend_buffer_uds_server,
                                        m_frontend_buffer_send_mutex, client_id);
                    };
                }
                m_media_lib->subscribe_to_frontend_output(fe_callbacks);
            }
        }

        return reactor;
    }

    grpc::ServerWriteReactor<media_library_service::BufferMetadata> *SubscribeToEncoderBufferStream(
        CallbackServerContext * /*context*/,
        const media_library_service::SubscribeToEncoderBufferStreamRequest *request) override
    {
        INIT_STREAM_BROKER(media_library_service::BufferMetadata, m_encoder_buffer_stream_reactor);
        std::string client_id = request->client_id();

        LOGGER__MODULE__INFO(LoggerType::Service, "SubscribeToEncoderBufferStream: client={} connecting UDS...",
                             client_id);

        // 1. Replace old encoder callbacks with no-ops to stop stale sends.
        if (m_media_lib)
        {
            for (const auto &[stream_id, encoder] : m_media_lib->m_encoders)
            {
                m_media_lib->subscribe_to_encoder_output(stream_id, [](HailoMediaLibraryBufferPtr, size_t) {});
            }
        }

        // 2. Serialize with in-flight encoder sends, then disconnect.
        {
            std::lock_guard<std::mutex> lock(m_encoder_buffer_send_mutex);
            m_encoder_buffer_uds_server.disconnect_client();
        }

        // 3. Accept the new UDS client connection (blocks until client connects)
        m_encoder_buffer_uds_server.accept_client();
        LOGGER__MODULE__INFO(LoggerType::Service,
                             "SubscribeToEncoderBufferStream: client={} UDS connected, wiring callbacks", client_id);

        if (m_media_lib)
        {
            // Wire encoder output callbacks -- send encoded buffers to client.
            // This overwrites the default subscribe_elements() wiring (encoder -> file),
            // so encoded output will go to the client instead of local files.
            for (const auto &[stream_id, encoder] : m_media_lib->m_encoders)
            {
                m_media_lib->subscribe_to_encoder_output(
                    stream_id, [this, reactor, stream_id_copy = stream_id, client_id](HailoMediaLibraryBufferPtr buffer,
                                                                                      size_t size) {
                        on_buffer_ready(buffer, size, stream_id_copy, reactor, m_encoder_buffer_uds_server,
                                        m_encoder_buffer_send_mutex, client_id);
                    });
            }
        }

        return reactor;
    }

    ServerUnaryReactor *ReleaseBuffer(CallbackServerContext *context,
                                      const media_library_service::ReleaseBufferRequest *request,
                                      MedialibraryStatusReply *reply) override
    {
        ServerUnaryReactor *reactor = context->DefaultReactor();
        uint64_t buffer_id = request->buffer_id();

        LOGGER__MODULE__INFO(LoggerType::Service, "ReleaseBuffer: ENTER buffer_id={}", buffer_id);

        // Move the buffer out of the map before erasing so its destructor runs
        // OUTSIDE m_shared_buffers_mutex.  The destructor may block for seconds.
        HailoMediaLibraryBufferPtr buffer_to_free;
        std::string client_id;
        size_t live_buffers_after;
        {
            std::lock_guard<std::mutex> lock(m_shared_buffers_mutex);

            auto it = m_shared_buffers.find(buffer_id);
            if (it == m_shared_buffers.end())
            {
                LOGGER__MODULE__ERROR(LoggerType::Service, "ReleaseBuffer: unknown buffer_id={} (live_buffers={})",
                                      buffer_id, m_shared_buffers.size());
                reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_ERROR));
                reply->set_error_message("Unknown buffer_id");
                reactor->Finish(Status::OK);
                return reactor;
            }

            client_id = it->second.client_id;
            buffer_to_free = std::move(it->second.buffer); // Move out; destructor deferred
            m_shared_buffers.erase(it);
            live_buffers_after = m_shared_buffers.size();

            LOGGER__MODULE__INFO(LoggerType::Service, "ReleaseBuffer: buffer_id={} client={} live_buffers={}",
                                 buffer_id, client_id, live_buffers_after);
        }
        // buffer_to_free destroyed here, outside the mutex.

        reply->set_status(convert_to_proto_status(media_library_return::MEDIA_LIBRARY_SUCCESS));
        reactor->Finish(Status::OK);
        return reactor;
    }

    ServerUnaryReactor *AddBufferToEncoder(CallbackServerContext *context,
                                           const media_library_service::AddBufferToEncoderRequest *request,
                                           MedialibraryStatusReply *reply) override
    {
        ServerUnaryReactor *reactor = context->DefaultReactor();
        uint64_t buffer_id = request->buffer_id();
        std::string stream_id = request->stream_id();

        LOGGER__MODULE__INFO(LoggerType::Service, "AddBufferToEncoder: ENTER buffer_id={} stream_id={}", buffer_id,
                             stream_id);

        // Look up the buffer in m_shared_buffers
        HailoMediaLibraryBufferPtr buffer;
        {
            std::lock_guard<std::mutex> lock(m_shared_buffers_mutex);
            auto it = m_shared_buffers.find(buffer_id);
            if (it == m_shared_buffers.end())
            {
                LOGGER__MODULE__ERROR(LoggerType::Service,
                                      "AddBufferToEncoder: unknown buffer_id={} stream_id={} (live_buffers={})",
                                      buffer_id, stream_id, m_shared_buffers.size());
                reply->set_status(convert_to_proto_status(MEDIA_LIBRARY_ERROR));
                reply->set_error_message("Unknown buffer_id");
                reactor->Finish(Status::OK);
                return reactor;
            }
            buffer = it->second.buffer; // Copy the shared_ptr (increases refcount)
        }

        LOGGER__MODULE__INFO(LoggerType::Service, "AddBufferToEncoder: buffer_id={} stream_id={}", buffer_id,
                             stream_id);

        // Feed the buffer to the encoder
        media_library_return ret = m_media_lib->add_buffer_to_encoder(stream_id, buffer);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(LoggerType::Service,
                                  "AddBufferToEncoder: encoder rejected buffer_id={} stream_id={} ret={}", buffer_id,
                                  stream_id, static_cast<int>(ret));
        }

        reply->set_status(convert_to_proto_status(ret));
        reactor->Finish(Status::OK);
        return reactor;
    }

    std::atomic<bool> m_initialized{false};
    MediaLibraryPtr m_media_lib;
    std::map<output_stream_id_t, std::ofstream> m_output_files;
    std::optional<config_profile_t> m_user_profile;
    std::mutex m_profile_mutex; // protects m_user_profile
    ClientSessionManager m_session_manager;

    std::mutex m_subscribe_mutex;
    EventStreamBroker<media_library_service::PipelineStateChangeEvent> *m_pipeline_state_reactor = nullptr;
    EventStreamBroker<media_library_service::ProfileRestrictedEvent> *m_profile_restricted_reactor = nullptr;
    EventStreamBroker<media_library_service::ProfileRestrictionDoneEvent> *m_profile_restriction_done_reactor = nullptr;
    EventStreamBroker<media_library_service::ThrottlingStateChangeEvent> *m_throttling_state_reactor = nullptr;
    EventStreamBroker<media_library_service::BufferMetadata> *m_frontend_buffer_stream_reactor = nullptr;
    EventStreamBroker<media_library_service::BufferMetadata> *m_encoder_buffer_stream_reactor = nullptr;

    // Buffer streaming state (shared across both frontend and encoder streams,
    // because ReleaseBuffer and AddBufferToEncoder need to find any buffer by ID)
    std::mutex m_shared_buffers_mutex;
    std::unordered_map<uint64_t, SharedBufferEntry> m_shared_buffers;
    std::atomic<uint64_t> m_next_buffer_id{0};

    // Per-stream mutexes ensure atomic UDS + gRPC ordering within each stream
    std::mutex m_frontend_buffer_send_mutex;
    std::mutex m_encoder_buffer_send_mutex;

    // Per-stream Unix domain sockets for FD transfer
    media_library_service::BufferUdsServer m_frontend_buffer_uds_server;
    media_library_service::BufferUdsServer m_encoder_buffer_uds_server;

    // Heartbeat thread — logs live_buffers and CMA free every HEARTBEAT_INTERVAL_SEC seconds
    std::atomic<bool> m_heartbeat_running{false};
    std::thread m_heartbeat_thread;
};

#undef INIT_REACTOR
#undef INIT_STREAM_BROKER
#undef FINISH_WITH_STATUS

const std::string SERVER_ADDRESS = "0.0.0.0:" + std::to_string(media_library_service::DEFAULT_SERVICE_PORT);

int main()
{
    uint service_port =
        get_env_var<uint>(MEDIALIB_SERVICE_GRPC_PORT_ENV_VAR, media_library_service::DEFAULT_SERVICE_PORT);
    std::string bind_address =
        get_env_var<std::string>(MEDIALIB_SERVICE_BIND_ADDRESS_ENV_VAR, DEFAULT_SERVICE_BIND_ADDRESS);
    std::string server_address = bind_address + ":" + std::to_string(service_port);

    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        LOGGER__MODULE__ERROR(LoggerType::Service, "Failed to create media library");
        return 1;
    }

    MediaLibraryServiceImpl service(media_lib_expected.value());

    // Register signal handler for graceful shutdown
    static signal_utils::SignalHandler signal_handler;
    signal_handler.register_signal_handler([&service](int signal) {
        service.graceful_shutdown();
        exit(signal);
    });

    // Start gRPC server first so the port is immediately available to clients.
    // Self-initialization (which may start the pipeline) happens after the port
    // is open, so clients can connect and query state while init is in progress.
    grpc::EnableDefaultHealthCheckService(true);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    LOGGER__MODULE__INFO(LoggerType::Service, "Server listening on {}", server_address);

    bool ignore_persistent_config = (std::getenv(MEDIALIB_SERVICE_IGNORE_PERSISTENT_CONFIG_ENV_VAR) != nullptr);

    if (ignore_persistent_config)
    {
        LOGGER__MODULE__INFO(LoggerType::Service,
                             "Ignoring persistent configuration ({} is set), waiting for client to call Initialize",
                             MEDIALIB_SERVICE_IGNORE_PERSISTENT_CONFIG_ENV_VAR);
    }
    else if (service.try_self_initialize())
    {
        LOGGER__MODULE__INFO(LoggerType::Service, "Service self-initialized from persisted configuration");
    }
    else
    {
        LOGGER__MODULE__INFO(LoggerType::Service, "Waiting for client to call Initialize");
    }

    server->Wait();
    return 0;
}
