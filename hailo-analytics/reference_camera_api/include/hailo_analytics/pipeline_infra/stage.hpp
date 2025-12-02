#pragma once

// General includes
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Infra includes
#include "buffer.hpp"
#include "queue.hpp"
#include "stage_tracing.hpp"

enum class AppStatus
{
    SUCCESS = 0,
    INVALID_ARGUMENT,
    CONFIGURATION_ERROR,
    BUFFER_ALLOCATION_ERROR,
    HAILORT_ERROR,
    DSP_OPERATION_ERROR,
    UNINITIALIZED,
    PIPELINE_ERROR,
    DMA_ERROR,
    MEDIA_LIBRARY_ERROR
};

class Stage;
using StagePtr = std::shared_ptr<Stage>;
class Stage
{
  protected:
    std::string m_stage_name;                // Name of the stage
    std::unique_ptr<StageTracing> m_tracing; // Perfetto tracing object for the stage
    bool m_trace_processing_operations;      // Whether to trace processing operations

  public:
    Stage(std::string name, bool trace_processing_operations = true);
    virtual ~Stage() = default;

    std::string get_name() const;
    void trace_fps();

    // Virtuals to override
    virtual AppStatus start() = 0;
    virtual AppStatus stop() = 0;
    virtual void add_subscriber(StagePtr subscriber) = 0;
    virtual void add_queue(std::string publisher_name) = 0;
    virtual void push(BufferPtr data, std::string publisher_name) = 0;
};

class ThreadedStage : public Stage
{
  protected:
    // Threading parameters
    std::atomic<bool> m_end_of_stream = false;
    std::thread m_thread;

    // Queue parameters
    size_t m_queue_size;
    bool m_leaky;
    std::vector<QueuePtr> m_queues;

    // Subscribers
    std::vector<StagePtr> m_subscribers;

  public:
    ThreadedStage(std::string name, size_t queue_size, bool leaky = false, bool trace_processing_operations = true);

    // Overrides
    AppStatus start() override;
    AppStatus stop() override;
    void add_subscriber(StagePtr subscriber) override;
    void add_queue(std::string publisher_name) override;
    void push(BufferPtr data, std::string publisher_name) override;

    // Virtuals to override
    virtual AppStatus init();
    virtual AppStatus deinit();
    virtual AppStatus process(BufferPtr buffer);
    virtual void loop();

    // Threaded stage functions
    void send_to_specific_subscriber(std::string stage_name, BufferPtr data);
    void set_end_of_stream(bool end_of_stream);
};
using ThreadedStagePtr = std::shared_ptr<ThreadedStage>;
