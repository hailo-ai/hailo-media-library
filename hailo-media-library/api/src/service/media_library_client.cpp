#include <nlohmann/json.hpp>
#include <google/protobuf/repeated_ptr_field.h>
#include <grpcpp/security/credentials.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include "media_library/media_library.hpp"
#include "media_library/analytics_db.hpp"
#include "hailo_media_library_perfetto.hpp"
#include "media_library/media_library_logger.hpp"
#include "media_library/profile_utils.hpp"
#include "media_library/buffer_pool.hpp"
#include "media_library.grpc.pb.h"
#include "proto_converters.hpp"
#include "client_buffer_manager.hpp"
#include "event_stream_reactors.hpp"
#include "media_library.pb.h"
#include "media_library/media_library_api_types.hpp"

// Forward declarations to avoid ODR violations with config_parser_internal.
// Including config_type_conversions.hpp directly causes linker conflicts due to non-inline definitions.
void to_json(nlohmann::json &j, const config_profile_t &profile);
void from_json(const nlohmann::json &j, config_profile_t &profile);

#include <grpcpp/grpcpp.h>
#include <tl/expected.hpp>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <random>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>

#include "client_session_keeper.hpp"

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
    static constexpr std::chrono::seconds RPC_DEADLINE{2};

    using PipelineStateCb = std::function<void(media_library_pipeline_state_t)>;
    using ProfileRestrictedCb = std::function<void(const config_profile_t &, const config_profile_t &)>;
    using ProfileRestrictionDoneCb = std::function<void()>;
    using ThrottlingStateCb = std::function<void(media_library_throttling_state_t)>;

    /// Stash of every user subscription, replayed verbatim on session reacquisition.
    /// Mutex protects all fields; copies are taken under the lock to avoid holding it
    /// across gRPC calls during replay.
    struct SubscriptionRegistry
    {
        std::mutex mu;
        PipelineStateCb pipeline_state_cb;
        ProfileRestrictedCb profile_restricted_cb;
        ProfileRestrictionDoneCb profile_restriction_done_cb;
        ThrottlingStateCb throttling_state_cb;
        FrontendCallbacksMap frontend_cbs;
        std::unordered_map<output_stream_id_t, AppWrapperCallback> encoder_cbs;
    };

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

    ClientSessionKeeper m_session_keeper;
    std::string m_init_config_string;
    SubscriptionRegistry m_subs;

    ClientSessionKeeper::InitializeResult invoke_initialize()
    {
        InitRequest request;
        request.set_config_string(m_init_config_string);
        request.set_client_id(m_client_id);
        MedialibraryStatusReply reply;
        ClientContext context;
        Status status = m_stub->Initialize(&context, request, &reply);

        if (!status.ok())
        {
            // Transport failure — server may not be up yet; retry.
            LOGGER__MODULE__WARN(LoggerType::ServiceClient, "Initialize gRPC transport failed: {}: {}",
                                 status.error_code(), status.error_message());
            return {media_library_return::MEDIA_LIBRARY_ERROR, true};
        }
        // Server replied — treat any server-side failure as non-retryable.
        media_library_return ret = convert_from_proto_status(reply.status());
        if (ret == media_library_return::MEDIA_LIBRARY_SUCCESS)
            on_session_acquired();
        return {ret, false};
    }

    bool invoke_keepalive()
    {
        media_library_service::KeepAliveRequest request;
        request.set_client_id(m_client_id);
        MedialibraryStatusReply reply;
        ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + RPC_DEADLINE);
        std::mutex mu;
        std::condition_variable cv;
        bool done = false;
        Status status;
        m_stub->async()->KeepAlive(&context, &request, &reply, [&mu, &cv, &done, &status](Status s) {
            std::lock_guard<std::mutex> lock(mu);
            status = std::move(s);
            done = true;
            cv.notify_one();
        });
        std::unique_lock<std::mutex> lock(mu);
        while (!done)
            cv.wait(lock);

        if (!status.ok())
        {
            LOGGER__MODULE__WARN(LoggerType::ServiceClient, "KeepAlive gRPC failed: {}: {}", status.error_code(),
                                 status.error_message());
            return false;
        }
        return convert_from_proto_status(reply.status()) == media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    ClientSessionKeeper::ReconnectResult invoke_reconnect()
    {
        media_library_service::ReconnectRequest request;
        request.set_client_id(m_client_id);
        MedialibraryStatusReply reply;
        ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + RPC_DEADLINE);
        std::mutex mu;
        std::condition_variable cv;
        bool done = false;
        Status status;
        m_stub->async()->Reconnect(&context, &request, &reply, [&mu, &cv, &done, &status](Status s) {
            std::lock_guard<std::mutex> lock(mu);
            status = std::move(s);
            done = true;
            cv.notify_one();
        });
        std::unique_lock<std::mutex> lock(mu);
        while (!done)
            cv.wait(lock);

        if (!status.ok())
        {
            LOGGER__MODULE__WARN(LoggerType::ServiceClient, "Reconnect gRPC transport failed: {}: {}",
                                 status.error_code(), status.error_message());
            return ClientSessionKeeper::ReconnectResult::TransportError;
        }
        if (convert_from_proto_status(reply.status()) == media_library_return::MEDIA_LIBRARY_SUCCESS)
            return ClientSessionKeeper::ReconnectResult::SessionAcquired;
        return ClientSessionKeeper::ReconnectResult::SessionRejected;
    }

    /// Post-Initialize setup: (re-)create buffer managers and replay any user
    /// subscriptions stashed in the registry. Called from invoke_initialize on
    /// success — covers both first init (registry empty, replay is a no-op) and
    /// reacquire (registry populated by prior subscribe_to_* calls).
    void on_session_acquired()
    {
        if (m_frontend_buffer_manager)
            m_frontend_buffer_manager->stop();
        m_frontend_buffer_manager =
            std::make_shared<ClientBufferManager>(*m_stub, BufferStreamType::Frontend, std::string{}, m_client_id,
                                                  ClientBufferManager::DEFAULT_MAX_DISPATCH_BUFFERS);

        if (m_encoder_buffer_manager)
            m_encoder_buffer_manager->stop();
        m_encoder_buffer_manager =
            std::make_shared<ClientBufferManager>(*m_stub, BufferStreamType::Encoder, std::string{}, m_client_id,
                                                  ClientBufferManager::DEFAULT_MAX_DISPATCH_BUFFERS);

        replay_subscriptions();
    }

    /// Fired by the keeper on the first keepalive failure. The server has either
    /// already evicted us (after its KEEPALIVE_TIMEOUT grace) or is unreachable;
    /// either way every buffer the server shared with us is no longer valid, so
    /// we drop the buffer managers (and their internal queues).
    void on_session_lost()
    {
        if (m_frontend_buffer_manager)
        {
            m_frontend_buffer_manager->stop();
            m_frontend_buffer_manager.reset();
        }
        if (m_encoder_buffer_manager)
        {
            m_encoder_buffer_manager->stop();
            m_encoder_buffer_manager.reset();
        }
    }

    /// Fired by the keeper after a successful Reconnect RPC. The server slot is
    /// ours again but server-side state was wiped — replay Initialize to restore
    /// config, then on_session_acquired (called from invoke_initialize) handles
    /// buffer-manager (re-)creation and subscription replay.
    void on_session_reacquired()
    {
        auto result = invoke_initialize();
        if (result.status != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__WARN(LoggerType::ServiceClient,
                                 "Initialize failed during session reacquisition (status={}); next keepalive will "
                                 "retrigger reconnect",
                                 static_cast<int>(result.status));
        }
    }

    /// Walk the registry and re-issue every stashed subscription. Snapshot under
    /// the registry mutex, then issue gRPC calls without holding it.
    void replay_subscriptions()
    {
        SubscriptionRegistry snapshot;
        {
            std::lock_guard<std::mutex> lock(m_subs.mu);
            snapshot.pipeline_state_cb = m_subs.pipeline_state_cb;
            snapshot.profile_restricted_cb = m_subs.profile_restricted_cb;
            snapshot.profile_restriction_done_cb = m_subs.profile_restriction_done_cb;
            snapshot.throttling_state_cb = m_subs.throttling_state_cb;
            snapshot.frontend_cbs = m_subs.frontend_cbs;
            snapshot.encoder_cbs = m_subs.encoder_cbs;
        }
        if (snapshot.pipeline_state_cb)
            do_subscribe_pipeline_state(snapshot.pipeline_state_cb);
        if (snapshot.profile_restricted_cb)
            do_subscribe_profile_restricted(snapshot.profile_restricted_cb);
        if (snapshot.profile_restriction_done_cb)
            do_subscribe_profile_restriction_done(snapshot.profile_restriction_done_cb);
        if (snapshot.throttling_state_cb)
            do_subscribe_throttling_state(snapshot.throttling_state_cb);
        if (!snapshot.frontend_cbs.empty() && m_frontend_buffer_manager)
            m_frontend_buffer_manager->subscribe(snapshot.frontend_cbs);
        if (m_encoder_buffer_manager)
        {
            for (auto &[stream_id, cb] : snapshot.encoder_cbs)
                m_encoder_buffer_manager->subscribe(stream_id, cb);
        }
    }

    // --- Private subscribe helpers ---
    // Each issues the gRPC server-streaming RPC for one event type. The public
    // MediaLibraryClient::subscribe_to_* methods stash the user callback in
    // m_subs and then call the matching helper. Replay re-issues each helper
    // with the stashed callback after a reconnect.

    void do_subscribe_pipeline_state(PipelineStateCb callback)
    {
        m_pipeline_state_stream_context = std::make_unique<ClientContext>();
        auto *reader = new EventStreamReader<media_library_service::PipelineStateChangeEvent>(
            [callback](const media_library_service::PipelineStateChangeEvent &event) {
                callback(convert_from_proto_pipeline_state(event.state()));
            });
        media_library_service::SubscribeToPipelineStateChangeRequest request;
        m_stub->async()->SubscribeToPipelineStateChange(m_pipeline_state_stream_context.get(), &request, reader);
        reader->start();
    }

    void do_subscribe_profile_restricted(ProfileRestrictedCb callback)
    {
        m_profile_restricted_stream_context = std::make_unique<ClientContext>();
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
        m_stub->async()->SubscribeToProfileRestricted(m_profile_restricted_stream_context.get(), &request, reader);
        reader->start();
    }

    void do_subscribe_profile_restriction_done(ProfileRestrictionDoneCb callback)
    {
        m_profile_restriction_done_stream_context = std::make_unique<ClientContext>();
        auto *reader = new EventStreamReader<media_library_service::ProfileRestrictionDoneEvent>(
            [callback](const media_library_service::ProfileRestrictionDoneEvent & /*event*/) { callback(); });
        media_library_service::SubscribeToProfileRestrictionDoneRequest request;
        m_stub->async()->SubscribeToProfileRestrictionDone(m_profile_restriction_done_stream_context.get(), &request,
                                                           reader);
        reader->start();
    }

    void do_subscribe_throttling_state(ThrottlingStateCb callback)
    {
        m_throttling_state_stream_context = std::make_unique<ClientContext>();
        auto *reader = new EventStreamReader<media_library_service::ThrottlingStateChangeEvent>(
            [callback](const media_library_service::ThrottlingStateChangeEvent &event) {
                callback(convert_from_proto_throttling_state(event.state()));
            });
        media_library_service::SubscribeToThrottlingStateChangeRequest request;
        m_stub->async()->SubscribeToThrottlingStateChange(m_throttling_state_stream_context.get(), &request, reader);
        reader->start();
    }
};

MediaLibraryClient::MediaLibraryClient(std::string address, uint port) : pimpl_(std::make_unique<Impl>())
{
    pimpl_->m_client_id = generate_client_id();
    pimpl_->m_stub = MediaLibraryService::NewStub(
        grpc::CreateChannel(address + ":" + std::to_string(port), grpc::InsecureChannelCredentials()));

    Impl *impl = pimpl_.get();
    pimpl_->m_session_keeper.subscribe_invoke_initialize([impl]() { return impl->invoke_initialize(); });
    pimpl_->m_session_keeper.subscribe_invoke_keepalive([impl]() { return impl->invoke_keepalive(); });
    pimpl_->m_session_keeper.subscribe_invoke_reconnect([impl]() { return impl->invoke_reconnect(); });
    pimpl_->m_session_keeper.subscribe_on_session_lost([impl]() { impl->on_session_lost(); });
    pimpl_->m_session_keeper.subscribe_on_session_reacquired([impl]() { impl->on_session_reacquired(); });
}

MediaLibraryClient::~MediaLibraryClient()
{
    // Stop the session keeper FIRST so its background thread can't race
    // teardown: between shutdown() (which sends Uninitialize) and the rest
    // of cleanup, the keepalive would otherwise see a dead session, transition
    // to Reconnecting, and fire a Reconnect RPC mid-destruction.
    pimpl_->m_session_keeper.stop();

    // Now safe to send Uninitialize to clean up server-side resources.
    shutdown();

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
    // Stash the config so the keeper's Initialize callback can replay it on transport retry.
    pimpl_->m_init_config_string = std::move(medialib_config_string);
    return pimpl_->m_session_keeper.initialize();
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
    {
        std::lock_guard<std::mutex> lock(pimpl_->m_subs.mu);
        pimpl_->m_subs.profile_restricted_cb = nullptr;
        pimpl_->m_subs.profile_restriction_done_cb = nullptr;
    }
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
    {
        std::lock_guard<std::mutex> lock(pimpl_->m_subs.mu);
        pimpl_->m_subs.throttling_state_cb = nullptr;
    }
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
    {
        std::lock_guard<std::mutex> lock(pimpl_->m_subs.mu);
        pimpl_->m_subs.frontend_cbs.clear();
    }
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
    {
        std::lock_guard<std::mutex> lock(pimpl_->m_subs.mu);
        pimpl_->m_subs.frontend_cbs = fe_callbacks;
    }
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
    {
        std::lock_guard<std::mutex> lock(pimpl_->m_subs.mu);
        pimpl_->m_subs.encoder_cbs[stream_id] = callback;
    }
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
    {
        std::lock_guard<std::mutex> lock(pimpl_->m_subs.mu);
        pimpl_->m_subs.profile_restricted_cb = callback;
    }
    pimpl_->do_subscribe_profile_restricted(std::move(callback));
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryClient::on_profile_restriction_done(std::function<void()> callback)
{
    return subscribe_to_profile_restriction_done(callback);
}

media_library_return MediaLibraryClient::subscribe_to_profile_restriction_done(std::function<void()> callback)
{
    CLIENT_TRACE_RPC("subscribe_to_profile_restriction_done");
    {
        std::lock_guard<std::mutex> lock(pimpl_->m_subs.mu);
        pimpl_->m_subs.profile_restriction_done_cb = callback;
    }
    pimpl_->do_subscribe_profile_restriction_done(std::move(callback));
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
    {
        std::lock_guard<std::mutex> lock(pimpl_->m_subs.mu);
        pimpl_->m_subs.pipeline_state_cb = callback;
    }
    pimpl_->do_subscribe_pipeline_state(std::move(callback));
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryClient::subscribe_to_throttling_state_change(
    std::function<void(media_library_throttling_state_t)> callback)
{
    CLIENT_TRACE_RPC("subscribe_to_throttling_state_change");
    {
        std::lock_guard<std::mutex> lock(pimpl_->m_subs.mu);
        pimpl_->m_subs.throttling_state_cb = callback;
    }
    pimpl_->do_subscribe_throttling_state(std::move(callback));
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

} // namespace media_library_service
