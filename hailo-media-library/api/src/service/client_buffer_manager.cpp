#include "client_buffer_manager.hpp"
#include "event_stream_reactors.hpp"
#include "media_library/media_library_logger.hpp"
#include "media_library/buffer_pool.hpp"
#include <grpcpp/grpcpp.h>
#include <limits>
#include <mutex>
#include <pthread.h>

using grpc::ClientContext;
using grpc::Status;

namespace media_library_service
{

static const char *default_socket_path_for(BufferStreamType stream_type)
{
    return stream_type == BufferStreamType::Frontend ? DEFAULT_FRONTEND_UDS_SOCKET_PATH
                                                     : DEFAULT_ENCODER_UDS_SOCKET_PATH;
}

ClientBufferManager::ClientBufferManager(StubType &stub, BufferStreamType stream_type, const std::string &socket_path,
                                         const std::string &client_id, size_t max_dispatch_buffers)
    : m_stub(stub), m_client_id(client_id), m_stream_type(stream_type),
      m_buffer_uds_client(socket_path.empty() ? default_socket_path_for(stream_type) : socket_path),
      m_max_dispatch_buffers(max_dispatch_buffers)
{
}

ClientBufferManager::~ClientBufferManager()
{
    stop();
}

media_library_return ClientBufferManager::subscribe(output_stream_id_t stream_id, AppWrapperCallback callback)
{
    {
        std::lock_guard<std::mutex> lock(m_callbacks_mutex);
        m_callbacks[stream_id] = std::make_shared<AppWrapperCallback>(std::move(callback));
        LOGGER__INFO("Registered callback for stream_id='{}'", stream_id);
    }
    return start_buffer_streaming();
}

media_library_return ClientBufferManager::subscribe(FrontendCallbacksMap fe_callbacks)
{
    {
        std::lock_guard<std::mutex> lock(m_callbacks_mutex);
        for (auto &[stream_id, callback] : fe_callbacks)
        {
            m_callbacks[stream_id] = std::make_shared<AppWrapperCallback>(std::move(callback));
        }
        LOGGER__INFO("Registered {} callbacks", fe_callbacks.size());
    }
    return start_buffer_streaming();
}

tl::expected<uint64_t, media_library_return> ClientBufferManager::lookup_buffer_id(
    const HailoMediaLibraryBufferPtr &buffer) const
{
    if (!buffer || buffer->service_buffer_id == 0)
        return tl::unexpected(MEDIA_LIBRARY_ERROR);
    return buffer->service_buffer_id;
}

// NOTE: stop() is terminal. The ClientContext is single-use — after TryCancel(),
// it cannot be reused. Do not call subscribe_* after stop(); create a new
// ClientBufferManager instance instead.
void ClientBufferManager::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_buffer_stream_state_mutex);
        // Signal the reader to stop issuing new StartRead() calls before
        // cancelling the context.  This prevents a use-after-free in
        // gRPC 1.46 where OnReadDone(true) fires for a pre-buffered
        // message after TryCancel() and calls StartRead() on a freed call.
        if (m_event_reader)
        {
            m_event_reader->cancel();
            m_event_reader = nullptr;
        }
        m_buffer_stream_context.TryCancel(); // No-op if never used in an RPC

        // Close UDS
        m_buffer_uds_client.close();

        // Set m_buffer_stream_active = false first to prevent new enqueues from
        // on_buffer_metadata_received, then stop the dispatch worker.
        m_buffer_stream_active.store(false, std::memory_order_release);
    }

    // Shutdown ordering for the worker threads:
    // 1. Cancel reader + close UDS + set active=false (above) — prevents new
    //    on_buffer_metadata_received calls and unblocks any in-flight ones
    // 2. Stop dispatch worker — drains remaining items, dispatching callbacks that
    //    may call release_buffer_async (enqueuing to the release worker)
    // 3. Stop release worker — drains remaining release RPCs
    // The dispatch worker MUST be joined BEFORE the release worker is stopped,
    // because drained callbacks may call release_buffer_async.

    // Shut down the dispatch worker thread
    m_dispatch_worker_running.store(false, std::memory_order_release);
    m_dispatch_queue_cv.notify_one();
    if (m_dispatch_worker.joinable())
        m_dispatch_worker.join();

    // Note: If user holds HailoMediaLibraryBufferPtr beyond the manager's
    // lifetime, the release RPC will not be sent. The server's keep-alive
    // mechanism reclaims such buffers when it detects client death.

    // Shut down the release worker thread (must be outside m_buffer_stream_state_mutex)
    m_release_worker_running.store(false, std::memory_order_release);
    m_release_queue_cv.notify_one();
    if (m_release_worker.joinable())
        m_release_worker.join();
}

media_library_return ClientBufferManager::connect_and_open_stream()
{
    std::lock_guard<std::mutex> lock(m_buffer_stream_state_mutex);
    if (m_buffer_stream_active.load(std::memory_order_relaxed))
        return MEDIA_LIBRARY_SUCCESS;

    LOGGER__INFO("Starting buffer streaming for client_id={}", m_client_id);

    // Connect to the UDS side-channel for FD transfer
    media_library_return ret = m_buffer_uds_client.connect();
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    // Open the gRPC server-streaming RPC for buffer metadata.
    // m_buffer_stream_context was created at construction — no allocation needed.
    std::weak_ptr<ClientBufferManager> weak_self = shared_from_this();
    auto *reader = new EventStreamReader<media_library_service::BufferMetadata>(
        [weak_self](const media_library_service::BufferMetadata &metadata) {
            auto self = weak_self.lock();
            if (!self)
                return;
            self->on_buffer_metadata_received(metadata);
        });
    m_event_reader = reader; // Store for cancel() in stop()

    // NOTE: reader->start() must be called while `request` is still alive.
    // gRPC's SendMessagePtr defers serialization until StartCall(), so the
    // request object must not go out of scope before start() completes.
    if (m_stream_type == BufferStreamType::Frontend)
    {
        media_library_service::SubscribeToFrontendBufferStreamRequest request;
        request.set_client_id(m_client_id);
        m_stub.async()->SubscribeToFrontendBufferStream(&m_buffer_stream_context, &request, reader);
        reader->start();
    }
    else
    {
        media_library_service::SubscribeToEncoderBufferStreamRequest request;
        request.set_client_id(m_client_id);
        m_stub.async()->SubscribeToEncoderBufferStream(&m_buffer_stream_context, &request, reader);
        reader->start();
    }

    // Set active only after a successful start so a failed attempt can be retried.
    m_buffer_stream_active.store(true, std::memory_order_release);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return ClientBufferManager::start_buffer_streaming()
{
    media_library_return ret = connect_and_open_stream();
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return ret;

    // Start worker threads if not already running (idempotent for multi-stream subscribe)
    if (!m_dispatch_worker.joinable())
    {
        m_dispatch_worker_running.store(true, std::memory_order_release);
        m_dispatch_worker = std::thread(&ClientBufferManager::dispatch_worker_loop, this);
    }

    if (!m_release_worker.joinable())
    {
        m_release_worker_running.store(true, std::memory_order_release);
        m_release_worker = std::thread(&ClientBufferManager::release_worker_loop, this);
    }

    return MEDIA_LIBRARY_SUCCESS;
}

void ClientBufferManager::on_buffer_metadata_received(const media_library_service::BufferMetadata &metadata)
{
    if (!m_buffer_stream_active.load(std::memory_order_acquire))
    {
        LOGGER__ERROR("Buffer metadata received but streaming not active, ignoring buffer_id={}", metadata.buffer_id());
        release_buffer_async(metadata.buffer_id());
        return;
    }

    uint64_t buffer_id = metadata.buffer_id();

    // Receive plane FDs from the Unix domain socket.
    // The server sent FDs BEFORE pushing the gRPC metadata event,
    // so FDs should already be waiting in the UDS receive buffer.
    // NOTE: if future work allows concurrent buffer streams, the UDS receive
    // path will need a concurrent queue to correctly pair FDs with metadata.
    uint64_t received_buffer_id = 0;
    std::vector<media_library_service::FileDescriptor> received_fds =
        m_buffer_uds_client.receive_buffer_fds(metadata.planes_count(), received_buffer_id);

    // During client reconnection, a few stale UDS messages from the  previous
    // session may sit in the kernel buffer.  Drain them until we find the
    // matching buffer_id (or exhaust retries).
    // TODO: Move UDS FD reception off the gRPC completion thread to the dispatch
    // worker to avoid blocking the completion queue during resync attempts.
    static constexpr int MAX_RESYNC_ATTEMPTS = 8;
    for (int attempt = 0; !received_fds.empty() && received_buffer_id != buffer_id && attempt < MAX_RESYNC_ATTEMPTS;
         attempt++)
    {
        LOGGER__WARN("UDS buffer_id mismatch (expected={} received={}), draining stale message (attempt {})", buffer_id,
                     received_buffer_id, attempt);
        // Release the stale buffer on the server side
        release_buffer_async(received_buffer_id);
        // Try the next UDS message
        received_fds = m_buffer_uds_client.receive_buffer_fds(metadata.planes_count(), received_buffer_id);
    }

    if (received_fds.empty() || received_buffer_id != buffer_id)
    {
        LOGGER__ERROR("FD receive failed or buffer_id mismatch after resync: "
                      "expected={} received={}",
                      buffer_id, received_buffer_id);
        release_buffer_async(buffer_id);
        return;
    }

    // Look up the user callback for this stream_id BEFORE creating the buffer.
    // If no callback is registered, we skip the expensive buffer construction
    // (mmap, ReleaseContext allocation) and release immediately.
    // m_callbacks_mutex protects against concurrent subscribe() calls
    // modifying the callback map while we read it.
    const std::string &stream_id = metadata.stream_id();
    uint32_t encoded_data_size = metadata.encoded_data_size();

    std::shared_ptr<AppWrapperCallback> callback;
    {
        std::lock_guard<std::mutex> lock(m_callbacks_mutex);
        auto it = m_callbacks.find(stream_id);
        if (it != m_callbacks.end())
            callback = it->second;
    }

    if (!callback)
    {
        LOGGER__WARNING("No callback registered for stream_id={}", stream_id);
        // Release directly
        release_buffer_async(buffer_id);
        return;
    }

    // Construct the real HailoMediaLibraryBufferPtr
    HailoMediaLibraryBufferPtr buffer = create_client_buffer(buffer_id, metadata, std::move(received_fds));

    if (!buffer)
    {
        LOGGER__ERROR("Failed to create client buffer for buffer_id={}", buffer_id);
        release_buffer_async(buffer_id);
        return;
    }

    // Enqueue the buffer for dispatch on the dedicated worker thread.
    // This returns immediately, unblocking the gRPC completion thread.
    {
        std::lock_guard<std::mutex> lock(m_dispatch_queue_mutex);
        if (m_dispatch_queue.size() >= m_max_dispatch_buffers)
        {
            // Queue is full — drop the incoming buffer (newest).
            // Do NOT call release_buffer_async explicitly: the HailoMediaLibraryBufferPtr
            // goes out of scope when this function returns (after the mutex releases),
            // triggering its on_free callback, which calls release_buffer_async via
            // the ReleaseContext.
            uint64_t drop_count = m_drop_count.fetch_add(1, std::memory_order_relaxed) + 1;
            LOGGER__WARNING("Dispatch queue full (size={}/{}), dropping buffer_id={} stream_id='{}' "
                            "(total drops={})",
                            m_dispatch_queue.size(), m_max_dispatch_buffers, buffer_id, stream_id, drop_count);
            return;
        }
        m_dispatch_queue.push(DispatchItem{std::move(buffer), std::move(callback), encoded_data_size});
        LOGGER__DEBUG("Dispatch queue enqueued buffer_id={} stream_id='{}' (queue_size={})", buffer_id, stream_id,
                      m_dispatch_queue.size());
    }
    m_dispatch_queue_cv.notify_one();
}

void ClientBufferManager::dispatch_worker_loop()
{
    while (true)
    {
        DispatchItem item;
        {
            std::unique_lock<std::mutex> lock(m_dispatch_queue_mutex);
            m_dispatch_queue_cv.wait(lock, [this] {
                return !m_dispatch_queue.empty() || !m_dispatch_worker_running.load(std::memory_order_acquire);
            });
            if (!m_dispatch_worker_running.load(std::memory_order_acquire) && m_dispatch_queue.empty())
                break;
            item = std::move(m_dispatch_queue.front());
            m_dispatch_queue.pop();
            LOGGER__DEBUG("Dispatch queue dequeued buffer (queue_size={})", m_dispatch_queue.size());
        }
        // Invoke user callback outside the mutex — the callback may block
        // (e.g., add_buffer_to_encoder does a synchronous gRPC call).
        (*item.callback)(item.buffer, item.encoded_data_size);
    }

    // Drain any remaining items before exiting (shutdown path).
    // Drained callbacks may call release_buffer_async, which enqueues to the
    // release worker — the release worker is still running at this point.
    std::unique_lock<std::mutex> lock(m_dispatch_queue_mutex);
    while (!m_dispatch_queue.empty())
    {
        DispatchItem item = std::move(m_dispatch_queue.front());
        m_dispatch_queue.pop();
        // Release the lock while invoking the callback to avoid holding the
        // queue mutex during potentially long user callbacks.
        lock.unlock();
        (*item.callback)(item.buffer, item.encoded_data_size);
        lock.lock();
    }
}

HailoMediaLibraryBufferPtr ClientBufferManager::create_client_buffer(
    uint64_t buffer_id, const media_library_service::BufferMetadata &metadata, std::vector<FileDescriptor> received_fds)
{
    auto buffer = std::make_shared<hailo_media_library_buffer>();

    // Build plane data from received FDs + metadata.
    if (metadata.planes_count() > static_cast<uint32_t>(metadata.planes_size()) ||
        metadata.planes_count() > received_fds.size())
    {
        LOGGER__ERROR("Plane count mismatch: planes_count={} planes_size={} received_fds={}", metadata.planes_count(),
                      metadata.planes_size(), received_fds.size());
        return nullptr;
    }

    std::vector<hailo_data_plane_t> planes;
    std::vector<void *> mapped_ptrs; // Track successful mmaps for cleanup on partial failure

    for (uint32_t plane_index = 0; plane_index < metadata.planes_count(); plane_index++)
    {
        const auto &plane_meta = metadata.planes(plane_index);
        int fd = received_fds[plane_index].get();

        // Use DmaMemoryAllocator::map_external_dma_buffer to mmap the received FD
        void *mapped_ptr = nullptr;
        media_library_return map_ret =
            DmaMemoryAllocator::get_instance().map_external_dma_buffer(plane_meta.bytesused(), fd, &mapped_ptr);
        if (map_ret != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to mmap plane {} for buffer_id={}", plane_index, buffer_id);
            // Unmap any planes we already mapped to avoid leaking them
            for (void *ptr : mapped_ptrs)
            {
                DmaMemoryAllocator::get_instance().unmap_external_dma_buffer(ptr);
            }
            return nullptr;
        }

        mapped_ptrs.push_back(mapped_ptr);

        // IMPORTANT: hailo_data_plane_t::fd holds a raw int copy of the FD.
        // The actual ownership is in ReleaseContext::owned_fds (FileDescriptor RAII).
        // The raw fd in the plane is valid only while the ReleaseContext is alive
        // (i.e., while the buffer's shared_ptr exists).
        hailo_data_plane_t plane;
        plane.userptr = mapped_ptr;
        plane.fd = fd;
        plane.bytesperline = plane_meta.bytesperline();
        plane.bytesused = plane_meta.bytesused();
        planes.push_back(plane);
    }

    // Create buffer_data with the correct format and memory type
    HailoBufferDataPtr buffer_data = std::make_shared<hailo_buffer_data_t>(
        metadata.width(), metadata.height(), metadata.planes_count(), static_cast<HailoFormat>(metadata.format()),
        HAILO_MEMORY_TYPE_DMABUF, std::move(planes));

    // Populate metadata from the gRPC message
    buffer->vsm.dx = metadata.vsm_dx();
    buffer->vsm.dy = metadata.vsm_dy();
    buffer->isp_ae_fps = metadata.isp_ae_fps();
    buffer->isp_ae_converged = metadata.isp_ae_converged();
    buffer->isp_ae_integration_time = metadata.isp_ae_integration_time();
    uint32_t luma_raw = metadata.isp_ae_average_luma();
    if (luma_raw > std::numeric_limits<uint8_t>::max())
    {
        LOGGER__WARNING("isp_ae_average_luma={} exceeds uint8_t range, clamping", luma_raw);
        luma_raw = std::numeric_limits<uint8_t>::max();
    }
    buffer->isp_ae_average_luma = static_cast<uint8_t>(luma_raw);
    buffer->isp_timestamp_ns = metadata.isp_timestamp_ns();
    buffer->pts = metadata.pts();
    buffer->buffer_index = metadata.buffer_index();
    buffer->motion_detected = metadata.motion_detected();
    buffer->optical_zoom_magnification = metadata.optical_zoom_magnification();

    const auto &proto_ids = metadata.concurrent_stream_ids();
    buffer->concurrent_stream_ids.insert(proto_ids.begin(), proto_ids.end());

    // Build the release context that lives until the buffer is destroyed.
    // We use shared_ptr (not unique_ptr) because std::function requires
    // CopyConstructible captures, and unique_ptr is move-only.
    // We use a weak_ptr to ClientBufferManager to guard against the manager
    // being destroyed while buffers are still alive.
    struct ReleaseContext
    {
        uint64_t buffer_id;
        std::weak_ptr<ClientBufferManager> manager_weak;
        std::vector<FileDescriptor> owned_fds;
    };

    auto release_ctx =
        std::make_shared<ReleaseContext>(ReleaseContext{buffer_id, shared_from_this(), std::move(received_fds)});

    // Note: If user holds HailoMediaLibraryBufferPtr beyond the manager's
    // lifetime, the release RPC will not be sent. The server's keep-alive
    // mechanism reclaims such buffers when it detects client death.
    auto on_free_callback = [release_ctx](void * /*unused*/) {
        if (auto manager = release_ctx->manager_weak.lock())
        {
            manager->release_buffer_async(release_ctx->buffer_id);
        }
        else
        {
            LOGGER__WARNING("ClientBufferManager already destroyed when releasing buffer_id={}, "
                            "relying on server keep-alive cleanup",
                            release_ctx->buffer_id);
        }
        // received FDs are closed when release_ctx is destroyed
    };

    buffer->create(nullptr, buffer_data, on_free_callback, nullptr);

    // Store the server-assigned buffer_id directly on the buffer object
    // so add_buffer_to_encoder can retrieve it without a side map.
    buffer->service_buffer_id = buffer_id;

    return buffer;
}

void ClientBufferManager::release_buffer_async(uint64_t buffer_id)
{
    // Queue the release — safe to call from any thread including gRPC completion thread.
    // The actual gRPC call happens on m_release_worker.
    {
        std::lock_guard<std::mutex> lock(m_release_queue_mutex);
        m_release_queue.push(buffer_id);
    }
    m_release_queue_cv.notify_one();
}

void ClientBufferManager::release_worker_loop()
{
    while (true)
    {
        uint64_t buffer_id = 0;
        {
            std::unique_lock<std::mutex> lock(m_release_queue_mutex);
            m_release_queue_cv.wait(lock, [this] {
                return !m_release_queue.empty() || !m_release_worker_running.load(std::memory_order_acquire);
            });
            if (!m_release_worker_running.load(std::memory_order_acquire) && m_release_queue.empty())
                break;
            buffer_id = m_release_queue.front();
            m_release_queue.pop();
        }
        send_release_buffer(buffer_id);
    }

    // Drain any remaining items before exiting
    std::lock_guard<std::mutex> lock(m_release_queue_mutex);
    while (!m_release_queue.empty())
    {
        send_release_buffer(m_release_queue.front());
        m_release_queue.pop();
    }
}

void ClientBufferManager::send_release_buffer(uint64_t buffer_id)
{
    // Synchronous call — runs on m_release_worker thread, so blocking is fine.
    // The async callback API (CallbackUnaryCallImpl) segfaults in gRPC 1.46.
    grpc::ClientContext context;
    media_library_service::ReleaseBufferRequest request;
    request.set_buffer_id(buffer_id);
    media_library_service::MedialibraryStatusReply reply;

    grpc::Status status = m_stub.ReleaseBuffer(&context, request, &reply);
    if (!status.ok())
    {
        LOGGER__ERROR("ReleaseBuffer RPC failed for buffer_id={}: {}", buffer_id, status.error_message());
    }
}

} // namespace media_library_service
