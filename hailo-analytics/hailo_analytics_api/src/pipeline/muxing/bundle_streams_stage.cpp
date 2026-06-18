#include "hailo_analytics/pipeline/muxing/bundle_streams_stage.hpp"

#include <algorithm>
#include <chrono>
#include <compare>
#include <stdexcept>
#include <thread>
#include <utility>
#include <atomic>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage_tracing.hpp"

namespace hailo_analytics::pipeline::muxing
{

static constexpr auto COLLECT_TIMEOUT = std::chrono::seconds(5);

BundleStreamsStage::BundleStreamsStage(std::string name, std::string carrier_stream_id,
                                       std::vector<std::string> passenger_stream_ids, size_t queue_size, bool leaky,
                                       bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(std::move(name), queue_size, leaky, trace_processing_operations),
      m_carrier_stream_id(std::move(carrier_stream_id)), m_passenger_stream_ids(std::move(passenger_stream_ids))
{
    // Pre-create one queue per declared stream id.
    m_carrier_queue = std::make_shared<Queue>(m_stage_name, m_carrier_stream_id, queue_size, leaky);
    m_queues.push_back(m_carrier_queue);
    m_passenger_queues.reserve(m_passenger_stream_ids.size());
    for (const auto &id : m_passenger_stream_ids)
    {
        auto stage_queue = std::make_shared<Queue>(m_stage_name, id, queue_size, leaky);
        m_queues.push_back(stage_queue);
        m_passenger_queues.push_back(std::move(stage_queue));
    }
}

void BundleStreamsStage::add_queue(std::string publisher_name)
{
    const bool is_carrier = (publisher_name == m_carrier_stream_id);
    const bool is_passenger = std::find(m_passenger_stream_ids.begin(), m_passenger_stream_ids.end(), publisher_name) !=
                              m_passenger_stream_ids.end();
    if (!is_carrier && !is_passenger)
    {
        throw std::invalid_argument("BundleStreamsStage[" + m_stage_name + "]: unknown publisher '" + publisher_name +
                                    "' — not in carrier or passenger roster");
    }
    // Queue already created in ctor; nothing else to do.
}

bool BundleStreamsStage::collect_frame_set(BufferPtr &carrier, std::vector<BufferPtr> &passengers,
                                           std::chrono::seconds timeout)
{
    passengers.assign(m_passenger_queues.size(), nullptr);
    size_t remaining = 1 + m_passenger_queues.size();
    auto last_progress = std::chrono::steady_clock::now();

    while (remaining > 0)
    {
        if (m_end_of_stream)
            return false;

        bool made_progress = false;

        if (!carrier && m_carrier_queue->size() > 0)
        {
            carrier = m_carrier_queue->pop();
            if (!carrier)
            {
                HAILO_ANALYTICS_LOG_WARN("BundleStreamsStage[{}]: carrier queue '{}' closed during bundle assembly",
                                         m_stage_name, m_carrier_stream_id);
                return false;
            }
            --remaining;
            made_progress = true;
        }
        for (size_t i = 0; i < m_passenger_queues.size(); ++i)
        {
            if (passengers[i] || m_passenger_queues[i]->size() == 0)
                continue;
            passengers[i] = m_passenger_queues[i]->pop();
            if (!passengers[i])
            {
                HAILO_ANALYTICS_LOG_WARN("BundleStreamsStage[{}]: passenger queue '{}' closed during bundle assembly",
                                         m_stage_name, m_passenger_stream_ids[i]);
                return false;
            }
            --remaining;
            made_progress = true;
        }

        if (made_progress)
        {
            last_progress = std::chrono::steady_clock::now();
            continue;
        }

        if (std::chrono::steady_clock::now() - last_progress >= timeout)
        {
            std::string missing = carrier ? "" : m_carrier_stream_id;
            for (size_t i = 0; i < m_passenger_queues.size(); ++i)
            {
                if (passengers[i])
                    continue;
                missing += (missing.empty() ? "" : ", ") + m_passenger_stream_ids[i];
            }
            HAILO_ANALYTICS_LOG_WARN("BundleStreamsStage[{}]: timed out after {}s waiting for stream(s): {}",
                                     m_stage_name, timeout.count(), missing);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

void BundleStreamsStage::loop()
{
    while (!m_end_of_stream)
    {
        BufferPtr carrier;
        std::vector<BufferPtr> passengers;
        if (!collect_frame_set(carrier, passengers, COLLECT_TIMEOUT))
            break; // collect_frame_set logged why

        m_tracing->trace_processing_start(carrier);
        for (size_t i = 0; i < passengers.size(); ++i)
            carrier->add_metadata(
                std::make_shared<AttachedStreamMetadata>(std::move(passengers[i]), m_passenger_stream_ids[i]));
        m_tracing->trace_processing_end(carrier);
        send_to_subscribers(carrier);
    }
}

// ---- Builder ----

BundleStreamsStageBuild::Builder &BundleStreamsStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = std::move(name);
    return *this;
}

BundleStreamsStageBuild::Builder &BundleStreamsStageBuild::Builder::set_carrier_stream_id(std::string id)
{
    m_carrier_stream_id = std::move(id);
    return *this;
}

BundleStreamsStageBuild::Builder &BundleStreamsStageBuild::Builder::set_passenger_stream_ids(
    std::vector<std::string> ids)
{
    m_passenger_stream_ids = std::move(ids);
    return *this;
}

BundleStreamsStageBuild::Builder &BundleStreamsStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}

BundleStreamsStageBuild::Builder &BundleStreamsStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

BundleStreamsStageBuild::Builder &BundleStreamsStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<BundleStreamsStage> BundleStreamsStageBuild::Builder::buildptr() const
{
    if (!m_stage_name.has_value())
        throw std::invalid_argument("BundleStreamsStage: stage_name is required");
    if (!m_carrier_stream_id.has_value())
        throw std::invalid_argument("BundleStreamsStage: carrier_stream_id is required");
    if (m_passenger_stream_ids.empty())
        throw std::invalid_argument("BundleStreamsStage: at least one passenger stream id is required");
    // Every passenger id must be distinct from the carrier and from every other passenger.
    if (std::find(m_passenger_stream_ids.begin(), m_passenger_stream_ids.end(), m_carrier_stream_id.value()) !=
        m_passenger_stream_ids.end())
        throw std::invalid_argument("BundleStreamsStage: carrier_stream_id appears in passenger list");
    for (size_t i = 0; i < m_passenger_stream_ids.size(); ++i)
        for (size_t j = i + 1; j < m_passenger_stream_ids.size(); ++j)
            if (m_passenger_stream_ids[i] == m_passenger_stream_ids[j])
                throw std::invalid_argument("BundleStreamsStage: duplicate passenger stream id '" +
                                            m_passenger_stream_ids[i] + "'");

    return std::make_shared<BundleStreamsStage>(m_stage_name.value(), m_carrier_stream_id.value(),
                                                m_passenger_stream_ids, m_queue_size, m_leaky, m_trace);
}

BundleStreamsStageBuild::Builder BundleStreamsStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::muxing
