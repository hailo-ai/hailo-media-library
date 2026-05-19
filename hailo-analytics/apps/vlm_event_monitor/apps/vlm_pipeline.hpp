#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>

#include <tl/expected.hpp>

#include "media_library/frontend.hpp"

#include "hailo_analytics/analytics/reference_camera_app_constructor.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/sinks/rtp_converter_stage.hpp"

#include "custom/event_monitor/chat_session_broker.hpp"
#include "custom/event_monitor/event_check_runner.hpp"
#include "custom/event_monitor/event_state_tracker.hpp"
#include "custom/event_monitor/event_store.hpp"
#include "custom/event_monitor/inference_request_queue.hpp"
#include "custom/inference/vlm_inference_manager.hpp"
#include "custom/pipeline/jpeg_ring_buffer_stage.hpp"
#include "custom/pipeline/nv12_to_rgb_stage.hpp"
#include "custom/service/sse_broadcaster.hpp"
#include "utils/vlm_app_config.hpp"
#include "vlm_pipeline_defines.hpp"

class VlmFramePreprocessor;

// VLM-specific UserData carried through CameraAppConstructor::AppConfigOverride.
struct VlmAppCustomData : public hailo_analytics::analytics::app_constructor::UserDataBase
{
    vlm_app_config::VlmAppConfig m_config;

    explicit VlmAppCustomData(vlm_app_config::VlmAppConfig config) : m_config(std::move(config))
    {
    }

    const char *type_name() const override
    {
        return "VlmAppCustomData";
    }
};

// The VLM-dependent runtime (inference manager, queue,
// runner, chat broker, state tracker, chat preprocessor) is now built on
// a background thread so the HTTP server can begin listening immediately.
// The non-VLM parts (event store, SSE broadcaster, JPEG ring, NV12→RGB
// stage, WebRTC) are still constructed synchronously during boot, so the
// frontend has everything it needs to render the live view + edit events
// + display "VLM loading" status right after app is launched
class VlmEventMonitorPipeline : public hailo_analytics::analytics::app_constructor::CameraAppConstructor
{
  public:
    enum class VlmLoadState
    {
        Loading,
        Ready,
        Failed,
    };

  private:
    // Cluster of components that depend on the VLM manager being loaded.
    // Built on the background thread; published atomically under
    // m_runtime_mutex.
    struct VlmRuntime
    {
        std::shared_ptr<VlmInferenceManager> manager;
        std::shared_ptr<vlm_event_monitor::InferenceRequestQueue> queue;
        std::shared_ptr<vlm_event_monitor::EventCheckRunner> runner;
        std::shared_ptr<vlm_event_monitor::ChatSessionBroker> chat_broker;
        std::shared_ptr<vlm_event_monitor::EventStateTracker> state_tracker;
        std::shared_ptr<VlmFramePreprocessor> chat_preprocessor;
    };

    std::shared_ptr<VlmAppCustomData> m_app_custom_data;

    // Built synchronously in build_pipeline (not VLM-dependent — just
    // wraps the encoder output and the NV12 frontend stream). The
    // background thread reads these after the build-completion signal.
    std::shared_ptr<vlm_event_monitor::JpegRingBufferStage> m_jpeg_ring;
    std::shared_ptr<vlm_event_monitor::Nv12ToRgbStage> m_nv12_to_rgb;

    // Built synchronously (cheap, no HEF dependency). The
    // EventStore apply callback (set during register_app_extensions) reads
    // the VLM runtime through m_runtime_mutex.
    std::shared_ptr<vlm_event_monitor::EventStore> m_event_store;
    std::shared_ptr<vlm_event_monitor::SseBroadcaster> m_sse_broadcaster;

    mutable std::shared_mutex m_runtime_mutex;
    VlmRuntime m_runtime;                                              // guarded by m_runtime_mutex
    std::atomic<VlmLoadState> m_vlm_load_state{VlmLoadState::Loading}; // independent of the mutex
    std::string m_vlm_load_error;                                      // guarded by m_runtime_mutex
    std::thread m_vlm_load_thread;
    std::atomic<bool> m_load_thread_should_join{false};

    // Set by build_pipeline once m_jpeg_ring and m_nv12_to_rgb are
    // constructed, so the background load thread can safely read them
    // when wiring callbacks.
    std::promise<void> m_pipeline_built_promise;
    std::shared_future<void> m_pipeline_built_future;

  public:
    VlmEventMonitorPipeline();
    ~VlmEventMonitorPipeline() override;

    std::shared_ptr<vlm_event_monitor::EventStore> event_store() const
    {
        return m_event_store;
    }
    std::shared_ptr<vlm_event_monitor::SseBroadcaster> sse_broadcaster() const
    {
        return m_sse_broadcaster;
    }
    std::shared_ptr<vlm_event_monitor::ChatSessionBroker> chat_broker() const;
    std::shared_ptr<vlm_event_monitor::EventStateTracker> event_state_tracker() const;

    VlmLoadState vlm_load_state() const
    {
        return m_vlm_load_state.load();
    }
    std::string vlm_load_error() const;
    bool vlm_ready() const
    {
        return vlm_load_state() == VlmLoadState::Ready;
    }

    // Build a fresh MonitoringStatus snapshot from current pipeline /
    // runtime / event-store state. The webserver delegates to this so
    // both the SSE greeting path and the GET /api/monitoring/status route
    // share one source of truth. Safe to call from any thread.
    vlm_event_monitor::MonitoringStatus build_monitoring_status() const;

  protected:
    hailo_analytics::analytics::app_constructor::CamAppReturnCode register_app_extensions(
        std::shared_ptr<hailo_analytics::analytics::app_constructor::UserDataBase> user_data) override;

    std::string default_media_config() const override;

    std::string main_stream_encoder_id(
        const hailo_analytics::analytics::app_constructor::MediaStageComponents &components) const override;

    std::string main_stream_frontend_output_id(
        const hailo_analytics::analytics::app_constructor::MediaStageComponents &components) const override;

    tl::expected<hailo_analytics::analytics::app_constructor::PipelinePtr,
                 hailo_analytics::analytics::app_constructor::CamAppReturnCode>
    build_pipeline(const hailo_analytics::analytics::app_constructor::MediaStageComponents &components) override;

  private:
    void log_components(const hailo_analytics::analytics::app_constructor::MediaStageComponents &components) const;

    // Best-effort VLM init. Returns nullptr on failure (model missing, HEF
    // load error, etc.) and logs the reason. The 4K and JPEG paths continue
    // to work without VLM; only event-check inference is disabled.
    std::shared_ptr<VlmInferenceManager> try_create_vlm_manager() const;

    // Spawn the background HEF load thread. Called from register_app_extensions
    // after the synchronous (non-VLM) setup completes.
    void start_vlm_load_async();

    // Body of the background thread. Loads the HEF, builds the runtime
    // members under the write lock, wires the NV12→RGB callback, starts
    // the queue/runner/broker, registers the chat-pause notifier, and
    // broadcasts a fresh monitoring_status. On failure, publishes an
    // error string and broadcasts the failed state.
    void run_vlm_load();

    // Build a fresh MonitoringStatus and push it via m_sse_broadcaster.
    // Used by the background load thread, the EventStore apply callback,
    // and the ChatSessionBroker pause-state notifier.
    void broadcast_monitoring_status() const;
};
