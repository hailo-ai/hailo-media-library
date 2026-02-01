#pragma once

// General Includes
#include <zmq.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>
#include <numeric>
#include <iostream>

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"

#define QUEUE_SIZE_DEFAULT (10)

namespace hailo_analytics::pipeline::sinks
{

class ZmqCommStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    enum class Mode
    {
        PUBLISHER,
        RECEIVER
    };

  private:
    Mode m_mode;
    std::string m_pub_address;
    std::string m_sub_address;

    zmq::context_t m_context{1};
    zmq::socket_t m_zmq_publisher;
    zmq::socket_t m_zmq_subscriber;
    bool m_initialized = false;
    zmq::message_t zmq_input_msg;
    zmq::message_t zmq_output_msg;
    std::mutex m_zmq_mutex;
    std::thread m_subscriber_thread;
    std::atomic<bool> m_run_subscriber{false};
    std::string m_latest_msg;
    std::mutex m_msg_mutex;
    bool m_print_fps;

    void init_zmq_publisher(const std::string &address);

    void init_zmq_subscriber(const std::string &address);

    void receive_messages();

  public:
    ZmqCommStage(std::string name, Mode mode, const std::string &pub_address, const std::string &sub_address,
                 size_t queue_size = QUEUE_SIZE_DEFAULT, bool leaky = false, bool trace_processing_operations = true,
                 bool print_fps = false);

    AppStatus init() override;
    AppStatus deinit() override;
    AppStatus process(BufferPtr input_buffer) override;
};

class ZmqCommStageBuild
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;
        bool m_print_fps = false;
        ZmqCommStage::Mode m_mode = ZmqCommStage::Mode::PUBLISHER;
        std::optional<std::string> m_pub_address;
        std::optional<std::string> m_sub_address;

      public:
        Builder &set_stage_name(const std::string &name);
        Builder &set_queue_size(size_t size);
        Builder &set_leaky(bool leaky);
        Builder &set_trace_opt(bool activate);
        Builder &set_print_fps(bool print_fps);
        Builder &set_mode(ZmqCommStage::Mode mode);
        Builder &set_pub_address(const std::string &addr);
        Builder &set_sub_address(const std::string &addr);

        std::shared_ptr<ZmqCommStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::sinks
