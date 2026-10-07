#pragma once

#include "media_library/media_library_logger.hpp"
#include "service_trace.hpp"
#include <functional>
#include <grpcpp/grpcpp.h>
#include <memory>
#include <mutex>
#include <queue>

// Server-side reactor for event streaming RPCs.
// Holds a gRPC server-write stream open and pushes events from MediaLibrary callbacks.
template <typename EventT> class EventStreamBroker : public grpc::ServerWriteReactor<EventT>
{
  public:
    /**
     * @brief Create a broker that owns itself for as long as gRPC needs it.
     *
     * The self-reference stored here is the object's lifetime; OnDone() releases it. Callers
     * keep weak references and lock them around each use, so a broker cannot be freed while
     * another thread is still inside one of its methods.
     */
    static std::shared_ptr<EventStreamBroker> create()
    {
        auto broker = std::shared_ptr<EventStreamBroker>(new EventStreamBroker());
        broker->m_self = broker;
        return broker;
    }

    void push_event(EventT event)
    {
        BROKER_TRACE_BEGIN("broker_push_event_enqueue");
        bool should_write = enqueue_event(std::move(event));
        BROKER_TRACE_END();
        if (should_write)
        {
            BROKER_TRACE_BEGIN("broker_StartWrite");
            this->StartWrite(&m_current_event);
            BROKER_TRACE_END();
        }
    }

    void OnWriteDone(bool ok) override
    {
        BROKER_TRACE_BEGIN("broker_OnWriteDone");
        auto action = dequeue_next(ok);
        if (action == WriteAction::FINISH)
            this->Finish(grpc::Status::OK);
        if (action == WriteAction::WRITE)
            this->StartWrite(&m_current_event);
        BROKER_TRACE_END();
    }

    // Gracefully close the stream from the server side (e.g. when a new subscriber replaces this one).
    void finish()
    {
        BROKER_TRACE_BEGIN("broker_finish");
        if (claim_finish())
            this->Finish(grpc::Status::OK);
        BROKER_TRACE_END();
    }

    void OnCancel() override
    {
        BROKER_TRACE_BEGIN("broker_OnCancel");
        if (claim_finish())
            this->Finish(grpc::Status::CANCELLED);
        BROKER_TRACE_END();
    }

    void OnDone() override
    {
        BROKER_TRACE_BEGIN("broker_OnDone");
        BROKER_TRACE_END();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_done = true;
        }
        // Releasing the last reference runs the destructor, so it must happen outside the lock.
        m_self.reset();
    }

  private:
    enum class WriteAction
    {
        NONE,
        WRITE,
        FINISH
    };

    EventStreamBroker() : m_writing(false), m_finished(false)
    {
    }

    /// Claim the sole right to call Finish(). The gRPC call must happen outside m_mutex:
    /// once Finish() is in flight another thread can run OnDone and drop the last reference.
    bool claim_finish()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_finished || m_done)
            return false;
        m_finished = true;
        return true;
    }

    /// Enqueue an event and claim write ownership if idle.
    /// Returns true when the caller should call StartWrite (outside the lock).
    bool enqueue_event(EventT event)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_finished || m_done)
            return false;
        m_pending_events.push(std::move(event));
        if (m_writing)
            return false;
        m_writing = true;
        m_current_event = std::move(m_pending_events.front());
        m_pending_events.pop();
        return true;
    }

    /// Process write completion and determine the next action.
    /// Called from OnWriteDone; the actual gRPC call happens outside the lock.
    WriteAction dequeue_next(bool ok)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!ok || m_finished)
        {
            m_writing = false;
            if (!m_finished)
            {
                m_finished = true;
                return WriteAction::FINISH;
            }
            return WriteAction::NONE;
        }
        if (!m_pending_events.empty())
        {
            m_current_event = std::move(m_pending_events.front());
            m_pending_events.pop();
            return WriteAction::WRITE;
        }
        m_writing = false;
        return WriteAction::NONE;
    }

    std::shared_ptr<EventStreamBroker> m_self;
    std::mutex m_mutex;
    std::queue<EventT> m_pending_events;
    EventT m_current_event;
    bool m_writing;
    bool m_finished;
    bool m_done{false};
};

// Client-side reactor for reading server-streaming events.
// Invokes a user-provided callback for each event received from the server.
//
// The callback may set the cancelled flag to stop further reads.  This is
// used by ClientBufferManager to prevent StartRead() from being called
// after the stream context has been cancelled (TryCancel), which avoids
// a use-after-free on the underlying gRPC call object in gRPC 1.46.
template <typename EventT> class EventStreamReader : public grpc::ClientReadReactor<EventT>
{
  public:
    using EventCallback = std::function<void(const EventT &)>;

    explicit EventStreamReader(EventCallback on_event) : m_on_event(std::move(on_event))
    {
    }

    /// Signal the reader to stop scheduling new reads.
    /// Safe to call from any thread.
    void cancel()
    {
        m_cancelled.store(true, std::memory_order_release);
    }

    void start()
    {
        READER_TRACE_BEGIN("reader_start");
        this->StartCall();
        this->StartRead(&m_current_event);
        READER_TRACE_END();
    }

    void OnReadDone(bool ok) override
    {
        READER_TRACE_BEGIN("reader_OnReadDone");
        if (!ok)
        {
            READER_TRACE_END();
            return;
        }
        {
            READER_TRACE_BEGIN("reader_on_event_callback");
            m_on_event(m_current_event);
            READER_TRACE_END();
        }
        // Do not issue another read if the owner has signalled cancellation.
        // The stream will end via OnDone after the context is cancelled.
        if (!m_cancelled.load(std::memory_order_acquire))
        {
            READER_TRACE_BEGIN("reader_StartRead");
            this->StartRead(&m_current_event);
            READER_TRACE_END();
        }
        READER_TRACE_END();
    }

    void OnDone(const grpc::Status &status) override
    {
        READER_TRACE_BEGIN("reader_OnDone");
        if (!status.ok())
        {
            LOGGER__MODULE__WARN(LoggerType::ServiceClient, "Event stream ended: {}", status.error_message());
        }
        READER_TRACE_END();
        delete this;
    }

  private:
    EventT m_current_event;
    EventCallback m_on_event;
    std::atomic<bool> m_cancelled{false};
};
