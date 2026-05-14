#include "media_library/media_library.hpp"
#include "media_library/analytics_db.hpp"
#include "media_library/media_library_logger.hpp"
#include "media_library/env_vars.hpp"
#include "media_library/profile_utils.hpp"
#include "media_library/buffer_pool.hpp"
#include "media_library.grpc.pb.h"
#include "proto_converters.hpp"
#include "client_buffer_manager.hpp"
#include "event_stream_reactors.hpp"
#include "hailo_media_library_perfetto.hpp"
#include <nlohmann/json.hpp>

// Forward declarations to avoid ODR violations with config_parser_internal.
// Including config_type_conversions.hpp directly causes linker conflicts due to non-inline definitions.
void to_json(nlohmann::json &j, const config_profile_t &profile);
void from_json(const nlohmann::json &j, config_profile_t &profile);

#include <grpcpp/grpcpp.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <unordered_map>
#include <random>
#include <thread>
#include <tl/expected.hpp>
#include "event_stream_reactors.hpp"

#ifdef HAVE_PERFETTO
class ClientTraceScope
{
  public:
    explicit ClientTraceScope(const char *name)
    {
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN(perfetto::DynamicString(name), SERVICE_CLIENT_TRACK,
                                              MEDIA_LIBRARY_CATEGORY);
    }
    ~ClientTraceScope()
    {
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(SERVICE_CLIENT_TRACK, MEDIA_LIBRARY_CATEGORY);
    }
};
#define CLIENT_TRACE_RPC(name) ClientTraceScope _trace_scope(name)
#else
#define CLIENT_TRACE_RPC(name)
#endif

using media_library_service::MediaLibraryService;

using grpc::ClientContext;
using grpc::Status;
using media_library_service::GetPipelineStateReply;
using media_library_service::GetPipelineStateRequest;
using media_library_service::InitRequest;
using media_library_service::MediaLibraryStatus;
using media_library_service::MedialibraryStatusReply;
using media_library_service::SetProfileRequest;
using media_library_service::StartPipelineRequest;
using media_library_service::StopPipelineRequest;

using media_library_proto_converters::convert_from_proto_pipeline_state;
using media_library_proto_converters::convert_from_proto_status;
using media_library_proto_converters::convert_from_proto_throttling_state;

namespace media_library_service
{

static std::string generate_client_id()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);

    auto rand32 = [&]() { return dis(gen); };
    uint32_t a = rand32(), b = rand32(), c = rand32(), d = rand32();

    // UUID v4: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    char buf[37];
    snprintf(buf, sizeof(buf), "%08x-%04x-4%03x-%x%03x-%04x%08x", a, (b >> 16) & 0xFFFF, b & 0x0FFF,
             8 | ((c >> 28) & 0x3), (c >> 16) & 0x0FFF, c & 0xFFFF, d);
    return std::string(buf);
}

class MediaLibraryClient::Impl
{
  public:
    static constexpr std::chrono::seconds KEEPALIVE_INTERVAL{1};

    std::unique_ptr<MediaLibraryService::Stub> m_stub;
    std::string m_client_id;

    // Stream contexts for event subscriptions (must stay alive for stream duration)
    std::unique_ptr<ClientContext> m_pipeline_state_stream_context;
    std::unique_ptr<ClientContext> m_profile_restricted_stream_context;
    std::unique_ptr<ClientContext> m_profile_restriction_done_stream_context;
    std::unique_ptr<ClientContext> m_throttling_state_stream_context;

    // Buffer managers (shared_ptr because they use enable_shared_from_this)
    // IMPORTANT: declared after m_stub so they are destroyed first (reverse declaration order)
    std::shared_ptr<ClientBufferManager> m_frontend_buffer_manager;
    std::shared_ptr<ClientBufferManager> m_encoder_buffer_manager;
    // Keepalive
    std::atomic<bool> m_keepalive_running{false};
    std::thread m_keepalive_thread;
    std::mutex m_keepalive_mutex;
    std::condition_variable m_keepalive_cv;

    void start_keepalive()
    {
        m_keepalive_running = true;
        m_keepalive_thread = std::thread([this]() {
            while (m_keepalive_running)
            {
                {
                    std::unique_lock<std::mutex> lock(m_keepalive_mutex);
                    m_keepalive_cv.wait_for(lock, KEEPALIVE_INTERVAL, [this]() { return !m_keepalive_running.load(); });
                }
                if (!m_keepalive_running)
                    break;
                send_keepalive();
            }
        });
    }

    void stop_keepalive()
    {
        m_keepalive_running = false;
        m_keepalive_cv.notify_all();
        if (m_keepalive_thread.joinable())
            m_keepalive_thread.join();
    }

  private:
    void send_keepalive()
    {
        media_library_service::KeepAliveRequest request;
        request.set_client_id(m_client_id);
        MedialibraryStatusReply reply;
        ClientContext context;
        Status status = m_stub->KeepAlive(&context, request, &reply);
        if (!status.ok())
            LOGGER__MODULE__WARN(LoggerType::ServiceClient, "KeepAlive gRPC failed: {}: {}", status.error_code(),
                                 status.error_message());
    }
};

MediaLibraryClient::MediaLibraryClient(std::string address, uint port) : pimpl_(std::make_unique<Impl>())
{
    pimpl_->m_client_id = generate_client_id();
    pimpl_->m_stub = MediaLibraryService::NewStub(
        grpc::CreateChannel(address + ":" + std::to_string(port), grpc::InsecureChannelCredentials()));
}

MediaLibraryClient::~MediaLibraryClient()
{
    // Stop buffer streaming first (cancels UDS + gRPC buffer streams)
    if (pimpl_->m_frontend_buffer_manager)
        pimpl_->m_frontend_buffer_manager->stop();
    if (pimpl_->m_encoder_buffer_manager)
        pimpl_->m_encoder_buffer_manager->stop();

    // Stop keepalive thread first
    pimpl_->stop_keepalive();

    // Cancel any active streaming contexts before destroying Impl
    if (pimpl_->m_pipeline_state_stream_context)
        pimpl_->m_pipeline_state_stream_context->TryCancel();
    if (pimpl_->m_profile_restricted_stream_context)
        pimpl_->m_profile_restricted_stream_context->TryCancel();
    if (pimpl_->m_profile_restriction_done_stream_context)
        pimpl_->m_profile_restriction_done_stream_context->TryCancel();
    if (pimpl_->m_throttling_state_stream_context)
        pimpl_->m_throttling_state_stream_context->TryCancel();

    // Uninitialize Flow
    CLIENT_TRACE_RPC("uninitializing");
    media_library_service::UninitializeRequest request;
    request.set_client_id(pimpl_->m_client_id);
    MedialibraryStatusReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->Uninitialize(&context, request, &reply);
    if (!status.ok())
        LOGGER__MODULE__WARN(LoggerType::ServiceClient, "Uninitialize gRPC failed: {}: {}", status.error_code(),
                             status.error_message());
}

tl::expected<MediaLibraryInterfacePtr, media_library_return> MediaLibraryClient::create(const std::string &address,
                                                                                        uint port)
{
    auto client = std::make_shared<MediaLibraryClient>(address, port);
    return std::static_pointer_cast<MediaLibraryInterface>(client);
}

media_library_return MediaLibraryClient::initialize(std::string medialib_config_string, bool /*should_restore_backup*/)
{
    CLIENT_TRACE_RPC("initialize");
    LOGGER__MODULE__INFO(LoggerType::ServiceClient, "initialize: client_id={} config_size={} bytes",
                         pimpl_->m_client_id, medialib_config_string.size());

    // Retry with exponential backoff when the server returns RESOURCE_EXHAUSTED
    // (session busy with another client whose keepalive hasn't expired yet).
    // The server's keepalive timeout is 3s, so we retry for up to ~5s total.
    static constexpr int MAX_INIT_RETRIES = 5;
    static constexpr std::chrono::milliseconds INIT_RETRY_BASE_DELAY{500};

    for (int attempt = 0; attempt <= MAX_INIT_RETRIES; attempt++)
    {
        InitRequest request;
        request.set_config_string(medialib_config_string);
        request.set_client_id(pimpl_->m_client_id);
        MedialibraryStatusReply reply;
        ClientContext context;

        Status status = pimpl_->m_stub->Initialize(&context, request, &reply);
        if (status.ok())
        {
            media_library_return ret = convert_from_proto_status(reply.status());
            if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
                return ret;

            if (!pimpl_->m_keepalive_running)
                pimpl_->start_keepalive();

            // (Re-)create buffer managers so subscribe_to_*_output() gets a fresh
            // ClientContext and UDS connection. This handles the case where the
            // service was already initialized from a previous client session.
            if (pimpl_->m_frontend_buffer_manager)
                pimpl_->m_frontend_buffer_manager->stop();
            pimpl_->m_frontend_buffer_manager = std::make_shared<ClientBufferManager>(
                *pimpl_->m_stub, BufferStreamType::Frontend, std::string{}, pimpl_->m_client_id,
                ClientBufferManager::DEFAULT_MAX_DISPATCH_BUFFERS);

            if (pimpl_->m_encoder_buffer_manager)
                pimpl_->m_encoder_buffer_manager->stop();
            pimpl_->m_encoder_buffer_manager = std::make_shared<ClientBufferManager>(
                *pimpl_->m_stub, BufferStreamType::Encoder, std::string{}, pimpl_->m_client_id,
                ClientBufferManager::DEFAULT_MAX_DISPATCH_BUFFERS);

            return MEDIA_LIBRARY_SUCCESS;
        }

        if (status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED && attempt < MAX_INIT_RETRIES)
        {
            auto delay = INIT_RETRY_BASE_DELAY * (1 << attempt); // 500ms, 1s, 2s, 4s, 8s
            LOGGER__MODULE__WARN(LoggerType::ServiceClient,
                                 "Initialize: session busy (attempt {}/{}), retrying in {}ms", attempt + 1,
                                 MAX_INIT_RETRIES, delay.count());
            std::this_thread::sleep_for(delay);
            continue;
        }

        LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "Initialize gRPC failed: {}: {}", status.error_code(),
                              status.error_message());
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    return media_library_return::MEDIA_LIBRARY_ERROR;
}

media_library_return MediaLibraryClient::start_pipeline()
{
    CLIENT_TRACE_RPC("start_pipeline");
    StartPipelineRequest request;
    MedialibraryStatusReply reply;
    ClientContext context;

    Status status = pimpl_->m_stub->StartPipeline(&context, request, &reply);
    if (status.ok())
    {
        return convert_from_proto_status(reply.status());
    }
    else
    {
        LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "StartPipeline gRPC failed: {}: {}", status.error_code(),
                              status.error_message());
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
}

media_library_return MediaLibraryClient::stop_pipeline()
{
    CLIENT_TRACE_RPC("stop_pipeline");
    StopPipelineRequest request;
    MedialibraryStatusReply reply;
    ClientContext context;

    Status status = pimpl_->m_stub->StopPipeline(&context, request, &reply);
    if (status.ok())
    {
        return convert_from_proto_status(reply.status());
    }
    else
    {
        LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "StopPipeline gRPC failed: {}: {}", status.error_code(),
                              status.error_message());
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
}

media_library_return MediaLibraryClient::set_profile(const std::string &profile_name)
{
    CLIENT_TRACE_RPC("set_profile");
    SetProfileRequest request;
    request.set_profile_name(profile_name);
    MedialibraryStatusReply reply;
    ClientContext context;

    Status status = pimpl_->m_stub->SetProfile(&context, request, &reply);
    if (status.ok())
    {
        return convert_from_proto_status(reply.status());
    }
    else
    {
        LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "SetProfile gRPC failed: {}: {}", status.error_code(),
                              status.error_message());
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
}

media_library_pipeline_state_t MediaLibraryClient::get_pipeline_state() const
{
    CLIENT_TRACE_RPC("get_pipeline_state");
    GetPipelineStateRequest request;
    GetPipelineStateReply reply;
    ClientContext context;

    Status status = pimpl_->m_stub->GetPipelineState(&context, request, &reply);
    if (status.ok())
    {
        return convert_from_proto_pipeline_state(reply.pipeline_state());
    }
    else
    {
        LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "GetPipelineState gRPC failed: {}: {}", status.error_code(),
                              status.error_message());
        return media_library_pipeline_state_t::PIPELINE_STATE_UNINITIALIZED;
    }
}

// ========== Phase 3: Simple Unary RPCs ==========

media_library_return MediaLibraryClient::shutdown()
{
    CLIENT_TRACE_RPC("shutdown");

    // Stop buffer streaming first — cancels UDS + gRPC buffer streams
    if (pimpl_->m_frontend_buffer_manager)
    {
        pimpl_->m_frontend_buffer_manager->stop();
        pimpl_->m_frontend_buffer_manager.reset();
    }
    if (pimpl_->m_encoder_buffer_manager)
    {
        pimpl_->m_encoder_buffer_manager->stop();
        pimpl_->m_encoder_buffer_manager.reset();
    }

    media_library_service::ShutdownRequest request;
    MedialibraryStatusReply reply;
    ClientContext context;
    request.set_client_id(pimpl_->m_client_id);
    Status status = pimpl_->m_stub->Shutdown(&context, request, &reply);
    if (status.ok())
        return convert_from_proto_status(reply.status());
    LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "Shutdown gRPC failed: {}: {}", status.error_code(),
                          status.error_message());
    return media_library_return::MEDIA_LIBRARY_ERROR;
}

media_library_return MediaLibraryClient::set_automatic_algorithm_configuration(std::string config)
{
    CLIENT_TRACE_RPC("set_automatic_algorithm_configuration");
    media_library_service::SetAutomaticAlgorithmConfigRequest request;
    request.set_config(config);
    MedialibraryStatusReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->SetAutomaticAlgorithmConfiguration(&context, request, &reply);
    if (status.ok())
        return convert_from_proto_status(reply.status());
    LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "SetAutomaticAlgorithmConfiguration gRPC failed: {}: {}",
                          status.error_code(), status.error_message());
    return media_library_return::MEDIA_LIBRARY_ERROR;
}

void MediaLibraryClient::set_override_persistent_settings(bool value)
{
    CLIENT_TRACE_RPC("set_override_persistent_settings");
    media_library_service::SetOverridePersistentSettingsRequest request;
    request.set_value(value);
    MedialibraryStatusReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->SetOverridePersistentSettings(&context, request, &reply);
    if (!status.ok())
        LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "SetOverridePersistentSettings gRPC failed: {}: {}",
                              status.error_code(), status.error_message());
}

media_library_return MediaLibraryClient::reset_profiles()
{
    CLIENT_TRACE_RPC("reset_profiles");
    media_library_service::ResetProfilesRequest request;
    MedialibraryStatusReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->ResetProfiles(&context, request, &reply);
    if (status.ok())
        return convert_from_proto_status(reply.status());
    LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "ResetProfiles gRPC failed: {}: {}", status.error_code(),
                          status.error_message());
    return media_library_return::MEDIA_LIBRARY_ERROR;
}

media_library_return MediaLibraryClient::set_auto_profile_restriction_enabled(bool enabled)
{
    CLIENT_TRACE_RPC("set_auto_profile_restriction_enabled");
    media_library_service::SetAutoProfileRestrictionEnabledRequest request;
    request.set_enabled(enabled);
    MedialibraryStatusReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->SetAutoProfileRestrictionEnabled(&context, request, &reply);
    if (status.ok())
        return convert_from_proto_status(reply.status());
    LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "SetAutoProfileRestrictionEnabled gRPC failed: {}: {}",
                          status.error_code(), status.error_message());
    return media_library_return::MEDIA_LIBRARY_ERROR;
}

media_library_return MediaLibraryClient::set_restriction_fallback_profile(const std::string &profile_name)
{
    CLIENT_TRACE_RPC("set_restriction_fallback_profile");
    media_library_service::SetRestrictionFallbackProfileRequest request;
    request.set_profile_name(profile_name);
    MedialibraryStatusReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->SetRestrictionFallbackProfile(&context, request, &reply);
    if (status.ok())
        return convert_from_proto_status(reply.status());
    LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "SetRestrictionFallbackProfile gRPC failed: {}: {}",
                          status.error_code(), status.error_message());
    return media_library_return::MEDIA_LIBRARY_ERROR;
}

bool MediaLibraryClient::get_auto_profile_restriction_enabled()
{
    CLIENT_TRACE_RPC("get_auto_profile_restriction_enabled");
    media_library_service::GetAutoProfileRestrictionEnabledRequest request;
    media_library_service::GetAutoProfileRestrictionEnabledReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->GetAutoProfileRestrictionEnabled(&context, request, &reply);
    if (status.ok())
        return reply.enabled();
    LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "GetAutoProfileRestrictionEnabled gRPC failed: {}: {}",
                          status.error_code(), status.error_message());
    return false;
}

media_library_return MediaLibraryClient::backup_profiles()
{
    CLIENT_TRACE_RPC("backup_profiles");
    media_library_service::BackupProfilesRequest request;
    MedialibraryStatusReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->BackupProfiles(&context, request, &reply);
    if (status.ok())
        return convert_from_proto_status(reply.status());
    LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "BackupProfiles gRPC failed: {}: {}", status.error_code(),
                          status.error_message());
    return media_library_return::MEDIA_LIBRARY_ERROR;
}

void MediaLibraryClient::set_default_backup_folder_path(const std::string &path)
{
    CLIENT_TRACE_RPC("set_default_backup_folder_path");
    media_library_service::SetDefaultBackupFolderPathRequest request;
    request.set_path(path);
    MedialibraryStatusReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->SetDefaultBackupFolderPath(&context, request, &reply);
    if (!status.ok())
        LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "SetDefaultBackupFolderPath gRPC failed: {}: {}",
                              status.error_code(), status.error_message());
}

media_library_return MediaLibraryClient::unsubscribe_from_profile_restriction_callbacks()
{
    CLIENT_TRACE_RPC("unsubscribe_from_profile_restriction_callbacks");
    // Cancel any active stream contexts for profile restriction events
    if (pimpl_->m_profile_restricted_stream_context)
        pimpl_->m_profile_restricted_stream_context->TryCancel();
    if (pimpl_->m_profile_restriction_done_stream_context)
        pimpl_->m_profile_restriction_done_stream_context->TryCancel();

    media_library_service::UnsubscribeFromProfileRestrictionCallbacksRequest request;
    MedialibraryStatusReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->UnsubscribeFromProfileRestrictionCallbacks(&context, request, &reply);
    if (status.ok())
        return convert_from_proto_status(reply.status());
    LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "UnsubscribeFromProfileRestrictionCallbacks gRPC failed: {}: {}",
                          status.error_code(), status.error_message());
    return media_library_return::MEDIA_LIBRARY_ERROR;
}

media_library_return MediaLibraryClient::unsubscribe_from_throttling_state_change()
{
    CLIENT_TRACE_RPC("unsubscribe_from_throttling_state_change");
    // Cancel any active stream context for throttling events
    if (pimpl_->m_throttling_state_stream_context)
        pimpl_->m_throttling_state_stream_context->TryCancel();

    media_library_service::UnsubscribeFromThrottlingStateChangeRequest request;
    MedialibraryStatusReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->UnsubscribeFromThrottlingStateChange(&context, request, &reply);
    if (status.ok())
        return convert_from_proto_status(reply.status());
    LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "UnsubscribeFromThrottlingStateChange gRPC failed: {}: {}",
                          status.error_code(), status.error_message());
    return media_library_return::MEDIA_LIBRARY_ERROR;
}

// ========== Phase 2: Profile RPCs (JSON serialization) ==========

tl::expected<std::string, media_library_return> MediaLibraryClient::get_current_profile_str()
{
    CLIENT_TRACE_RPC("get_current_profile_str");
    // Reuse GetCurrentProfile RPC and return the JSON string directly
    media_library_service::GetCurrentProfileRequest request;
    media_library_service::GetCurrentProfileReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->GetCurrentProfile(&context, request, &reply);
    if (status.ok())
    {
        media_library_return ret = convert_from_proto_status(reply.status());
        if (ret == media_library_return::MEDIA_LIBRARY_SUCCESS)
            return reply.profile_json();
        return tl::unexpected(ret);
    }
    LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "GetCurrentProfileStr gRPC failed: {}: {}", status.error_code(),
                          status.error_message());
    return tl::unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
}

media_library_return MediaLibraryClient::set_override_parameters(const config_profile_t &profile)
{
    CLIENT_TRACE_RPC("set_override_parameters");
    media_library_service::SetOverrideParametersRequest request;
    nlohmann::json j = profile;
    request.set_profile_json(j.dump());
    MedialibraryStatusReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->SetOverrideParameters(&context, request, &reply);
    if (status.ok())
        return convert_from_proto_status(reply.status());
    LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "SetOverrideParameters gRPC failed: {}: {}", status.error_code(),
                          status.error_message());
    return media_library_return::MEDIA_LIBRARY_ERROR;
}

tl::expected<config_profile_t, media_library_return> MediaLibraryClient::get_profile(const std::string &profile_name)
{
    CLIENT_TRACE_RPC("get_profile");
    media_library_service::GetProfileRequest request;
    request.set_profile_name(profile_name);
    media_library_service::GetProfileReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->GetProfile(&context, request, &reply);
    if (status.ok())
    {
        media_library_return ret = convert_from_proto_status(reply.status());
        if (ret == media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            try
            {
                nlohmann::json j = nlohmann::json::parse(reply.profile_json());
                return j.get<config_profile_t>();
            }
            catch (const nlohmann::json::exception &e)
            {
                LOGGER__ERROR("GetProfile JSON parse failed: {}", e.what());
                return tl::unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
            }
        }
        return tl::unexpected(ret);
    }
    LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "GetProfile gRPC failed: {}: {}", status.error_code(),
                          status.error_message());
    return tl::unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
}

tl::expected<config_profile_t, media_library_return> MediaLibraryClient::get_current_profile()
{
    CLIENT_TRACE_RPC("get_current_profile");
    media_library_service::GetCurrentProfileRequest request;
    media_library_service::GetCurrentProfileReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->GetCurrentProfile(&context, request, &reply);
    if (status.ok())
    {
        media_library_return ret = convert_from_proto_status(reply.status());
        if (ret == media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            try
            {
                nlohmann::json j = nlohmann::json::parse(reply.profile_json());
                return j.get<config_profile_t>();
            }
            catch (const nlohmann::json::exception &e)
            {
                LOGGER__ERROR("GetCurrentProfile JSON parse failed: {}", e.what());
                return tl::unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
            }
        }
        return tl::unexpected(ret);
    }
    LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "GetCurrentProfile gRPC failed: {}: {}", status.error_code(),
                          status.error_message());
    return tl::unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
}

bool MediaLibraryClient::stream_restart_required(const config_profile_t &previous_profile,
                                                 const config_profile_t &new_profile)
{
    return ::stream_restart_required(previous_profile, new_profile);
}

// ========== Phase 3: Throttling State Query ==========

tl::expected<media_library_throttling_state_t, media_library_return> MediaLibraryClient::get_throttling_state() const
{
    CLIENT_TRACE_RPC("get_throttling_state");
    media_library_service::GetThrottlingStateRequest request;
    media_library_service::GetThrottlingStateReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->GetThrottlingState(&context, request, &reply);
    if (status.ok())
    {
        media_library_return ret = convert_from_proto_status(reply.status());
        if (ret == media_library_return::MEDIA_LIBRARY_SUCCESS)
            return convert_from_proto_throttling_state(reply.throttling_state());
        return tl::unexpected(ret);
    }
    LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "GetThrottlingState gRPC failed: {}: {}", status.error_code(),
                          status.error_message());
    return tl::unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
}

// ========== Not feasible over gRPC ==========

AnalyticsDB &MediaLibraryClient::get_analytics_db()
{
    LOGGER__MODULE__WARN(LoggerType::ServiceClient,
                         "[MediaLibraryClient] get_analytics_db returns local empty singleton over gRPC");
    return AnalyticsDB::instance();
}

// ========== Frontend / Encoder ==========

tl::expected<std::vector<frontend_output_stream_t>, media_library_return> MediaLibraryClient::
    get_frontend_output_streams()
{
    CLIENT_TRACE_RPC("get_frontend_output_streams");
    media_library_service::GetFrontendOutputStreamsRequest request;
    media_library_service::GetFrontendOutputStreamsReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->GetFrontendOutputStreams(&context, request, &reply);
    if (status.ok() && reply.status() == media_library_service::PROTO_MEDIA_LIBRARY_SUCCESS)
    {
        std::vector<frontend_output_stream_t> streams;
        for (const auto &proto_stream : reply.streams())
        {
            frontend_output_stream_t stream;
            stream.id = proto_stream.id();
            stream.width = proto_stream.width();
            stream.height = proto_stream.height();
            stream.target_fps = proto_stream.target_fps();
            stream.current_fps = proto_stream.current_fps();
            stream.srcpad_name = proto_stream.srcpad_name();
            streams.push_back(std::move(stream));
        }
        return streams;
    }
    if (!status.ok())
    {
        LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "GetFrontendOutputStreams gRPC failed: {}: {}",
                              status.error_code(), status.error_message());
        return tl::unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    return tl::unexpected(convert_from_proto_status(reply.status()));
}

media_library_return MediaLibraryClient::unsubscribe_all_from_frontend()
{
    CLIENT_TRACE_RPC("unsubscribe_all_from_frontend");
    media_library_service::UnsubscribeAllFromFrontendRequest request;
    MedialibraryStatusReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->UnsubscribeAllFromFrontend(&context, request, &reply);
    if (status.ok())
        return convert_from_proto_status(reply.status());
    LOGGER__MODULE__ERROR(LoggerType::ServiceClient, "UnsubscribeAllFromFrontend gRPC failed: {}: {}",
                          status.error_code(), status.error_message());
    return MEDIA_LIBRARY_ERROR;
}

// ========== Buffer Streaming: Public API ==========

media_library_return MediaLibraryClient::add_buffer_to_encoder(output_stream_id_t stream_id,
                                                               HailoMediaLibraryBufferPtr buffer)
{
    // TODO: future support in new buffer from the user

    // Look up the buffer_id for this buffer via the frontend buffer manager
    // (add_buffer_to_encoder always operates on frontend buffers)
    auto lookup_result = pimpl_->m_frontend_buffer_manager->lookup_buffer_id(buffer);
    if (!lookup_result)
    {
        LOGGER__ERROR("add_buffer_to_encoder: buffer not found in buffer_id_map "
                      "(was it received from this client?)");
        return MEDIA_LIBRARY_ERROR;
    }
    uint64_t buffer_id = *lookup_result;

    // Send gRPC request -- no FD transfer, just buffer_id + stream_id
    media_library_service::AddBufferToEncoderRequest request;
    request.set_buffer_id(buffer_id);
    request.set_stream_id(stream_id);

    media_library_service::MedialibraryStatusReply reply;
    ClientContext context;
    Status status = pimpl_->m_stub->AddBufferToEncoder(&context, request, &reply);
    if (!status.ok())
    {
        LOGGER__ERROR("AddBufferToEncoder gRPC failed: {}", status.error_message());
        return MEDIA_LIBRARY_ERROR;
    }

    return convert_from_proto_status(reply.status());
}

media_library_return MediaLibraryClient::add_buffer_to_frontend(HailoMediaLibraryBufferPtr /*buffer*/)
{
    // Pushing frames into the frontend appsrc from the client side is not supported
    throw std::runtime_error("MediaLibraryClient::add_buffer_to_frontend is not supported in multi process client");
}

media_library_return MediaLibraryClient::subscribe_to_frontend_output(FrontendCallbacksMap fe_callbacks)
{
    if (!pimpl_->m_frontend_buffer_manager)
    {
        LOGGER__MODULE__ERROR(LoggerType::ServiceClient,
                              "subscribe_to_frontend_output: not initialized, call initialize() first");
        return MEDIA_LIBRARY_UNINITIALIZED;
    }
    LOGGER__MODULE__INFO(LoggerType::ServiceClient, "subscribe_to_frontend_output: {} streams", fe_callbacks.size());
    return pimpl_->m_frontend_buffer_manager->subscribe(std::move(fe_callbacks));
}

media_library_return MediaLibraryClient::subscribe_to_encoder_output(output_stream_id_t stream_id,
                                                                     AppWrapperCallback callback)
{
    if (!pimpl_->m_encoder_buffer_manager)
    {
        LOGGER__MODULE__ERROR(LoggerType::ServiceClient,
                              "subscribe_to_encoder_output: not initialized, call initialize() first");
        return MEDIA_LIBRARY_UNINITIALIZED;
    }
    LOGGER__MODULE__INFO(LoggerType::ServiceClient, "subscribe_to_encoder_output: stream_id={}", stream_id);
    return pimpl_->m_encoder_buffer_manager->subscribe(stream_id, std::move(callback));
}

media_library_return MediaLibraryClient::unsubscribe_from_encoder_output(output_stream_id_t)
{
    LOGGER__MODULE__WARN(
        LoggerType::ServiceClient,
        "[MediaLibraryClient] unsubscribe_from_encoder_output not yet implemented over gRPC (requires DMA buffer "
        "streaming - MSW-9794)");
    return MEDIA_LIBRARY_UNINITIALIZED;
}

media_library_return MediaLibraryClient::on_profile_restricted(
    std::function<void(config_profile_t, config_profile_t)> callback)
{
    return subscribe_to_profile_restricted(
        [callback](const config_profile_t &prev, const config_profile_t &next) { callback(prev, next); });
}

media_library_return MediaLibraryClient::subscribe_to_profile_restricted(
    std::function<void(const config_profile_t &, const config_profile_t &)> callback)
{
    CLIENT_TRACE_RPC("subscribe_to_profile_restricted");
    pimpl_->m_profile_restricted_stream_context = std::make_unique<ClientContext>();

    auto *reader = new EventStreamReader<media_library_service::ProfileRestrictedEvent>(
        [callback](const media_library_service::ProfileRestrictedEvent &event) {
            try
            {
                nlohmann::json j_prev = nlohmann::json::parse(event.previous_profile_json());
                nlohmann::json j_new = nlohmann::json::parse(event.new_profile_json());
                callback(j_prev.get<config_profile_t>(), j_new.get<config_profile_t>());
            }
            catch (const nlohmann::json::exception &e)
            {
                LOGGER__ERROR("ProfileRestricted event JSON parse failed: {}", e.what());
            }
        });

    media_library_service::SubscribeToProfileRestrictedRequest request;
    pimpl_->m_stub->async()->SubscribeToProfileRestricted(pimpl_->m_profile_restricted_stream_context.get(), &request,
                                                          reader);
    reader->start();

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryClient::on_profile_restriction_done(std::function<void()> callback)
{
    return subscribe_to_profile_restriction_done(callback);
}

media_library_return MediaLibraryClient::subscribe_to_profile_restriction_done(std::function<void()> callback)
{
    CLIENT_TRACE_RPC("subscribe_to_profile_restriction_done");
    pimpl_->m_profile_restriction_done_stream_context = std::make_unique<ClientContext>();

    auto *reader = new EventStreamReader<media_library_service::ProfileRestrictionDoneEvent>(
        [callback](const media_library_service::ProfileRestrictionDoneEvent & /*event*/) { callback(); });

    media_library_service::SubscribeToProfileRestrictionDoneRequest request;
    pimpl_->m_stub->async()->SubscribeToProfileRestrictionDone(pimpl_->m_profile_restriction_done_stream_context.get(),
                                                               &request, reader);
    reader->start();

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryClient::on_pipeline_state_change(
    std::function<void(media_library_pipeline_state_t)> callback)
{
    return subscribe_to_pipeline_state_change(callback);
}

media_library_return MediaLibraryClient::subscribe_to_pipeline_state_change(
    std::function<void(media_library_pipeline_state_t)> callback)
{
    CLIENT_TRACE_RPC("subscribe_to_pipeline_state_change");
    pimpl_->m_pipeline_state_stream_context = std::make_unique<ClientContext>();

    auto *reader = new EventStreamReader<media_library_service::PipelineStateChangeEvent>(
        [callback](const media_library_service::PipelineStateChangeEvent &event) {
            callback(convert_from_proto_pipeline_state(event.state()));
        });

    media_library_service::SubscribeToPipelineStateChangeRequest request;
    pimpl_->m_stub->async()->SubscribeToPipelineStateChange(pimpl_->m_pipeline_state_stream_context.get(), &request,
                                                            reader);
    reader->start();

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryClient::subscribe_to_throttling_state_change(
    std::function<void(media_library_throttling_state_t)> callback)
{
    CLIENT_TRACE_RPC("subscribe_to_throttling_state_change");
    pimpl_->m_throttling_state_stream_context = std::make_unique<ClientContext>();

    auto *reader = new EventStreamReader<media_library_service::ThrottlingStateChangeEvent>(
        [callback](const media_library_service::ThrottlingStateChangeEvent &event) {
            callback(convert_from_proto_throttling_state(event.state()));
        });

    media_library_service::SubscribeToThrottlingStateChangeRequest request;
    pimpl_->m_stub->async()->SubscribeToThrottlingStateChange(pimpl_->m_throttling_state_stream_context.get(), &request,
                                                              reader);
    reader->start();

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

} // namespace media_library_service
