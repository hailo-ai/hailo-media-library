#include "vlm_pipeline.hpp"

#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "custom/streaming/webrtc_streamer_ext.hpp"
#include "vlm_frame_preprocessor.hpp"

namespace
{
using namespace hailo_analytics::analytics::app_constructor;
using hailo_analytics::pipeline::PipelineBuilder;
using hailo_analytics::pipeline::StageType;
using hailo_analytics::pipeline::sinks::EncodingType;
using hailo_analytics::pipeline::sinks::RTPConverterStage;
using hailo_analytics::pipeline::sinks::RTPConverterStageBuild;

// Fallback dimensions if the VLM manager is unavailable. The medialib profile
// publishes 336x336 for the VlmInput stream, so this matches what the
// frontend produces.
constexpr uint32_t kFallbackVlmHeight = 336;
constexpr uint32_t kFallbackVlmWidth = 336;
} // namespace

VlmEventMonitorPipeline::VlmEventMonitorPipeline()
    : m_pipeline_built_future(m_pipeline_built_promise.get_future().share())
{
}

VlmEventMonitorPipeline::~VlmEventMonitorPipeline()
{
    // Signal the load thread to abort before unblocking it.
    m_load_thread_should_join.store(true);

    // Wake the loader so the join() below can complete: without this,
    // if teardown happens before build_pipeline() satisfied the promise,
    // run_vlm_load() stays parked on m_pipeline_built_future.wait() and
    // join() deadlocks. m_load_thread_should_join (set above) makes the
    // loader return immediately on wake-up without touching m_jpeg_ring /
    // m_nv12_to_rgb, which the build may never have initialized.
    // set_value() throws future_error in the common case where
    // build_pipeline() already fired the promise — swallow it.
    try
    {
        m_pipeline_built_promise.set_value();
    }
    catch (const std::future_error &)
    {
    }

    if (m_vlm_load_thread.joinable())
    {
        m_vlm_load_thread.join();
    }

    // Snapshot the runtime under shared lock and stop everything in the
    // documented order. After join() above no one else writes m_runtime,
    // but we still hold the lock briefly to keep destruction race-free.
    VlmRuntime runtime;
    {
        std::shared_lock lock(m_runtime_mutex);
        runtime = m_runtime;
    }

    // Stop the runner first so no further EventCheck submissions hit the queue.
    if (runtime.runner)
    {
        runtime.runner->stop();
    }

    // Stop the chat broker before the queue — the broker may still have an
    // in-flight chat job that the queue is processing; on shutdown it closes
    // any open sessions and unblocks pending streams.
    if (runtime.chat_broker)
    {
        runtime.chat_broker->stop();
    }
    if (runtime.queue)
    {
        runtime.queue->stop();
    }

    // Clear the converter callback to break any potential capture cycles.
    if (m_nv12_to_rgb)
    {
        m_nv12_to_rgb->set_callback(nullptr);
    }
}

std::shared_ptr<vlm_event_monitor::ChatSessionBroker> VlmEventMonitorPipeline::chat_broker() const
{
    std::shared_lock lock(m_runtime_mutex);
    return m_runtime.chat_broker;
}

std::shared_ptr<vlm_event_monitor::EventStateTracker> VlmEventMonitorPipeline::event_state_tracker() const
{
    std::shared_lock lock(m_runtime_mutex);
    return m_runtime.state_tracker;
}

std::string VlmEventMonitorPipeline::vlm_load_error() const
{
    std::shared_lock lock(m_runtime_mutex);
    return m_vlm_load_error;
}

std::string VlmEventMonitorPipeline::default_media_config() const
{
    return vlm_app::paths::medialib_config;
}

std::string VlmEventMonitorPipeline::main_stream_encoder_id(const MediaStageComponents &components) const
{
    for (const auto &[stream_id, _] : components.m_encoder_stages)
    {
        if (stream_id == vlm_app::stream_id::stream_4k)
        {
            return stream_id;
        }
    }
    return {};
}

std::string VlmEventMonitorPipeline::main_stream_frontend_output_id(const MediaStageComponents &components) const
{
    auto streams = components.m_frontend_stage->get_outputs_streams();
    if (!streams.has_value())
    {
        return {};
    }
    for (const auto &output_stream : streams.value())
    {
        if (output_stream.id == vlm_app::stream_id::stream_4k)
        {
            return output_stream.id;
        }
    }
    return {};
}

std::shared_ptr<VlmInferenceManager> VlmEventMonitorPipeline::try_create_vlm_manager() const
{
    if (!m_app_custom_data)
    {
        return nullptr;
    }
    const auto &model = m_app_custom_data->m_config.vlm_model;
    if (model.hef_path.empty())
    {
        HAILO_ANALYTICS_LOG_WARN("VLM HEF path is empty in YAML config; event-check loop disabled");
        return nullptr;
    }

    VlmConfig vlm_config;
    vlm_config.hef_path = model.hef_path;
    vlm_config.group_id = m_app_custom_data->m_config.hailort_device_config.device_id;
    vlm_config.default_max_generated_tokens = model.default_max_generated_tokens;
    vlm_config.busy_wait_timeout = std::chrono::milliseconds(model.busy_wait_timeout_ms);

    auto result = VlmInferenceManager::create(vlm_config);
    if (!result)
    {
        HAILO_ANALYTICS_LOG_WARN("VLM Inference Manager init failed ({}); event-check loop disabled. "
                                 "JPEG cache and 336 RGB conversion still active.",
                                 result.error());
        return nullptr;
    }
    HAILO_ANALYTICS_LOG_INFO("VLM Inference Manager loaded (input {}x{}x{})", result.value()->input_frame_width(),
                             result.value()->input_frame_height(), result.value()->input_frame_channels());
    return result.value();
}

CamAppReturnCode VlmEventMonitorPipeline::register_app_extensions(std::shared_ptr<UserDataBase> user_data)
{
    auto custom = std::dynamic_pointer_cast<VlmAppCustomData>(user_data);
    if (!custom)
    {
        HAILO_ANALYTICS_LOG_ERROR("VlmAppCustomData is not set in VlmEventMonitorPipeline");
        return CamAppReturnCode::APP_EXTENSION_REGITRATION_FAILED;
    }
    m_app_custom_data = custom;

    register_extension(std::make_shared<WebRTCStreamerExt>());

    // Persisted event list + SSE fan-out. Both are constructed
    // unconditionally (independent of VLM availability) so the frontend can
    // edit/save events even if the model didn't load.
    m_event_store = vlm_event_monitor::EventStore::create(m_app_custom_data->m_config.events_file_path);
    m_sse_broadcaster = std::make_shared<vlm_event_monitor::SseBroadcaster>();

    // The apply callback resolves the VLM runtime through the
    // shared lock on every fire. Until the background load thread
    // populates m_runtime, the runner/tracker work short-circuits; the
    // monitoring_status broadcast still fires so the frontend reflects
    // mode/event-list changes during the load window.
    std::weak_ptr<vlm_event_monitor::EventStore> store_weak = m_event_store;
    m_event_store->set_apply_callback([this, store_weak](const std::vector<vlm_event_monitor::UserEvent> &old_events,
                                                         const std::vector<vlm_event_monitor::UserEvent> &events,
                                                         vlm_event_monitor::EventCheckMode mode) {
        VlmRuntime runtime_snapshot;
        {
            std::shared_lock lock(m_runtime_mutex);
            runtime_snapshot = m_runtime;
        }

        // Reset cooldown for any event whose description
        // changed (so the new prompt's first verdict isn't masked
        // by the old prompt's stamp); prune entries for deleted
        // event_ids so the tracker map can't leak. No-op while the
        // tracker is still being constructed by the background load
        // thread.
        if (runtime_snapshot.state_tracker)
        {
            std::unordered_map<uint32_t, const std::string *> old_by_id;
            old_by_id.reserve(old_events.size());
            for (const auto &event : old_events)
            {
                old_by_id.emplace(event.id, &event.description);
            }

            std::unordered_set<uint32_t> new_ids;
            new_ids.reserve(events.size());
            for (const auto &event : events)
            {
                new_ids.insert(event.id);
                auto it = old_by_id.find(event.id);
                if (it != old_by_id.end() && *it->second != event.description)
                {
                    runtime_snapshot.state_tracker->reset(event.id);
                    HAILO_ANALYTICS_LOG_INFO("EventStateTracker: cooldown reset for event_id={} "
                                             "(description changed)",
                                             event.id);
                }
            }
            runtime_snapshot.state_tracker->prune_to(new_ids);
        }

        if (runtime_snapshot.runner)
        {
            runtime_snapshot.runner->set_events(events);
            runtime_snapshot.runner->set_mode(mode);
        }

        auto store = store_weak.lock();
        if (m_sse_broadcaster && store)
        {
            broadcast_monitoring_status();
        }
    });

    // Spawn the background HEF load. register_app_extensions
    // returns immediately afterwards so build_pipeline + listen() can
    // proceed; the runtime members become available once the thread
    // publishes them.
    start_vlm_load_async();

    return CamAppReturnCode::SUCCESS;
}

void VlmEventMonitorPipeline::start_vlm_load_async()
{
    m_vlm_load_thread = std::thread(&VlmEventMonitorPipeline::run_vlm_load, this);
}

void VlmEventMonitorPipeline::run_vlm_load()
{
    HAILO_ANALYTICS_LOG_INFO("VlmEventMonitorPipeline: background VLM load starting…");

    auto manager = try_create_vlm_manager();
    if (!manager)
    {
        {
            std::unique_lock lock(m_runtime_mutex);
            m_vlm_load_error = "VLM manager creation failed (see warnings above)";
        }
        m_vlm_load_state.store(VlmLoadState::Failed);
        HAILO_ANALYTICS_LOG_ERROR("VlmEventMonitorPipeline: VLM load FAILED — chat + event-check disabled");
        broadcast_monitoring_status();
        return;
    }

    // Wait for build_pipeline to construct m_jpeg_ring and m_nv12_to_rgb.
    // In practice the HEF load is orders of magnitude slower than
    // build_pipeline so this returns immediately; the wait is just
    // belt-and-braces for tear-down ordering.
    m_pipeline_built_future.wait();

    // If the destructor unblocked us as part of tear-down, bail out
    // before constructing anything that touches potentially-freed
    // members. The destructor's join() then completes promptly.
    if (m_load_thread_should_join.load())
    {
        HAILO_ANALYTICS_LOG_INFO("VlmEventMonitorPipeline: VLM load aborted (shutting down)");
        return;
    }

    const auto &event_check_cfg = m_app_custom_data->m_config.event_check;

    vlm_event_monitor::EventCheckRunnerConfig runner_config;
    runner_config.mode = m_event_store->mode();
    runner_config.performance_lead_prompt = event_check_cfg.performance.lead_prompt;
    runner_config.accuracy_lead_prompt = event_check_cfg.accuracy.lead_prompt;
    runner_config.accuracy_max_tokens = event_check_cfg.accuracy.max_tokens;
    const auto &override_cfg = event_check_cfg.debug_prompt_override;
    runner_config.debug_override_enabled = override_cfg.enabled;
    runner_config.debug_override_prompt = override_cfg.prompt;
    runner_config.debug_override_max_tokens = override_cfg.max_generated_tokens;
    const auto &meta_cfg = event_check_cfg.debug_metadata_save;
    runner_config.debug_metadata_save_enabled = meta_cfg.enabled;
    runner_config.debug_metadata_save_keep_last = meta_cfg.keep_last;
    runner_config.vlm_hef_path = m_app_custom_data->m_config.vlm_model.hef_path;
    runner_config.vlm_default_max_generated_tokens = m_app_custom_data->m_config.vlm_model.default_max_generated_tokens;

    HAILO_ANALYTICS_LOG_INFO("EventCheckRunner: starting in '{}' mode",
                             vlm_event_monitor::to_string(runner_config.mode));
    if (override_cfg.enabled)
    {
        HAILO_ANALYTICS_LOG_INFO("EventCheckRunner: DEBUG prompt override active — yes/no parsing disabled, "
                                 "prompt='{}', max_tokens={}",
                                 override_cfg.prompt, override_cfg.max_generated_tokens);
    }
    if (meta_cfg.enabled)
    {
        HAILO_ANALYTICS_LOG_INFO("EventCheckRunner: DEBUG metadata save active — writing to "
                                 "/var/volatile/vlm-event-metadata/, keep_last={}",
                                 meta_cfg.keep_last);
    }

    VlmRuntime runtime;
    runtime.manager = manager;
    runtime.queue = std::make_shared<vlm_event_monitor::InferenceRequestQueue>(manager);
    runtime.state_tracker =
        std::make_shared<vlm_event_monitor::EventStateTracker>(std::chrono::seconds(event_check_cfg.cooldown_seconds));
    HAILO_ANALYTICS_LOG_INFO("EventStateTracker: cooldown {} s", event_check_cfg.cooldown_seconds);

    runtime.runner = std::make_shared<vlm_event_monitor::EventCheckRunner>(
        runtime.queue, runner_config, m_event_store->events(),
        std::weak_ptr<vlm_event_monitor::JpegRingBufferStage>(m_jpeg_ring), runtime.state_tracker);

    std::weak_ptr<vlm_event_monitor::SseBroadcaster> broadcaster_weak = m_sse_broadcaster;
    runtime.runner->set_result_broadcaster([broadcaster_weak](const vlm_event_monitor::TriggeredEvent &event) {
        if (auto broadcaster = broadcaster_weak.lock())
        {
            broadcaster->push_new_event(event);
        }
    });

    runtime.chat_preprocessor = std::make_shared<VlmFramePreprocessor>(
        manager->input_frame_height(), manager->input_frame_width(), manager->input_frame_channels());

    vlm_event_monitor::ChatSessionBroker::Config chat_broker_config;
    chat_broker_config.session_timeout_seconds = m_app_custom_data->m_config.chat.session_timeout_seconds;
    chat_broker_config.pause_event_check_during_chat = m_app_custom_data->m_config.chat.pause_event_check_during_chat;
    chat_broker_config.default_max_generated_tokens = m_app_custom_data->m_config.chat.default_max_generated_tokens;
    runtime.chat_broker = std::make_shared<vlm_event_monitor::ChatSessionBroker>(
        runtime.queue, m_jpeg_ring, runtime.chat_preprocessor, m_sse_broadcaster, chat_broker_config);
    runtime.chat_broker->set_pause_state_change_notifier([this]() { broadcast_monitoring_status(); });

    // Wire the RGB sampler callback BEFORE publishing — once published,
    // any caller could observe the runtime and start submitting jobs;
    // we want the runner to receive frames from the moment it's visible.
    if (m_nv12_to_rgb)
    {
        std::weak_ptr<vlm_event_monitor::EventCheckRunner> runner_weak = runtime.runner;
        m_nv12_to_rgb->set_callback([runner_weak](std::vector<uint8_t> rgb) {
            if (auto runner = runner_weak.lock())
            {
                runner->push_rgb_frame(std::move(rgb));
            }
        });
    }

    runtime.queue->start();
    runtime.runner->start();
    runtime.chat_broker->start();

    {
        std::unique_lock lock(m_runtime_mutex);
        m_runtime = std::move(runtime);
    }
    m_vlm_load_state.store(VlmLoadState::Ready);
    HAILO_ANALYTICS_LOG_INFO("VlmEventMonitorPipeline: VLM ready — event-check + chat enabled");

    broadcast_monitoring_status();
}

vlm_event_monitor::MonitoringStatus VlmEventMonitorPipeline::build_monitoring_status() const
{
    vlm_event_monitor::MonitoringStatus status;

    VlmRuntime runtime_snapshot;
    {
        std::shared_lock lock(m_runtime_mutex);
        runtime_snapshot = m_runtime;
    }

    const VlmLoadState load_state = m_vlm_load_state.load();
    switch (load_state)
    {
    case VlmLoadState::Loading:
        status.vlm_state = "loading";
        break;
    case VlmLoadState::Ready:
        status.vlm_state = "ready";
        break;
    case VlmLoadState::Failed:
        status.vlm_state = "failed";
        break;
    }
    if (load_state == VlmLoadState::Failed)
    {
        status.vlm_error = vlm_load_error();
    }

    // Mode and event-list counts come from the EventStore regardless of
    // VLM state so the UI keeps showing them during loading.
    if (m_event_store)
    {
        const auto events = m_event_store->events();
        status.mode = vlm_event_monitor::to_string(m_event_store->mode());
        status.event_count = events.size();
        for (const auto &event : events)
        {
            if (event.enabled)
            {
                status.enabled_count++;
            }
        }
    }

    bool runner_running = false;
    if (runtime_snapshot.runner)
    {
        runner_running = runtime_snapshot.runner->is_running();
    }
    status.state = runner_running ? "running" : "stopped";

    if (runtime_snapshot.queue)
    {
        status.busy_with = runtime_snapshot.queue->busy_with();
        status.pending_count = runtime_snapshot.queue->pending_count();
        status.event_inference_enabled = runtime_snapshot.queue->is_event_inference_enabled();
    }

    if (runtime_snapshot.state_tracker)
    {
        status.active_incidents = runtime_snapshot.state_tracker->active_incident_count();
        status.cooldown_seconds = static_cast<uint32_t>(runtime_snapshot.state_tracker->cooldown().count());
    }
    else if (m_app_custom_data)
    {
        // Tracker not yet constructed (VLM still loading) — surface the YAML
        // default so the Settings-modal slider has a sensible initial value.
        status.cooldown_seconds = m_app_custom_data->m_config.event_check.cooldown_seconds;
    }

    // monitoring_pause_reason — single source of truth for the frontend
    // Monitoring badge. Order matters: VLM state dominates, then chat.
    if (load_state == VlmLoadState::Loading)
    {
        status.monitoring_pause_reason = "vlm_loading";
    }
    else if (load_state == VlmLoadState::Failed)
    {
        status.monitoring_pause_reason = "vlm_failed";
    }
    else if (runtime_snapshot.chat_broker && runtime_snapshot.chat_broker->any_session_open() &&
             !status.event_inference_enabled)
    {
        status.monitoring_pause_reason = "chat_active";
    }

    return status;
}

void VlmEventMonitorPipeline::broadcast_monitoring_status() const
{
    if (!m_sse_broadcaster)
    {
        return;
    }
    m_sse_broadcaster->push_monitoring_status(build_monitoring_status());
}

void VlmEventMonitorPipeline::log_components(const MediaStageComponents &components) const
{
    HAILO_ANALYTICS_LOG_INFO("FrontendStage name: {}", components.m_frontend_stage->get_name());
    auto streams = components.m_frontend_stage->get_outputs_streams();
    if (streams.has_value())
    {
        for (const auto &s : streams.value())
        {
            HAILO_ANALYTICS_LOG_INFO("  frontend output stream id={} resolution={}x{}", s.id, s.width, s.height);
        }
    }
    for (const auto &[stream_id, _] : components.m_encoder_stages)
    {
        HAILO_ANALYTICS_LOG_INFO("  encoder stage for stream id: {}", stream_id);
    }
}

tl::expected<PipelinePtr, CamAppReturnCode> VlmEventMonitorPipeline::build_pipeline(
    const MediaStageComponents &components)
{
    log_components(components);

    auto custom = std::dynamic_pointer_cast<VlmAppCustomData>(components.m_user_data);
    if (!custom)
    {
        HAILO_ANALYTICS_LOG_ERROR("VlmAppCustomData missing in build_pipeline");
        return tl::unexpected(CamAppReturnCode::FAILED);
    }

    PipelineBuilder pip_builder;

    const std::string frontend_stage_name = components.m_frontend_stage->get_name();
    pip_builder.add_stage(components.m_frontend_stage, StageType::SOURCE);

    // ── 4K → encoder → RTPConverter → WebRTCStreamerExt (Stage 1, unchanged) ──
    auto webrtc_streamer_ext = get_extension<WebRTCStreamerExt>();
    if (!webrtc_streamer_ext)
    {
        HAILO_ANALYTICS_LOG_ERROR("WebRTCStreamerExt extension is required but cannot be found");
        return tl::unexpected(CamAppReturnCode::FAILED);
    }

    auto enc_4k_it = components.m_encoder_stages.find(vlm_app::stream_id::stream_4k);
    if (enc_4k_it == components.m_encoder_stages.end())
    {
        HAILO_ANALYTICS_LOG_ERROR("Cannot find encoder stage for 4K stream id '{}'", vlm_app::stream_id::stream_4k);
        return tl::unexpected(CamAppReturnCode::FAILED);
    }

    std::shared_ptr<RTPConverterStage> main_4k_webrtc_stage = RTPConverterStageBuild::create()
                                                                  .set_stage_name(vlm_app::stage::main_4k_webrtc)
                                                                  .set_rtp_receiver(webrtc_streamer_ext)
                                                                  .set_session_name(vlm_app::stage::main_4k_webrtc)
                                                                  .set_leaky_opt(true)
                                                                  .buildptr();
    main_4k_webrtc_stage->configure(EncodingType::H264);

    pip_builder.add_stage(enc_4k_it->second.encoder_stage_ptr, StageType::SINK);
    pip_builder.add_stage(main_4k_webrtc_stage, StageType::SINK);

    pip_builder.connect_frontend(frontend_stage_name, vlm_app::stream_id::stream_4k, vlm_app::stream_id::stream_4k);
    pip_builder.connect(vlm_app::stream_id::stream_4k, vlm_app::stage::main_4k_webrtc);

    // ── VGA → JPEG encoder → JpegRingBufferStage ────────────────────
    auto enc_vga_it = components.m_encoder_stages.find(vlm_app::stream_id::stream_vga);
    if (enc_vga_it == components.m_encoder_stages.end())
    {
        HAILO_ANALYTICS_LOG_WARN("No encoder stage for VGA stream id '{}'; skipping JPEG path",
                                 vlm_app::stream_id::stream_vga);
    }
    else
    {
        constexpr size_t kJpegRingCapacity = 20;
        m_jpeg_ring =
            std::make_shared<vlm_event_monitor::JpegRingBufferStage>(vlm_app::stage::vga_jpeg_ring, kJpegRingCapacity);

        // The runner / chat broker (built on the background load thread)
        // will pick up the ring through the m_runtime snapshot once
        // run_vlm_load advances past m_pipeline_built_future.wait().

        pip_builder.add_stage(enc_vga_it->second.encoder_stage_ptr, StageType::SINK);
        pip_builder.add_stage(m_jpeg_ring, StageType::SINK);

        pip_builder.connect_frontend(frontend_stage_name, vlm_app::stream_id::stream_vga,
                                     vlm_app::stream_id::stream_vga);
        pip_builder.connect(vlm_app::stream_id::stream_vga, vlm_app::stage::vga_jpeg_ring);
    }

    // ── 336x336 NV12 → Nv12ToRgbStage → EventCheckRunner ────────────
    // We always use the fallback dims here — the medialib profile pins the
    // VlmInput stream to 336x336, and the runner is built after the manager
    // by the background load thread, so the manager's input dims aren't
    // available yet at build_pipeline time. If they ever diverge, the
    // runtime's chat preprocessor (sized from the manager) will catch it.
    // NOTE: Callback is installed by run_vlm_load once the runner exists. Until
    // then, converted RGB frames are simply dropped — which is what we
    // want during the load window.
    m_nv12_to_rgb = std::make_shared<vlm_event_monitor::Nv12ToRgbStage>(vlm_app::stage::nv12_to_rgb, kFallbackVlmHeight,
                                                                        kFallbackVlmWidth);

    pip_builder.add_stage(m_nv12_to_rgb, StageType::SINK);
    pip_builder.connect_frontend(frontend_stage_name, vlm_app::stream_id::stream_336, vlm_app::stage::nv12_to_rgb);

    auto result = pip_builder.build("VlmEventMonitorPipeline");

    // Signal the background load thread that m_jpeg_ring + m_nv12_to_rgb
    // are now visible. set_value() throws future_error if already
    // satisfied (e.g. destructor ran first during a failed boot); swallow
    // that — the load thread will see the satisfied future regardless.
    try
    {
        m_pipeline_built_promise.set_value();
    }
    catch (const std::future_error &)
    {
    }

    return result;
}
