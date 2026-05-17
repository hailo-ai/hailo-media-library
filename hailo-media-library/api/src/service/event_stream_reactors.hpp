#pragma once

#include "media_library/media_library_logger.hpp"
#include <functional>
#include <grpcpp/grpcpp.h>
#include <mutex>
#include <queue>

// Server-side reactor for event streaming RPCs.
// Holds a gRPC server-write stream open and pushes events from MediaLibrary callbacks.
template <typename EventT> class EventStreamBroker : public grpc::ServerWriteReactor<EventT>
{
  public:
    EventStreamBroker() : m_writing(false), m_finished(false)
    {
    }

    void push_event(EventT event)
    {
        if (enqueue_event(std::move(event)))
            this->StartWrite(&m_current_event);
    }

    void OnWriteDone(bool ok) override
    {
        auto action = dequeue_next(ok);
        if (action == WriteAction::FINISH)
            this->Finish(grpc::Status::OK);
        if (action == WriteAction::WRITE)
            this->StartWrite(&m_current_event);
    }

    // Gracefully close the stream from the server side (e.g. when a new subscriber replaces this one).
    void finish()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_finished)
        {
            m_finished = true;
            this->Finish(grpc::Status::OK);
        }
    }

    void OnCancel() override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_finished)
        {
            m_finished = true;
            this->Finish(grpc::Status::CANCELLED);
        }
    }

    void OnDone() override
    {
        delete this;
    }

  private:
    enum class WriteAction
    {
        NONE,
        WRITE,
        FINISH
    };

    /// Enqueue an event and claim write ownership if idle.
    /// Returns true when the caller should call StartWrite (outside the lock).
    bool enqueue_event(EventT event)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_finished)
            return false;
        m_pending_events.push(std::move(event));
        LOGGER__MODULE__INFO(LoggerType::Service, "EventStreamBroker::push_event: pending_events={} writing={}",
                             m_pending_events.size(), m_writing);
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

    std::mutex m_mutex;
    std::queue<EventT> m_pending_events;
    EventT m_current_event;
    bool m_writing;
    bool m_finished;
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
        this->StartCall();
        this->StartRead(&m_current_event);
    }

    void OnReadDone(bool ok) override
    {
        if (!ok)
            return;
        m_on_event(m_current_event);
        // Do not issue another read if the owner has signalled cancellation.
        // The stream will end via OnDone after the context is cancelled.
        if (!m_cancelled.load(std::memory_order_acquire))
            this->StartRead(&m_current_event);
    }

    void OnDone(const grpc::Status &status) override
    {
        if (!status.ok())
        {
            LOGGER__MODULE__WARN(LoggerType::ServiceClient, "Event stream ended: {}", status.error_message());
        }
        delete this;
    }

  private:
    EventT m_current_event;
    EventCallback m_on_event;
    std::atomic<bool> m_cancelled{false};
};
