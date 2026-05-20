#pragma once

#include <grpcpp/grpcpp.h>
#include <stddef.h>
#include <stdint.h>
#include <tl/expected.hpp>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "media_library/media_library_types.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/buffer_pool.hpp"
#include "media_library_service_buffer_uds.hpp"
#include "media_library.grpc.pb.h"
#include "media_library.pb.h"
#include "event_stream_reactors.hpp"

// Forward declaration for test access (defined in unit tests)
class ClientBufferManagerTestAccess;

namespace media_library_service
{

/// Identifies which buffer stream type this manager handles.
enum class BufferStreamType
{
    Frontend,
    Encoder
};

/// Manages all client-side buffer lifecycle: receiving DMA buffers over
/// UDS + gRPC, constructing HailoMediaLibraryBufferPtr, dispatching to
/// user callbacks, tracking buffer IDs, and releasing buffers back to
/// the service.
///
/// Each instance handles a single stream type (frontend or encoder).
/// The stream type determines which gRPC streaming RPC is opened and
/// which UDS socket path is used.
///
/// Inherits enable_shared_from_this because the ReleaseContext in each
/// buffer's on_free callback holds a weak_ptr to this manager. This
/// design isolates the shared_from_this requirement to the buffer
/// subsystem, allowing the containing Impl to use unique_ptr.
class ClientBufferManager : public std::enable_shared_from_this<ClientBufferManager>
{
  public:
    using StubType = MediaLibraryService::Stub;

    static constexpr size_t DEFAULT_MAX_DISPATCH_BUFFERS = 10;

    /// Construct a buffer manager for a specific stream type.
    /// @param stub                  Reference to the gRPC stub owned by Impl. The stub
    ///                              must outlive the buffer manager (guaranteed because
    ///                              Impl owns both the stub and the buffer manager).
    /// @param stream_type           Which buffer stream this manager handles.
    /// @param socket_path           UDS socket path for the buffer FD side-channel.
    ///                              Pass empty string to use the stream-type-specific
    ///                              path, or a custom path in tests.
    /// @param client_id             Client identifier used in gRPC subscribe requests.
    /// @param max_dispatch_buffers  Maximum number of buffers that can be queued for
    ///                              dispatch to the user callback. When the queue is full,
    ///                              newly incoming buffers are dropped (released back to
    ///                              the server).
    explicit ClientBufferManager(StubType &stub, BufferStreamType stream_type, const std::string &socket_path,
                                 const std::string &client_id, size_t max_dispatch_buffers);

    ~ClientBufferManager();

    // Non-copyable, non-movable
    ClientBufferManager(const ClientBufferManager &) = delete;
    ClientBufferManager &operator=(const ClientBufferManager &) = delete;
    ClientBufferManager(ClientBufferManager &&) = delete;
    ClientBufferManager &operator=(ClientBufferManager &&) = delete;

    /// Register a callback for the given stream ID and start streaming.
    /// @param stream_id  Output stream identifier (e.g., "sink0")
    /// @param callback   User callback invoked with (buffer, size)
    media_library_return subscribe(output_stream_id_t stream_id, AppWrapperCallback callback);

    /// Register multiple callbacks and start streaming.
    /// @param fe_callbacks  Map of stream_id -> callback
    media_library_return subscribe(FrontendCallbacksMap fe_callbacks);

    /// Look up the buffer_id for a buffer received via this manager.
    /// Used by add_buffer_to_encoder to translate buffer -> buffer_id.
    /// @param buffer  The buffer to look up
    /// @return buffer_id on success, or MEDIA_LIBRARY_ERROR if not found
    tl::expected<uint64_t, media_library_return> lookup_buffer_id(const HailoMediaLibraryBufferPtr &buffer) const;

    /// Stop buffer streaming: cancel the gRPC stream context and close UDS.
    /// Safe to call multiple times.
    void stop();

  private:
    /// Connect UDS and open the appropriate gRPC buffer stream based on
    /// m_stream_type. Acquires m_buffer_stream_state_mutex internally.
    /// Called lazily on first subscribe; idempotent if already active.
    media_library_return start_buffer_streaming();

    /// Connect UDS side-channel, open gRPC server-streaming RPC for buffer
    /// metadata, and mark the stream as active. Acquires
    /// @return MEDIA_LIBRARY_SUCCESS on success, error code on failure.
    media_library_return connect_and_open_stream();

    /// EventStreamReader callback: receive FDs from UDS, construct buffer,
    /// dispatch to user callback.
    void on_buffer_metadata_received(const media_library_service::BufferMetadata &metadata);

    /// Construct a HailoMediaLibraryBufferPtr from gRPC metadata + UDS FDs.
    /// Sets service_buffer_id on the buffer and wires the on_free callback
    /// with a weak_ptr to this manager.
    HailoMediaLibraryBufferPtr create_client_buffer(uint64_t buffer_id,
                                                    const media_library_service::BufferMetadata &metadata,
                                                    std::vector<FileDescriptor> received_fds);

    /// Item queued for dispatch to the user callback on the dispatch worker thread.
    struct DispatchItem
    {
        HailoMediaLibraryBufferPtr buffer;
        std::shared_ptr<AppWrapperCallback> callback;
        uint32_t encoded_data_size{0};
    };

    /// Worker loop that dequeues DispatchItems and invokes user callbacks.
    /// Runs on m_dispatch_worker thread. Drains remaining items on shutdown.
    void dispatch_worker_loop();

    /// Queue a buffer_id for release. Safe to call from any thread including
    /// the gRPC completion thread (does not make gRPC calls inline).
    void release_buffer_async(uint64_t buffer_id);

    /// Worker loop that drains m_release_queue and sends ReleaseBuffer RPCs.
    /// Runs on m_release_worker thread.
    void release_worker_loop();

    /// Send a single ReleaseBuffer gRPC (called from the worker thread only).
    void send_release_buffer(uint64_t buffer_id);

    // --- Members ---

    /// Reference to Impl's gRPC stub (not owned -- Impl owns it)
    StubType &m_stub;

    /// Client identifier used in subscribe RPCs (passed through constructor).
    std::string m_client_id;

    /// Which buffer stream this manager handles (Frontend or Encoder).
    BufferStreamType m_stream_type;

    /// UDS client for receiving DMA buffer FDs via SCM_RIGHTS
    BufferUdsClient m_buffer_uds_client;

    /// Streaming state.
    /// m_buffer_stream_state_mutex serializes start_buffer_streaming() vs stop().
    /// m_buffer_stream_active is atomic so on_buffer_metadata_received can read
    /// it without acquiring the mutex.
    std::mutex m_buffer_stream_state_mutex;
    grpc::ClientContext m_buffer_stream_context; // Created at construction, single-use
    std::atomic<bool> m_buffer_stream_active{false};

    /// Pointer to the event stream reader.  Non-owning: gRPC owns the reader
    /// and deletes it in OnDone.  Used by stop() to signal the reader to
    /// stop issuing new StartRead() calls before TryCancel() is invoked.
    /// Protected by m_buffer_stream_state_mutex.
    EventStreamReader<media_library_service::BufferMetadata> *m_event_reader{nullptr};

    /// User callbacks, keyed by stream_id.
    /// Stored as shared_ptr so that copies taken outside m_callbacks_mutex
    /// share the same std::function instance (preserving mutable lambda state).
    std::mutex m_callbacks_mutex;
    std::unordered_map<std::string, std::shared_ptr<AppWrapperCallback>> m_callbacks;

    /// Background worker thread for sending ReleaseBuffer RPCs.
    /// Avoids calling gRPC from the completion thread (crashes in gRPC 1.46).
    /// Joined in stop().
    std::thread m_release_worker;
    std::mutex m_release_queue_mutex;
    std::condition_variable m_release_queue_cv;
    std::queue<uint64_t> m_release_queue;
    std::atomic<bool> m_release_worker_running{false};

    /// Bounded dispatch queue: decouples gRPC metadata reception from user
    /// callback invocation. on_buffer_metadata_received enqueues items here;
    /// dispatch_worker_loop dequeues and invokes the user callback. When the
    /// queue is full, the incoming buffer is dropped and released back to the server).
    std::thread m_dispatch_worker;
    std::mutex m_dispatch_queue_mutex;
    std::condition_variable m_dispatch_queue_cv;
    std::queue<DispatchItem> m_dispatch_queue;
    std::atomic<bool> m_dispatch_worker_running{false};
    const size_t m_max_dispatch_buffers;

    /// Number of buffers dropped due to the dispatch queue being full.
    std::atomic<uint64_t> m_drop_count{0};

    friend class ::ClientBufferManagerTestAccess;
};

} // namespace media_library_service
