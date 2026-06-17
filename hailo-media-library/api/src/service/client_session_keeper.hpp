#pragma once

#include "media_library/media_library_logger.hpp"
#include "media_library/media_library_types.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

/**
 * @brief Owns a media library client's session lifecycle: initial acquisition,
 *        keepalive, and reconnection after loss.
 *
 * This class has zero knowledge of gRPC *or* of the client's configuration.
 * It exposes three callback slots that the client wires up to its own
 * transport layer. The keeper drives the orchestration the client implements *how* each operation is performed
 * and owns any config state it needs internally.
 *
 * Callback contract — all are synchronous and must block until the underlying
 * RPC completes:
 *   - InvokeInitialize: attempt Initialize; return status + whether the
 *                       failure is transient (retryable). Called only from
 *                       initialize() — never from the reconnect loop.
 *   - InvokeKeepalive:  return true if the session is still ours.
 *   - InvokeReconnect:  attempt Reconnect; return SessionAcquired,
 *                       SessionRejected, or TransportError.
 *
 * Typical usage:
 *   keeper.subscribe_invoke_initialize([this]{ return do_init(); });
 *   keeper.subscribe_invoke_keepalive(...);
 *   keeper.subscribe_invoke_reconnect(...);
 *   auto ret = keeper.initialize();   // blocks with retry until success or stop()
 *   // ... normal use ...
 *   keeper.stop();                    // called from the client destructor
 */
class ClientSessionKeeper
{
  public:
    static constexpr std::chrono::seconds KEEPALIVE_INTERVAL{1};

    enum class ReconnectResult
    {
        SessionAcquired, ///< Server granted the session.
        SessionRejected, ///< Server is initialized but declined (uninitialized or other client).
        TransportError,  ///< gRPC transport error; cannot tell what happened.
    };

    /** @brief Result of an Initialize attempt returned by the client callback. */
    struct InitializeResult
    {
        media_library_return status;
        bool retryable;
    };

    using InvokeInitialize = std::function<InitializeResult()>;
    using InvokeKeepalive = std::function<bool()>;
    using InvokeReconnect = std::function<ReconnectResult()>;
    using OnSessionLost = std::function<void()>;
    using OnSessionReacquired = std::function<void()>;

    ClientSessionKeeper() = default;
    ~ClientSessionKeeper()
    {
        stop();
    }

    ClientSessionKeeper(const ClientSessionKeeper &) = delete;
    ClientSessionKeeper &operator=(const ClientSessionKeeper &) = delete;

    void subscribe_invoke_initialize(InvokeInitialize fn)
    {
        m_invoke_initialize = std::move(fn);
    }
    void subscribe_invoke_keepalive(InvokeKeepalive fn)
    {
        m_invoke_keepalive = std::move(fn);
    }
    void subscribe_invoke_reconnect(InvokeReconnect fn)
    {
        m_invoke_reconnect = std::move(fn);
    }

    /**
     * @brief Fired once per Active → Reconnecting transition (on the first
     *        keepalive failure). The owning client should treat its server-side
     *        state as gone and release any resources tied to the lost session
     *        (e.g., buffers received from the server).
     *
     * Invoked on the keeper's background thread.
     */
    void subscribe_on_session_lost(OnSessionLost fn)
    {
        m_on_session_lost = std::move(fn);
    }

    /**
     * @brief Fired once per Reconnecting → Active transition (after a
     *        successful Reconnect RPC). The owning client should restore any
     *        per-session state it needs (e.g., re-apply config, re-subscribe
     *        to event streams, re-establish buffer streams).
     *
     * Not fired on the very first successful Initialize — only on a true
     * reacquisition. Invoked on the keeper's background thread.
     */
    void subscribe_on_session_reacquired(OnSessionReacquired fn)
    {
        m_on_session_reacquired = std::move(fn);
    }

    /**
     * @brief Acquire the session with retry.
     *
     * Invokes the client's Initialize callback, and — on transient failures —
     * retries until success or stop(). On the first
     * SUCCESS, starts the background keepalive thread and returns.
     * Non-retryable failures are returned immediately.
     */
    media_library_return initialize()
    {
        while (m_alive)
        {
            auto result = m_invoke_initialize();
            if (result.status == media_library_return::MEDIA_LIBRARY_SUCCESS)
            {
                start_thread();
                return result.status;
            }
            if (!result.retryable)
                return result.status;

            LOGGER__MODULE__WARN(LoggerType::ServiceClient, "Initialize deferred, retrying in {} ms",
                                 KEEPALIVE_INTERVAL.count());
            if (wait_for_stop(KEEPALIVE_INTERVAL))
                return media_library_return::MEDIA_LIBRARY_ERROR;
        }
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    /** @brief Stop the thread and wake any in-flight sleep. Idempotent. */
    void stop()
    {
        m_alive = false;
        m_session_cv.notify_all();
        if (m_thread.joinable())
            m_thread.join();
    }

    bool is_alive() const
    {
        return m_alive.load();
    }

  private:
    enum class SessionState
    {
        Active,       ///< Healthy — heartbeat on the normal cadence.
        Reconnecting, ///< Keepalive failed — retry session reacquisition.
    };

    void start_thread()
    {
        if (m_thread.joinable())
            return;
        m_thread = std::thread([this]() { session_loop(); });
    }

    bool wait_for_stop(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m_session_mutex);
        m_session_cv.wait_for(lock, timeout, [this]() { return !m_alive.load(); });
        return !m_alive.load();
    }

    // Unified session state-machine loop. Exactly one timer is active at any
    // moment: KEEPALIVE_INTERVAL in Active state
    void session_loop()
    {
        SessionState state = SessionState::Active;
        while (m_alive)
        {
            const auto delay = std::chrono::milliseconds{KEEPALIVE_INTERVAL};
            if (wait_for_stop(delay))
                return;

            if (state == SessionState::Active)
            {
                if (!m_invoke_keepalive())
                {
                    LOGGER__MODULE__WARN(LoggerType::ServiceClient, "KeepAlive failed — entering reconnection state");
                    state = SessionState::Reconnecting;
                    if (m_on_session_lost)
                        m_on_session_lost();
                }
            }
            else
            {
                switch (m_invoke_reconnect())
                {
                case ReconnectResult::SessionAcquired:
                    LOGGER__MODULE__INFO(LoggerType::ServiceClient, "Session re-acquired");
                    state = SessionState::Active;
                    if (m_on_session_reacquired)
                        m_on_session_reacquired();
                    break;
                case ReconnectResult::SessionRejected:
                    LOGGER__MODULE__WARN(LoggerType::ServiceClient,
                                         "Reconnect rejected by server — will retry in {} ms",
                                         KEEPALIVE_INTERVAL.count());
                    break;
                case ReconnectResult::TransportError:
                    LOGGER__MODULE__WARN(LoggerType::ServiceClient, "Reconnect transport error — will retry in {} ms",
                                         KEEPALIVE_INTERVAL.count());
                    break;
                }
            }
        }
    }

    InvokeInitialize m_invoke_initialize;
    InvokeKeepalive m_invoke_keepalive;
    InvokeReconnect m_invoke_reconnect;
    OnSessionLost m_on_session_lost;
    OnSessionReacquired m_on_session_reacquired;

    std::atomic<bool> m_alive{true};
    std::thread m_thread;
    std::mutex m_session_mutex;
    std::condition_variable m_session_cv;
};
