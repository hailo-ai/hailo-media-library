#pragma once

#include "media_library/media_library_logger.hpp"
#include <chrono>
#include <functional>
#include <mutex>
#include <string>

/**
 * @brief Manages exclusive single-client access to the media library service.
 *
 * Only one client may hold an active session at a time. Clients acquire a session
 * via try_acquire() and must periodically send keepalive() signals to maintain it.
 * If a client's session expires (no keepalive within KEEPALIVE_TIMEOUT), a new client
 * may take over the session. Observers can subscribe to connection/disconnection events
 * to react to session lifecycle changes (e.g., applying or reverting client-specific
 * configuration).
 *
 * All public methods are thread-safe.
 */
class ClientSessionManager
{
  public:
    /** @brief Callback invoked when a new client acquires the session. */
    using OnConnected = std::function<void(const std::string &client_id)>;
    /** @brief Callback invoked when a client's session ends (released or expired). */
    using OnDisconnected = std::function<void(const std::string &client_id)>;

    /** @brief Duration after which a session without keepalive is considered expired. */
    static constexpr std::chrono::seconds KEEPALIVE_TIMEOUT{3};

    /**
     * @brief Register a callback for client connection events.
     * @param callback Called with the client_id when a new session is acquired.
     *                 Replaces any previously registered callback.
     */
    void subscribe_on_connected(OnConnected callback)
    {
        m_on_connected = std::move(callback);
    }

    /**
     * @brief Register a callback for client disconnection events.
     * @param callback Called with the client_id when a session is released or expires.
     *                 Replaces any previously registered callback.
     */
    void subscribe_on_disconnected(OnDisconnected callback)
    {
        m_on_disconnected = std::move(callback);
    }

    /**
     * @brief Attempt to acquire or refresh an exclusive client session.
     *
     * Behavior depends on the current state:
     * - No active session: the requesting client acquires the session.
     * - Same client: the keepalive timer is reset (idempotent re-acquire).
     * - Different client, active session expired: the expired session is evicted
     *   and the new client acquires the session.
     * - Different client, active session alive: the request is rejected.
     *
     * @param client_id Unique identifier for the requesting client.
     * @return true if the client now holds the active session, false if rejected.
     */
    bool try_acquire(const std::string &client_id)
    {
        bool should_notify_disconnect = false;
        bool should_notify_connect = false;
        std::string disconnect_id;
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            // Evict expired session held by a different client
            if (!m_active_client_id.empty() && m_active_client_id != client_id && is_expired())
            {
                LOGGER__MODULE__WARN(LoggerType::Service, "Client session expired (no keepalive): {}",
                                     m_active_client_id);
                disconnect_id = m_active_client_id;
                m_active_client_id.clear();
                should_notify_disconnect = true;
            }

            // No active session — grant to the requesting client
            if (m_active_client_id.empty())
            {
                m_active_client_id = client_id;
                m_last_keepalive = std::chrono::steady_clock::now();
                LOGGER__MODULE__INFO(LoggerType::Service, "Client session acquired: {}", client_id);
                should_notify_connect = true;
            }
            // Same client re-acquiring — just reset the keepalive timer
            else if (m_active_client_id == client_id)
            {
                m_last_keepalive = std::chrono::steady_clock::now();
            }
            // Another client holds a non-expired session — reject
            else
            {
                LOGGER__MODULE__WARN(LoggerType::Service, "Client session rejected (active: {}): {}",
                                     m_active_client_id, client_id);
                return false;
            }
        }

        if (should_notify_disconnect)
            notify_disconnected(disconnect_id);
        if (should_notify_connect)
            notify_connected(client_id);
        return true;
    }

    /**
     * @brief Explicitly release the active session.
     *
     * Only the client that currently holds the session may release it.
     *
     * @param client_id Identifier of the client requesting the release.
     * @return true if the session was released, false if client_id does not match.
     */
    bool release(const std::string &client_id)
    {
        bool should_notify_disconnect = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_active_client_id == client_id)
            {
                LOGGER__MODULE__INFO(LoggerType::Service, "Client session released: {}", client_id);
                m_active_client_id.clear();
                should_notify_disconnect = true;
            }
            else
            {
                LOGGER__MODULE__WARN(LoggerType::Service, "Client session release ignored (active: {}): {}",
                                     m_active_client_id, client_id);
                return false;
            }
        }

        if (should_notify_disconnect)
            notify_disconnected(client_id);
        return true;
    }

    /**
     * @brief Release the currently active session regardless of client identity.
     *
     * Used when the caller does not know (or send) a client_id, e.g., a legacy
     * Shutdown RPC that predates the client_id field.
     *
     * @return true if a session was released, false if no session was active.
     */
    bool release_active()
    {
        std::string released_id;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_active_client_id.empty())
                return false;
            LOGGER__MODULE__INFO(LoggerType::Service, "Active client session released: {}", m_active_client_id);
            released_id = m_active_client_id;
            m_active_client_id.clear();
        }

        notify_disconnected(released_id);
        return true;
    }

    /**
     * @brief Refresh the keepalive timer for the active session.
     *
     * Clients must call this periodically (more frequently than KEEPALIVE_TIMEOUT)
     * to prevent their session from being considered expired and evicted by a new client.
     *
     * @param client_id Identifier of the client sending the keepalive.
     * @return true if the timer was refreshed, false if client_id does not match the active session.
     */
    bool keepalive(const std::string &client_id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_active_client_id == client_id)
        {
            m_last_keepalive = std::chrono::steady_clock::now();
            return true;
        }
        return false;
    }

  private:
    /** @brief Check whether the active session has exceeded the keepalive timeout. */
    bool is_expired() const
    {
        return (std::chrono::steady_clock::now() - m_last_keepalive) > KEEPALIVE_TIMEOUT;
    }

    /** @brief Invoke the on-connected callback if one is registered. */
    void notify_connected(const std::string &client_id)
    {
        if (m_on_connected)
            m_on_connected(client_id);
    }

    /** @brief Invoke the on-disconnected callback if one is registered. */
    void notify_disconnected(const std::string &client_id)
    {
        if (m_on_disconnected)
            m_on_disconnected(client_id);
    }

    std::mutex m_mutex;             ///< Protects all mutable state below
    std::string m_active_client_id; ///< ID of the client holding the current session (empty if none)
    std::chrono::steady_clock::time_point m_last_keepalive; ///< Timestamp of the last keepalive or acquire
    OnConnected m_on_connected;                             ///< Observer callback for new session acquisition
    OnDisconnected m_on_disconnected;                       ///< Observer callback for session release or expiry
};
