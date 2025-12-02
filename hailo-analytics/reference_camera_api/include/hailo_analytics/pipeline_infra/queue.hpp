#pragma once

// General includes
#include <atomic>
#include <optional>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <string>
#include <memory>
#include <iostream>

// Infra includes
#include "buffer.hpp"

// Forward declaration of internal QueueTracing class
class QueueTracing;

class Queue
{
  private:
    std::queue<BufferPtr> m_queue;
    size_t m_max_buffers;
    bool m_leaky;
    std::string m_name;
    std::atomic<bool> m_flushing;
    std::unique_ptr<std::condition_variable> m_condvar;
    std::shared_ptr<std::mutex> m_mutex;
    std::unique_ptr<QueueTracing> m_tracing;

  public:
    Queue(std::string parent_name, std::string queue_name, size_t max_buffers, bool leaky = false);
    ~Queue();

    std::string name();
    int size();
    void push(BufferPtr buffer);
    BufferPtr pop();
    void flush();
};

using QueuePtr = std::shared_ptr<Queue>;
