
#include <zmq.hpp>
#include <queue>
#include <nlohmann/json.hpp>
#include <cmath>
#include <memory>

#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/sinks/zmq_comm_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

using json = nlohmann::json;

namespace hailo_analytics::pipeline::sinks
{

ZmqCommStage::ZmqCommStage(std::string name, Mode mode, const std::string &pub_address, const std::string &sub_address,
                           size_t queue_size, bool leaky, bool trace_processing_operations, bool print_fps)
    : ThreadedStage(name, queue_size, leaky, trace_processing_operations), m_mode(mode), m_pub_address(pub_address),
      m_sub_address(sub_address), m_print_fps(print_fps)
{
}

void ZmqCommStage::init_zmq_publisher(const std::string &address)
{
    m_zmq_publisher = zmq::socket_t(m_context, zmq::socket_type::pub);
    m_zmq_publisher.bind(address);
}

void ZmqCommStage::init_zmq_subscriber(const std::string &address)
{
    m_zmq_subscriber = zmq::socket_t(m_context, ZMQ_SUB);
    m_zmq_subscriber.connect(address);
    m_zmq_subscriber.set(zmq::sockopt::subscribe, "");
    m_zmq_subscriber.set(zmq::sockopt::rcvtimeo, 1000);
}

void ZmqCommStage::receive_messages()
{
    try
    {
        while (m_run_subscriber.load())
        {
            zmq::message_t msg;
            auto ok = m_zmq_subscriber.recv(msg, zmq::recv_flags::none);
            if (!ok)
            {
                if (zmq_errno() == EAGAIN)
                    continue;
                if (zmq_errno() == ETERM)
                    break;
                continue;
            }
            std::string json_str(static_cast<char *>(msg.data()), msg.size());

            { // critical-section
                std::lock_guard<std::mutex> lg(m_msg_mutex);
                m_latest_msg = std::move(json_str);
            }
        }
    }
    catch (const zmq::error_t &e)
    {
        std::cerr << "ZMQ recv error: " << e.what() << std::endl;
    }
}

AppStatus ZmqCommStage::init()
{
    if (m_mode == Mode::PUBLISHER)
    {
        init_zmq_publisher(m_pub_address);
    }
    else
    {
        init_zmq_subscriber(m_sub_address);
        m_run_subscriber = true;
        m_subscriber_thread = std::thread(&ZmqCommStage::receive_messages, this);
    }
    return AppStatus::SUCCESS;
}

AppStatus ZmqCommStage::deinit()
{
    if (m_mode == Mode::RECEIVER)
    {
        m_run_subscriber = false;
        m_zmq_subscriber.close();
        if (m_subscriber_thread.joinable())
            m_subscriber_thread.join();
    }
    if (m_mode == Mode::PUBLISHER)
    {
        m_zmq_publisher.close();
    }
    m_context.shutdown();
    return AppStatus::SUCCESS;
}

AppStatus ZmqCommStage::process(BufferPtr input_buffer)
{
    if (!input_buffer || !input_buffer->get_roi())
    {
        return AppStatus::SUCCESS;
    }
    HailoROIPtr roi = input_buffer->get_roi();

    if (m_mode == Mode::RECEIVER)
    {

        std::string local_json;
        {
            std::lock_guard<std::mutex> lg(m_msg_mutex);
            local_json = m_latest_msg;
        }

        if (!local_json.empty())
        {
            HailoZMQMessagePtr input_zmq = nullptr;

            for (const auto &obj : roi->get_objects())
            {
                if (obj->get_type() != HAILO_ZMQ)
                    continue;
                auto z = std::dynamic_pointer_cast<HailoZMQMessage>(obj);
                if (z && !z->get_input_msg().empty())
                {
                    input_zmq = z;
                    break;
                }
            }

            if (input_zmq)
            {
                input_zmq->set_input_msg(local_json);
            }
            else
            {
                auto zmq_msg = std::make_shared<HailoZMQMessage>();
                zmq_msg->set_input_msg(local_json);
                roi->add_object(zmq_msg);
            }
        }
    }
    else // send output
    {
        for (const auto &obj : roi->get_objects())
        {
            if (obj->get_type() == HAILO_ZMQ)
            {
                auto zmq_msg = std::dynamic_pointer_cast<HailoZMQMessage>(obj);
                if (zmq_msg && zmq_msg->has_output_msg())
                {
                    const std::string &out = zmq_msg->get_output_msg();
                    m_zmq_publisher.send(zmq::buffer(out), zmq::send_flags::none);
                }
            }
        }
    }
    return AppStatus::SUCCESS;
}

ZmqCommStageBuild::Builder &ZmqCommStageBuild::Builder::set_stage_name(const std::string &name)
{
    m_stage_name = name;
    return *this;
}

ZmqCommStageBuild::Builder &ZmqCommStageBuild::Builder::set_queue_size(size_t size)
{
    m_queue_size = size;
    return *this;
}

ZmqCommStageBuild::Builder &ZmqCommStageBuild::Builder::set_leaky(bool leaky)
{
    m_leaky = leaky;
    return *this;
}

ZmqCommStageBuild::Builder &ZmqCommStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

ZmqCommStageBuild::Builder &ZmqCommStageBuild::Builder::set_print_fps(bool print_fps)
{
    m_print_fps = print_fps;
    return *this;
}

ZmqCommStageBuild::Builder &ZmqCommStageBuild::Builder::set_mode(ZmqCommStage::Mode mode)
{
    m_mode = mode;
    return *this;
}

ZmqCommStageBuild::Builder &ZmqCommStageBuild::Builder::set_pub_address(const std::string &addr)
{
    m_pub_address = addr;
    return *this;
}

ZmqCommStageBuild::Builder &ZmqCommStageBuild::Builder::set_sub_address(const std::string &addr)
{
    m_sub_address = addr;
    return *this;
}

std::shared_ptr<ZmqCommStage> ZmqCommStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    if (m_mode == ZmqCommStage::Mode::PUBLISHER)
        THROW_IF_MISSING(m_pub_address.has_value(), "set_pub_address");
    else
        THROW_IF_MISSING(m_sub_address.has_value(), "set_sub_address");

    return std::make_shared<ZmqCommStage>(m_stage_name.value(), m_mode, m_pub_address.value_or(""),
                                          m_sub_address.value_or(""), m_queue_size, m_leaky, m_trace, m_print_fps);
}

ZmqCommStageBuild::Builder ZmqCommStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::sinks
