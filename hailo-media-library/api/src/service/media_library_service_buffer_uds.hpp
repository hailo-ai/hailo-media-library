/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include "media_library/media_library_types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace media_library_service
{

/// Status returned by send_buffer_fds — distinguishes transient backpressure
/// from permanent client death.
enum class uds_buffer_send_status
{
    OK,         // FDs delivered successfully
    WouldBlock, // EAGAIN/EWOULDBLOCK — buffer full, client slow
    Broken,     // EPIPE/ECONNRESET/ENOTCONN — client dead
    Error,      // other hard error
};

// Per-stream UDS socket paths for the split frontend/encoder buffer streams.
inline constexpr const char *DEFAULT_FRONTEND_UDS_SOCKET_PATH = "/var/run/medialib_frontend_buffers.sock";
inline constexpr const char *DEFAULT_ENCODER_UDS_SOCKET_PATH = "/var/run/medialib_encoder_buffers.sock";

/// RAII wrapper for file descriptors received via SCM_RIGHTS.
/// Move-only to prevent double-close.
class FileDescriptor
{
  public:
    explicit FileDescriptor(int fd = -1) : m_fd(fd)
    {
    }
    ~FileDescriptor();

    // Move-only
    FileDescriptor(FileDescriptor &&other) noexcept;
    FileDescriptor &operator=(FileDescriptor &&other) noexcept;
    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;

    /// [[nodiscard]] is a attribute that makes the compiler emit a warning (promoted to error by -Werror) if you call
    /// the function and discard the return value.
    [[nodiscard]] int get() const;
    [[nodiscard]] int release();
    explicit operator bool() const;

  private:
    int m_fd;
};

/// Server-side Unix Domain Socket manager for DMA buffer FD transfer.
/// Creates a listening socket, accepts a single client connection,
/// and sends buffer plane FDs via SCM_RIGHTS.
class BufferUdsServer
{
  public:
    /// Construct with a socket path. Does NOT set up the socket yet.
    explicit BufferUdsServer(const std::string &socket_path);
    ~BufferUdsServer();

    // Non-copyable, non-movable (owns socket resources)
    BufferUdsServer(const BufferUdsServer &) = delete;
    BufferUdsServer &operator=(const BufferUdsServer &) = delete;
    BufferUdsServer(BufferUdsServer &&) = delete;
    BufferUdsServer &operator=(BufferUdsServer &&) = delete;

    /// Create, bind, and listen on the UDS socket.
    /// Removes any stale socket file at the configured path.
    /// Returns MEDIA_LIBRARY_SUCCESS on success, MEDIA_LIBRARY_ERROR on failure.
    media_library_return setup();

    /// Accept a single client connection (blocking).
    /// Returns MEDIA_LIBRARY_SUCCESS on success.
    /// No-op if already accepted or if setup() was not called.
    media_library_return accept_client();

    /// Send DMA buffer plane FDs to the connected client via SCM_RIGHTS.
    /// The buffer_id is sent as the iov payload for client-side correlation.
    /// All plane FDs are sent in a single sendmsg() call.
    /// Returns uds_buffer_send_status::OK on success, uds_buffer_send_status::WouldBlock if the socket
    /// send buffer is full (EAGAIN), uds_buffer_send_status::Broken if the client is dead
    /// (EPIPE/ECONNRESET/ENOTCONN), or uds_buffer_send_status::Error for other failures.
    uds_buffer_send_status send_buffer_fds(const std::vector<int> &plane_fds, uint64_t buffer_id);

    /// Get the accepted client socket fd (-1 if no client connected).
    int get_client_fd() const;

    /// Get the listening socket fd (-1 if not set up).
    int get_listen_fd() const;

    /// Check if a client is connected.
    bool is_client_connected() const;

    /// Close only the accepted client connection, keeping the listening socket alive.
    /// A new client can connect via accept_client() afterwards.
    /// Safe to call multiple times.
    void disconnect_client();

    /// Close all sockets and unlink the socket file.
    /// Safe to call multiple times.
    void close();

  private:
    std::string m_socket_path;
    int m_listen_fd;
    int m_client_fd;
    bool m_bound = false;
};

/// Client-side Unix Domain Socket manager for DMA buffer FD transfer.
/// Connects to the server's UDS and receives buffer plane FDs via SCM_RIGHTS.
class BufferUdsClient
{
  public:
    /// Construct with a socket path. Does NOT connect yet.
    explicit BufferUdsClient(const std::string &socket_path);
    ~BufferUdsClient();

    // Non-copyable, non-movable (owns socket resources)
    BufferUdsClient(const BufferUdsClient &) = delete;
    BufferUdsClient &operator=(const BufferUdsClient &) = delete;
    BufferUdsClient(BufferUdsClient &&) = delete;
    BufferUdsClient &operator=(BufferUdsClient &&) = delete;

    /// Connect to the server's UDS socket.
    /// Returns MEDIA_LIBRARY_SUCCESS on success, MEDIA_LIBRARY_ERROR on failure.
    media_library_return connect();

    /// Receive DMA buffer plane FDs from the server via SCM_RIGHTS.
    /// @param expected_fd_count  Number of plane FDs expected (from gRPC metadata).
    /// @param buffer_id          Output: the buffer_id sent as the iov payload.
    /// @return  Vector of RAII FileDescriptor wrappers. Empty on failure.
    std::vector<FileDescriptor> receive_buffer_fds(uint32_t expected_fd_count, uint64_t &buffer_id);

    /// Get the connected socket fd (-1 if not connected).
    int get_socket_fd() const;

    /// Check if connected to the server.
    bool is_connected() const;

    /// Close the socket connection.
    /// Safe to call multiple times.
    void close();

  private:
    std::string m_socket_path;
    int m_socket_fd;
};

} // namespace media_library_service
