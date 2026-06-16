#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <tl/expected.hpp>

#include "hailo/genai/vlm/vlm.hpp"
#include "hailo/hailort.hpp"

static constexpr std::chrono::milliseconds DEFAULT_BUSY_WAIT_TIMEOUT{5000};

// Configuration for VLM Inference Manager initialization.
struct VlmConfig
{
    std::string hef_path;
    std::string group_id = "device0";
    uint32_t default_max_generated_tokens = 256;
    std::chrono::milliseconds busy_wait_timeout = DEFAULT_BUSY_WAIT_TIMEOUT;
};

// A single inference request. Frames are preprocessed RGB pixel buffers
// produced by VlmFramePreprocessor — each element must be exactly
// input_frame_height() * input_frame_width() * input_frame_channels() bytes.
struct InferenceRequest
{
    std::vector<std::vector<uint8_t>> frames; // Preprocessed RGB frames (empty = text-only follow-up)
    std::string prompt;
    std::string system_prompt;         // Applied only on first inference of a session
    uint32_t max_generated_tokens = 0; // 0 = use default from config
    bool use_video_mode = false;       // false = input_frames, true = input_videos
};

// Statistics collected during generation.
struct VlmGenerationStats
{
    double ttft_ms = 0.0;
    double tf_ms = 0.0;
    double total_ms = 0.0;
    double tps = 0.0;
    size_t tokens_generated = 0;
    size_t context_usage = 0;
    size_t context_capacity = 0;
};

/// Result of an inference call.
struct InferenceResult
{
    std::string response;
    VlmGenerationStats stats;
};

// Per-token streaming callback. Return false to abort generation.
using TokenCallback = std::function<bool(const std::string &)>;

// Manages VLM inference with multi-session context save/restore.
// Only one generation runs at a time (enforced by mutex). Multiple sessions
// can coexist; the manager transparently saves and restores KV-cache context
// when switching between them.
class VlmInferenceManager
{
  public:
    VlmInferenceManager(const VlmInferenceManager &) = delete;
    VlmInferenceManager &operator=(const VlmInferenceManager &) = delete;

    // Factory. Creates VDevice, loads HEF, queries input shape.
    static tl::expected<std::shared_ptr<VlmInferenceManager>, std::string> create(const VlmConfig &config);

    // ── Session management ──────────────────────────────────────────────

    /// Create a new session. Returns session_id.
    tl::expected<uint32_t, std::string> create_session();

    /// Close a session and discard its saved context.
    tl::expected<void, std::string> close_session(uint32_t session_id);

    /// List active session IDs.
    std::vector<uint32_t> list_sessions() const;

    // ── Inference ───────────────────────────────────────────────────────

    // Run inference within a session.
    // First call must include at least one frame. Subsequent calls may
    // include new frames (added to conversation) or be text-only.
    // Context save/restore is handled internally when switching sessions.
    // on_token optional per-token callback, full response is always returned in
    // InferenceResult regardless.
    tl::expected<InferenceResult, std::string> infer(uint32_t session_id, const InferenceRequest &request,
                                                     TokenCallback on_token = nullptr);

    // One-shot inference with no session. Clears context before and after.
    // Suspends any active session first (restored lazily on next infer()).
    tl::expected<InferenceResult, std::string> infer_oneshot(const InferenceRequest &request,
                                                             TokenCallback on_token = nullptr);

    // ── Info ────────────────────────────────────────────────────────────

    tl::expected<size_t, std::string> get_context_usage();
    tl::expected<size_t, std::string> max_context_capacity();

    /// Model-expected input frame shape. Callers use these to build a
    /// VlmFramePreprocessor that matches the loaded HEF.
    uint32_t input_frame_height() const
    {
        return m_input_height;
    }
    uint32_t input_frame_width() const
    {
        return m_input_width;
    }
    uint32_t input_frame_channels() const
    {
        return m_input_channels;
    }

  private:
    VlmInferenceManager() = default;

    // ── Internal types ──────────────────────────────────────────────────

    struct SessionState
    {
        uint32_t session_id = 0;
        hailort::BufferPtr saved_context;
        bool first_inference = true;
    };

    // ── Internal helpers ────────────────────────────────────────────────

    // Try to acquire the inference lock within the configured timeout.
    tl::expected<std::unique_lock<std::timed_mutex>, std::string> acquire_lock();

    // Switch device context to the target session.
    tl::expected<void, std::string> activate_session(uint32_t session_id);

    // Save the currently active session's context off-device.
    tl::expected<void, std::string> suspend_active_session();

    // Build the JSON messages vector from a request.
    std::vector<std::string> build_messages(const InferenceRequest &request, bool include_system_prompt);

    // Core generate loop shared by infer() and infer_oneshot().
    tl::expected<InferenceResult, std::string> run_generate(const InferenceRequest &request, bool include_system_prompt,
                                                            TokenCallback on_token);

    // ── Members ─────────────────────────────────────────────────────────

    std::shared_ptr<hailort::VDevice> m_vdevice;
    std::unique_ptr<hailort::genai::VLM> m_vlm;
    std::optional<hailort::genai::LLMGeneratorParams> m_generator_params;
    VlmConfig m_config;

    mutable std::timed_mutex m_infer_mutex;
    mutable std::mutex m_session_mutex;
    uint32_t m_next_session_id = 1;
    uint32_t m_active_session_id = 0;
    std::unordered_map<uint32_t, SessionState> m_sessions;

    uint32_t m_input_height = 0;
    uint32_t m_input_width = 0;
    uint32_t m_input_channels = 0;
    bool m_initialized = false;
};
