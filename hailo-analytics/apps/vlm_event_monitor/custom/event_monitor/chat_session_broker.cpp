#include "chat_session_broker.hpp"

#include <utility>

#include <nlohmann/json.hpp>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

#include "custom/pipeline/jpeg_ring_buffer_stage.hpp"
#include "custom/service/sse_broadcaster.hpp"
#include "inference_request_queue.hpp"

namespace vlm_event_monitor
{

namespace
{

constexpr std::chrono::seconds kJanitorInterval{30};

// Standard library base64 alphabet (mirror of sse_broadcaster.cpp's
// table-driven encoder; we re-encode here so the module is self-contained
// and doesn't pull SseBroadcaster into the broker translation unit).
const std::string kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const uint8_t *data, size_t len)
{
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t index = 0;
    while (index + 3 <= len)
    {
        const uint32_t triple = (static_cast<uint32_t>(data[index]) << 16) |
                                (static_cast<uint32_t>(data[index + 1]) << 8) | static_cast<uint32_t>(data[index + 2]);
        out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 6) & 0x3F]);
        out.push_back(kBase64Alphabet[triple & 0x3F]);
        index += 3;
    }
    if (index < len)
    {
        const uint32_t b0 = data[index];
        const uint32_t b1 = (index + 1 < len) ? data[index + 1] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8);
        out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
        out.push_back((index + 1 < len) ? kBase64Alphabet[(triple >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

// Pre-format a single SSE frame body. Wire format:
//   data: <json>\n\n
// Chat streams use unnamed events (default `message` SSE type).
std::string format_data_frame(const nlohmann::json &payload)
{
    std::string body = payload.dump();
    std::string frame;
    frame.reserve(body.size() + 8);
    frame.append("data: ").append(body).append("\n\n");
    return frame;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// ChatStreamHandler
// ─────────────────────────────────────────────────────────────────────────────

void ChatStreamHandler::push_token(const std::string &token)
{
    nlohmann::json payload = {{"token", token}};
    std::string frame = format_data_frame(payload);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_client_disconnected || m_done)
        {
            return;
        }
        m_pending_frames.push_back(std::move(frame));
    }
    m_cv.notify_one();
}

void ChatStreamHandler::finalize(tl::expected<InferenceResult, std::string> result, std::chrono::milliseconds wait_ms,
                                 uint32_t max_generated_tokens)
{
    nlohmann::json payload;
    payload["done"] = true;
    if (result.has_value())
    {
        payload["full_response"] = result->response;
        payload["stats"] = {
            {"ttft_ms", result->stats.ttft_ms},
            {"tps", result->stats.tps},
            {"total_ms", result->stats.total_ms},
            {"wait_ms", static_cast<double>(wait_ms.count())},
            {"tokens_generated", result->stats.tokens_generated},
            {"max_generated_tokens", max_generated_tokens},
            {"context_usage", result->stats.context_usage},
            {"context_capacity", result->stats.context_capacity},
        };
    }
    else
    {
        payload["error"] = result.error();
        payload["stats"] = {
            {"ttft_ms", 0.0},        {"tps", 0.0},
            {"total_ms", 0.0},       {"wait_ms", static_cast<double>(wait_ms.count())},
            {"tokens_generated", 0}, {"max_generated_tokens", max_generated_tokens},
        };
    }

    std::string frame = format_data_frame(payload);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending_frames.push_back(std::move(frame));
        m_done = true;
    }
    m_cv.notify_all();
}

std::string ChatStreamHandler::next_frame_or_block()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    // Block (with a max wait) so we can periodically observe disconnect
    // even if no new tokens arrive.
    m_cv.wait_for(lock, std::chrono::seconds(1),
                  [this] { return !m_pending_frames.empty() || m_done || m_client_disconnected; });

    if (!m_pending_frames.empty())
    {
        std::string frame = std::move(m_pending_frames.front());
        m_pending_frames.pop_front();
        return frame;
    }

    if (m_client_disconnected || m_done)
    {
        return std::string{}; // nothing more to send
    }

    return std::string{}; // periodic wakeup with nothing yet — try again next tick
}

void ChatStreamHandler::mark_client_disconnected()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_client_disconnected = true;
    }
    m_cv.notify_all();
}

bool ChatStreamHandler::client_disconnected() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_client_disconnected;
}

bool ChatStreamHandler::finished() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_done && m_pending_frames.empty();
}

// ─────────────────────────────────────────────────────────────────────────────
// ChatSessionBroker
// ─────────────────────────────────────────────────────────────────────────────

ChatSessionBroker::ChatSessionBroker(std::shared_ptr<InferenceRequestQueue> queue,
                                     std::shared_ptr<JpegRingBufferStage> jpeg_ring,
                                     std::shared_ptr<VlmFramePreprocessor> preprocessor,
                                     std::shared_ptr<SseBroadcaster> sse_broadcaster, Config config)
    : m_queue(std::move(queue)), m_jpeg_ring(std::move(jpeg_ring)), m_preprocessor(std::move(preprocessor)),
      m_sse_broadcaster(std::move(sse_broadcaster)), m_config(config)
{
}

ChatSessionBroker::~ChatSessionBroker()
{
    stop();
}

void ChatSessionBroker::start()
{
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true))
    {
        return;
    }
    m_janitor = std::thread(&ChatSessionBroker::janitor_loop, this);
    HAILO_ANALYTICS_LOG_INFO("ChatSessionBroker: started (timeout={}s, pause_event_check_during_chat={}, "
                             "default_max_tokens={})",
                             m_config.session_timeout_seconds, m_config.pause_event_check_during_chat,
                             m_config.default_max_generated_tokens);
}

void ChatSessionBroker::set_jpeg_ring(std::shared_ptr<JpegRingBufferStage> jpeg_ring)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_jpeg_ring = std::move(jpeg_ring);
}

void ChatSessionBroker::stop()
{
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false))
    {
        return;
    }
    m_janitor_cv.notify_all();
    if (m_janitor.joinable())
    {
        m_janitor.join();
    }

    // Close any remaining sessions on shutdown
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &[id, session] : m_sessions)
    {
        if (auto handler = session->active_handler.lock())
        {
            handler->mark_client_disconnected();
        }
        if (m_queue && m_queue->manager())
        {
            (void)m_queue->manager()->close_session(id);
        }
    }
    m_sessions.clear();
    on_session_count_changed_locked();
}

tl::expected<std::vector<std::vector<uint8_t>>, std::string> ChatSessionBroker::preprocess_jpegs(
    const std::vector<std::vector<uint8_t>> &jpegs) const
{
    if (!m_preprocessor)
    {
        return tl::make_unexpected("VlmFramePreprocessor not available");
    }
    std::vector<std::vector<uint8_t>> rgb_frames;
    rgb_frames.reserve(jpegs.size());
    for (size_t index = 0; index < jpegs.size(); index++)
    {
        auto rgb = m_preprocessor->preprocess_jpeg(jpegs[index]);
        if (!rgb)
        {
            return tl::make_unexpected("preprocess_jpeg failed at frame " + std::to_string(index) + ": " + rgb.error());
        }
        rgb_frames.push_back(std::move(rgb.value()));
    }
    return rgb_frames;
}

void ChatSessionBroker::evict_all_locked()
{
    if (m_sessions.empty())
    {
        return;
    }
    // Snapshot ids first since we modify m_sessions inside the loop.
    std::vector<uint32_t> ids;
    ids.reserve(m_sessions.size());
    for (const auto &[id, _] : m_sessions)
    {
        ids.push_back(id);
    }
    for (uint32_t id : ids)
    {
        auto it = m_sessions.find(id);
        if (it == m_sessions.end())
        {
            continue;
        }
        if (auto handler = it->second->active_handler.lock())
        {
            // Forces the on_token callback to return false on the next
            // token, aborting infer() inside the manager. close_session
            // below will then unblock as soon as the manager mutex is
            // released.
            handler->mark_client_disconnected();
        }
        HAILO_ANALYTICS_LOG_INFO("ChatSessionBroker: auto-evicting orphan session {}", id);
        if (m_queue && m_queue->manager())
        {
            auto close_result = m_queue->manager()->close_session(id);
            if (!close_result)
            {
                HAILO_ANALYTICS_LOG_WARN("ChatSessionBroker: close_session({}) failed: {}", id, close_result.error());
            }
        }
    }
    m_sessions.clear();
    on_session_count_changed_locked();
}

void ChatSessionBroker::on_session_count_changed_locked()
{
    if (!m_queue)
    {
        return;
    }
    const bool desired = m_config.pause_event_check_during_chat && !m_sessions.empty();
    if (m_chat_pause_active.exchange(desired) != desired)
    {
        m_queue->set_event_inference_enabled(!desired);
    }
}

bool ChatSessionBroker::any_session_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_sessions.empty();
}

void ChatSessionBroker::set_pause_state_change_notifier(std::function<void()> notifier)
{
    std::lock_guard<std::mutex> lock(m_pause_notifier_mutex);
    m_pause_notifier = std::move(notifier);
}

void ChatSessionBroker::fire_pause_notifier()
{
    std::function<void()> notifier_copy;
    {
        std::lock_guard<std::mutex> lock(m_pause_notifier_mutex);
        notifier_copy = m_pause_notifier;
    }
    if (notifier_copy)
    {
        notifier_copy();
    }
}

tl::expected<ChatSessionBroker::StartResult, std::string> ChatSessionBroker::start_event_chat(
    const std::vector<std::vector<uint8_t>> &jpeg_frames)
{
    PauseNotifyOnExit notify_guard(*this);

    if (jpeg_frames.empty())
    {
        return tl::make_unexpected("frames must not be empty");
    }
    if (!m_queue || !m_queue->manager())
    {
        return tl::make_unexpected("VLM manager not available");
    }

    auto rgb = preprocess_jpegs(jpeg_frames);
    if (!rgb)
    {
        return tl::make_unexpected(rgb.error());
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    evict_all_locked();

    auto created = m_queue->manager()->create_session();
    if (!created)
    {
        return tl::make_unexpected("create_session failed: " + created.error());
    }

    auto session = std::make_shared<ChatSession>();
    session->session_id = created.value();
    session->stashed_rgb_frames = std::move(rgb.value());
    session->last_activity_at = std::chrono::steady_clock::now();
    m_sessions[session->session_id] = session;
    on_session_count_changed_locked();

    HAILO_ANALYTICS_LOG_INFO("ChatSessionBroker: event-chat session {} created with {} frame(s)", session->session_id,
                             jpeg_frames.size());

    StartResult result;
    result.session_id = session->session_id;
    result.frame_count = jpeg_frames.size();
    return result;
}

tl::expected<ChatSessionBroker::StartResult, std::string> ChatSessionBroker::start_live_chat()
{
    PauseNotifyOnExit notify_guard(*this);

    if (!m_queue || !m_queue->manager())
    {
        return tl::make_unexpected("VLM manager not available");
    }
    if (!m_jpeg_ring)
    {
        return tl::make_unexpected("JPEG ring not available");
    }

    auto latest = m_jpeg_ring->latest();
    if (!latest.has_value())
    {
        return tl::make_unexpected("JPEG cache is empty (no frames captured yet)");
    }

    std::vector<std::vector<uint8_t>> jpegs{std::move(latest.value())};
    auto rgb = preprocess_jpegs(jpegs);
    if (!rgb)
    {
        return tl::make_unexpected(rgb.error());
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    evict_all_locked();

    auto created = m_queue->manager()->create_session();
    if (!created)
    {
        return tl::make_unexpected("create_session failed: " + created.error());
    }

    auto session = std::make_shared<ChatSession>();
    session->session_id = created.value();
    session->stashed_rgb_frames = std::move(rgb.value());
    session->last_activity_at = std::chrono::steady_clock::now();
    m_sessions[session->session_id] = session;
    on_session_count_changed_locked();

    HAILO_ANALYTICS_LOG_INFO("ChatSessionBroker: live-chat session {} created", session->session_id);

    StartResult result;
    result.session_id = session->session_id;
    result.frame_count = 1;
    result.snapshot_base64 = base64_encode(jpegs[0].data(), jpegs[0].size());
    return result;
}

tl::expected<void, std::string> ChatSessionBroker::run_stream(uint32_t session_id, const std::string &prompt,
                                                              uint32_t max_generated_tokens,
                                                              std::shared_ptr<ChatStreamHandler> handler)
{
    if (!handler)
    {
        return tl::make_unexpected("handler is null");
    }
    if (!m_queue)
    {
        return tl::make_unexpected("inference queue not available");
    }

    InferenceRequest request;
    request.prompt = prompt;
    request.max_generated_tokens =
        max_generated_tokens > 0 ? max_generated_tokens : m_config.default_max_generated_tokens;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sessions.find(session_id);
        if (it == m_sessions.end())
        {
            return tl::make_unexpected("session_not_found");
        }
        auto &session = it->second;
        session->last_activity_at = std::chrono::steady_clock::now();
        session->active_handler = handler;

        if (!session->first_inference_done)
        {
            // First /chat/stream consumes the stashed frames.
            request.frames = std::move(session->stashed_rgb_frames);
            session->stashed_rgb_frames.clear();
            request.use_video_mode = request.frames.size() > 1;
            session->first_inference_done = true;
        }
        // else: text-only follow-up; request.frames stays empty
    }

    auto enqueued_at = std::chrono::steady_clock::now();
    auto handler_weak = std::weak_ptr<ChatStreamHandler>(handler);
    const uint32_t resolved_max_tokens = request.max_generated_tokens;

    InferenceJob job;
    job.priority = InferencePriority::Chat;
    job.enqueued_at = enqueued_at;

    // Capture by value: request holds the RGB frames + prompt, session_id
    // identifies the manager session. handler is captured weakly so the
    // queue worker doesn't extend the handler's lifetime past the request.
    job.work = [request = std::move(request), session_id,
                handler_weak](VlmInferenceManager &manager) -> tl::expected<InferenceResult, std::string> {
        TokenCallback on_token = [handler_weak](const std::string &token) -> bool {
            auto h = handler_weak.lock();
            if (!h)
            {
                return false;
            }
            if (h->client_disconnected())
            {
                return false;
            }
            h->push_token(token);
            return true;
        };
        return manager.infer(session_id, request, std::move(on_token));
    };

    // Strong handler capture (everywhere else this broker uses weak_ptr).
    // The work lambda runs best-effort — if the client disconnected, weak
    // captures expire and streaming stops. on_complete is different: its
    // cleanup (clearing active_handler, bumping last_activity_at to keep
    // the janitor from reaping a session that just finished, delivering
    // the final {done:true} SSE frame) MUST run regardless of whether the
    // HTTP side is still alive. Capturing handler strongly here pins it
    // alive until finalize() returns; finalize() then writes into the
    // handler's queue, which the HTTP thread either drains (client still
    // connected) or destroys harmlessly (client already gone).
    job.on_complete = [handler, enqueued_at, session_id, resolved_max_tokens,
                       this](tl::expected<InferenceResult, std::string> result) {
        const auto now = std::chrono::steady_clock::now();
        auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - enqueued_at);

        // Calculate the amount of time it took to respond (just an approximate)
        if (result.has_value())
        {
            auto total_ms_elapsed = std::chrono::milliseconds(static_cast<int64_t>(result.value().stats.total_ms));
            if (total_ms_elapsed < wait_ms)
            {
                wait_ms -= total_ms_elapsed;
            }
            else
            {
                wait_ms = std::chrono::milliseconds(0);
            }
        }

        // Refresh last_activity_at so janitor_loop doesn't reap this session
        // the instant a long inference returns: it sweeps when
        // now - last_activity_at > session_timeout_seconds, and the previous
        // bump was at submission.
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_sessions.find(session_id);
            if (it != m_sessions.end())
            {
                it->second->last_activity_at = std::chrono::steady_clock::now();
                it->second->active_handler.reset();
            }
        }

        handler->finalize(std::move(result), wait_ms, resolved_max_tokens);
    };

    if (!m_queue->submit(std::move(job)))
    {
        return tl::make_unexpected("inference queue rejected the job");
    }
    return {};
}

tl::expected<void, std::string> ChatSessionBroker::close_session(uint32_t session_id)
{
    PauseNotifyOnExit notify_guard(*this);

    std::shared_ptr<ChatSession> session;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sessions.find(session_id);
        if (it == m_sessions.end())
        {
            return tl::make_unexpected("session_not_found");
        }
        session = it->second;
        m_sessions.erase(it);
        on_session_count_changed_locked();
    }
    if (auto handler = session->active_handler.lock())
    {
        handler->mark_client_disconnected();
    }
    if (m_queue && m_queue->manager())
    {
        auto close_result = m_queue->manager()->close_session(session_id);
        if (!close_result)
        {
            HAILO_ANALYTICS_LOG_WARN("ChatSessionBroker: close_session({}) failed: {}", session_id,
                                     close_result.error());
        }
    }
    HAILO_ANALYTICS_LOG_INFO("ChatSessionBroker: explicit close session {}", session_id);
    return {};
}

void ChatSessionBroker::janitor_loop()
{
    HAILO_ANALYTICS_LOG_INFO("ChatSessionBroker: janitor thread started");
    while (m_running.load())
    {
        // Per-iteration guard: fires pause notifier if THIS sweep
        // transitions the pause state (typical: last-session-evicted-by-
        // timeout flips from paused→unpaused so the frontend can re-enable
        // its monitoring badge).
        PauseNotifyOnExit notify_guard(*this);
        {
            std::unique_lock<std::mutex> lock(m_janitor_mutex);
            m_janitor_cv.wait_for(lock, kJanitorInterval, [this] { return !m_running.load(); });
            if (!m_running.load())
            {
                break;
            }
        }

        const auto timeout = std::chrono::seconds(m_config.session_timeout_seconds);
        const auto now = std::chrono::steady_clock::now();

        std::vector<uint32_t> expired_ids;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto it = m_sessions.begin(); it != m_sessions.end();)
            {
                if (now - it->second->last_activity_at > timeout)
                {
                    if (auto handler = it->second->active_handler.lock())
                    {
                        handler->mark_client_disconnected();
                    }
                    if (m_queue && m_queue->manager())
                    {
                        auto close_result = m_queue->manager()->close_session(it->first);
                        if (!close_result)
                        {
                            HAILO_ANALYTICS_LOG_WARN("ChatSessionBroker: close_session({}) failed during "
                                                     "janitor sweep: {}",
                                                     it->first, close_result.error());
                        }
                    }
                    expired_ids.push_back(it->first);
                    HAILO_ANALYTICS_LOG_INFO("ChatSessionBroker: EXPIRED session={} idle>{}s", it->first,
                                             m_config.session_timeout_seconds);
                    it = m_sessions.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            on_session_count_changed_locked();
        }

        // Push the SSE notice outside the broker mutex — fan-out walks
        // SseBroadcaster's client set and we don't want to block other
        // /chat/start callers during it.
        if (m_sse_broadcaster)
        {
            const std::string message = "Inactivity " + std::to_string(m_config.session_timeout_seconds / 60) +
                                        " minute timeout, please close this session and start again";
            for (uint32_t id : expired_ids)
            {
                m_sse_broadcaster->push_chat_session_expired(id, "idle_timeout", message);
            }
        }
    }
    HAILO_ANALYTICS_LOG_INFO("ChatSessionBroker: janitor thread stopped");
}

} // namespace vlm_event_monitor
