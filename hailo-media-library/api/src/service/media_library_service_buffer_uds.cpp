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

#include "media_library_service_buffer_uds.hpp"
#include "media_library/media_library_logger.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace media_library_service
{

static constexpr size_t MAX_PLANE_FDS = 4;

// ========== FileDescriptor ==========

FileDescriptor::~FileDescriptor()
{
    if (m_fd >= 0)
    {
        if (::close(m_fd) != 0)
            LOGGER__ERROR("Failed to close fd={}. errno={}", m_fd, errno);
    }
}

FileDescriptor::FileDescriptor(FileDescriptor &&other) noexcept : m_fd(std::exchange(other.m_fd, -1))
{
}

FileDescriptor &FileDescriptor::operator=(FileDescriptor &&other) noexcept
{
    if (this != &other)
    {
        if (m_fd >= 0)
        {
            if (::close(m_fd) != 0)
                LOGGER__ERROR("FileDescriptor::operator=: Failed to close fd={}. errno={}", m_fd, errno);
        }
        m_fd = std::exchange(other.m_fd, -1);
    }
    return *this;
}

int FileDescriptor::get() const
{
    return m_fd;
}

int FileDescriptor::release()
{
    return std::exchange(m_fd, -1);
}

FileDescriptor::operator bool() const
{
    return m_fd >= 0;
}

// ========== BufferUdsServer ==========

BufferUdsServer::BufferUdsServer(const std::string &socket_path)
    : m_socket_path(socket_path), m_listen_fd(-1), m_client_fd(-1), m_bound(false)
{
}

BufferUdsServer::~BufferUdsServer()
{
    close();
}

media_library_return BufferUdsServer::setup()
{
    if (m_listen_fd >= 0)
        return MEDIA_LIBRARY_SUCCESS;

    // Remove stale socket file if present
    unlink(m_socket_path.c_str());

    m_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_listen_fd < 0)
    {
        LOGGER__ERROR("Failed to create UDS listen socket: errno={}", errno);
        return MEDIA_LIBRARY_ERROR;
    }

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, m_socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(m_listen_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        LOGGER__ERROR("Failed to bind UDS: errno={}", errno);
        ::close(m_listen_fd);
        m_listen_fd = -1;
        return MEDIA_LIBRARY_ERROR;
    }

    m_bound = true;

    if (listen(m_listen_fd, 1) < 0)
    {
        LOGGER__ERROR("Failed to listen on UDS: errno={}", errno);
        ::close(m_listen_fd);
        m_listen_fd = -1;
        return MEDIA_LIBRARY_ERROR;
    }
    LOGGER__INFO("UDS listening on {}", m_socket_path);
    return MEDIA_LIBRARY_SUCCESS;
}

/// Set a file descriptor to non-blocking mode (O_NONBLOCK).
/// Returns true on success, false on failure (logs the error).
static bool set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        LOGGER__ERROR("Failed to set O_NONBLOCK on fd={}: errno={}", fd, errno);
        return false;
    }
    return true;
}

/// Quick non-blocking probe: returns true if the socket fd is open and
/// the peer hasn't hung up. Uses poll() with timeout=0 to detect
/// POLLHUP/POLLERR on a half-closed socket without blocking.
static bool is_socket_alive(int fd)
{
    struct pollfd pfd = {fd, POLLIN, 0};
    int pr = poll(&pfd, 1, 0);
    return (pr >= 0) && ((pfd.revents & (POLLHUP | POLLERR)) == 0);
}

media_library_return BufferUdsServer::accept_client()
{
    if (m_listen_fd < 0)
        return MEDIA_LIBRARY_ERROR;
    if (m_client_fd >= 0)
        return MEDIA_LIBRARY_SUCCESS; // Already accepted

    // Accept the next pending connection.  If it turns out to be a stale
    // half-closed socket (client died between connect() and accept()),
    // close it and retry until we get a live connection.
    static constexpr int MAX_ACCEPT_RETRIES = 5;
    for (int attempt = 0; attempt < MAX_ACCEPT_RETRIES; attempt++)
    {
        int fd = ::accept(m_listen_fd, nullptr, nullptr);
        if (fd < 0)
        {
            LOGGER__ERROR("Failed to accept UDS client: errno={}", errno);
            return MEDIA_LIBRARY_ERROR;
        }

        if (is_socket_alive(fd))
        {
            if (!set_nonblocking(fd))
            {
                ::close(fd);
                return MEDIA_LIBRARY_ERROR;
            }
            m_client_fd = fd;
            LOGGER__INFO("UDS client connected (fd={}, O_NONBLOCK set, attempt={})", m_client_fd, attempt);
            return MEDIA_LIBRARY_SUCCESS;
        }

        // Stale connection — close and retry.
        LOGGER__WARN("UDS accept: stale connection fd={}, draining (attempt {})", fd, attempt);
        ::close(fd);
    }

    LOGGER__ERROR("UDS accept: exhausted {} retries, all connections stale", MAX_ACCEPT_RETRIES);
    return MEDIA_LIBRARY_ERROR;
}

uds_buffer_send_status BufferUdsServer::send_buffer_fds(const std::vector<int> &plane_fds, uint64_t buffer_id)
{
    if (m_client_fd < 0)
    {
        LOGGER__ERROR("send_buffer_fds: no client connected");
        return uds_buffer_send_status::Broken;
    }

    if (plane_fds.empty())
    {
        LOGGER__ERROR("send_buffer_fds: plane_fds is empty for buffer_id={}", buffer_id);
        return uds_buffer_send_status::Error;
    }

    struct msghdr msg = {};
    struct iovec iov;

    // Send buffer_id as the payload so client can correlate with gRPC metadata
    uint64_t id_payload = buffer_id;
    iov.iov_base = &id_payload;
    iov.iov_len = sizeof(id_payload);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    // Ancillary data carries all plane FDs
    size_t fd_count = plane_fds.size();
    if (fd_count > MAX_PLANE_FDS)
    {
        LOGGER__ERROR("send_buffer_fds: fd_count={} exceeds MAX_PLANE_FDS={}", fd_count, MAX_PLANE_FDS);
        return uds_buffer_send_status::Error;
    }
    alignas(struct cmsghdr) char cmsg_buf[CMSG_SPACE(sizeof(int) * MAX_PLANE_FDS)] = {};
    size_t cmsg_space = CMSG_SPACE(sizeof(int) * fd_count);
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = cmsg_space;

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * fd_count);
    memcpy(CMSG_DATA(cmsg), plane_fds.data(), sizeof(int) * fd_count);

    auto t_before_send = std::chrono::steady_clock::now();
    ssize_t sent;
    // Retry on EINTR; return immediately on EAGAIN or hard errors (socket is non-blocking)
    do
    {
        sent = sendmsg(m_client_fd, &msg, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);

    auto t_after_send = std::chrono::steady_clock::now();
    auto send_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_after_send - t_before_send).count();

    if (sent < 0)
    {
        int saved_errno = errno;
        if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK)
        {
            LOGGER__WARN("send_buffer_fds: EAGAIN for buffer_id={} (client slow/full) send_ms={}", buffer_id, send_ms);
            return uds_buffer_send_status::WouldBlock;
        }
        if (saved_errno == EPIPE || saved_errno == ECONNRESET || saved_errno == ENOTCONN)
        {
            LOGGER__WARN("send_buffer_fds: client dead for buffer_id={} errno={} send_ms={}", buffer_id, saved_errno,
                         send_ms);
            return uds_buffer_send_status::Broken;
        }
        LOGGER__ERROR("send_buffer_fds: sendmsg failed for buffer_id={} errno={} send_ms={}", buffer_id, saved_errno,
                      send_ms);
        return uds_buffer_send_status::Error;
    }

    LOGGER__INFO("UDS send_buffer_fds: buffer_id={} fd_count={} bytes_sent={} send_ms={}", buffer_id, fd_count, sent,
                 send_ms);
    return uds_buffer_send_status::OK;
}

int BufferUdsServer::get_client_fd() const
{
    return m_client_fd;
}

int BufferUdsServer::get_listen_fd() const
{
    return m_listen_fd;
}

bool BufferUdsServer::is_client_connected() const
{
    return m_client_fd >= 0;
}

void BufferUdsServer::disconnect_client()
{
    if (m_client_fd >= 0)
    {
        LOGGER__INFO("BufferUdsServer::disconnect_client: closing client fd={} on {}", m_client_fd, m_socket_path);
        ::close(m_client_fd);
        m_client_fd = -1;
    }
}

void BufferUdsServer::close()
{
    LOGGER__INFO("BufferUdsServer::close: shutting down UDS on {}", m_socket_path);
    disconnect_client();
    if (m_listen_fd >= 0)
    {
        ::close(m_listen_fd);
        m_listen_fd = -1;
    }
    if (m_bound)
    {
        unlink(m_socket_path.c_str());
        m_bound = false;
        LOGGER__INFO("BufferUdsServer::close: unlinked socket file {}", m_socket_path);
    }
}

// ========== BufferUdsClient ==========

BufferUdsClient::BufferUdsClient(const std::string &socket_path) : m_socket_path(socket_path), m_socket_fd(-1)
{
}

BufferUdsClient::~BufferUdsClient()
{
    close();
}

media_library_return BufferUdsClient::connect()
{
    if (m_socket_fd >= 0)
    {
        LOGGER__WARNING("BufferUdsClient::connect: already connected (fd={}), closing first", m_socket_fd);
        close();
    }

    m_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_socket_fd < 0)
    {
        LOGGER__ERROR("Failed to create UDS socket: errno={}", errno);
        return MEDIA_LIBRARY_ERROR;
    }

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, m_socket_path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(m_socket_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        LOGGER__ERROR("Failed to connect UDS: errno={}", errno);
        ::close(m_socket_fd);
        m_socket_fd = -1;
        return MEDIA_LIBRARY_ERROR;
    }

    LOGGER__INFO("Connected to UDS at {}", m_socket_path);
    return MEDIA_LIBRARY_SUCCESS;
}

std::vector<FileDescriptor> BufferUdsClient::receive_buffer_fds(uint32_t expected_fd_count, uint64_t &buffer_id)
{
    struct msghdr msg = {};
    struct iovec iov;

    iov.iov_base = &buffer_id;
    iov.iov_len = sizeof(buffer_id);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    if (expected_fd_count > MAX_PLANE_FDS)
    {
        LOGGER__ERROR("receive_buffer_fds: expected_fd_count={} exceeds MAX_PLANE_FDS={}", expected_fd_count,
                      MAX_PLANE_FDS);
        return {};
    }
    alignas(struct cmsghdr) char cmsg_buf[CMSG_SPACE(sizeof(int) * MAX_PLANE_FDS)] = {};
    size_t cmsg_space = CMSG_SPACE(sizeof(int) * expected_fd_count);
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = cmsg_space;

    ssize_t n;
    do
    {
        n = recvmsg(m_socket_fd, &msg, 0);
    } while (n < 0 && errno == EINTR);
    if (n <= 0)
    {
        LOGGER__ERROR("recvmsg returned {} (errno={})", n, errno);
        return {};
    }

    // Validate we received the full buffer_id payload
    if (static_cast<size_t>(n) < sizeof(buffer_id))
    {
        LOGGER__ERROR("recvmsg short iov read: got {} bytes, expected {} for buffer_id", n, sizeof(buffer_id));
        return {};
    }

    // Check for truncated control data -- happens when the process hits FD limits
    if (msg.msg_flags & MSG_CTRUNC)
    {
        LOGGER__CRITICAL("recvmsg control data truncated -- likely hit file descriptor "
                         "limit. The DMA buffer FD was lost. Check ulimit -n.");
        return {};
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

    // If no ancillary data at all, the sender did not attach SCM_RIGHTS
    if (!cmsg)
    {
        LOGGER__ERROR("recvmsg: no ancillary data (cmsg is null) -- sender did not "
                      "attach SCM_RIGHTS. buffer_id={}",
                      buffer_id);
        return {};
    }

    if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
    {
        if (cmsg->cmsg_len < CMSG_LEN(0))
        {
            LOGGER__CRITICAL("receive_buffer_fds: malformed cmsg_len={} (< CMSG_LEN(0)={})", cmsg->cmsg_len,
                             CMSG_LEN(0));
            return {};
        }
        size_t fd_count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        int *fd_array = reinterpret_cast<int *>(CMSG_DATA(cmsg));

        // Validate received FD count matches expectation
        if (fd_count != expected_fd_count)
        {
            LOGGER__CRITICAL("receive_buffer_fds: expected {} FDs but recvmsg "
                             "delivered {} -- protocol mismatch, closing leaked FDs",
                             expected_fd_count, fd_count);
            for (size_t i = 0; i < fd_count; i++)
            {
                ::close(fd_array[i]);
            }
            return {};
        }

        // Wrap FDs in RAII immediately -- no raw-FD gap
        std::vector<FileDescriptor> result;
        result.reserve(fd_count);
        for (size_t i = 0; i < fd_count; i++)
        {
            result.emplace_back(fd_array[i]);
        }

        LOGGER__TRACE("UDS receive_buffer_fds: buffer_id={} fd_count={}", buffer_id, fd_count);
        return result;
    }
    else
    {
        LOGGER__ERROR("recvmsg: unexpected cmsg level={} type={}, expected "
                      "SOL_SOCKET/SCM_RIGHTS. buffer_id={}",
                      cmsg->cmsg_level, cmsg->cmsg_type, buffer_id);
        return {};
    }
}

int BufferUdsClient::get_socket_fd() const
{
    return m_socket_fd;
}

bool BufferUdsClient::is_connected() const
{
    return m_socket_fd >= 0;
}

void BufferUdsClient::close()
{
    if (m_socket_fd >= 0)
    {
        ::close(m_socket_fd);
        m_socket_fd = -1;
    }
}

} // namespace media_library_service
