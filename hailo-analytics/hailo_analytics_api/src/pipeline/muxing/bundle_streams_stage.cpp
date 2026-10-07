#include "hailo_analytics/pipeline/muxing/bundle_streams_stage.hpp"

#include <algorithm>
#include <chrono>
#include <compare>
#include <mutex>
#include <media_library/buffer_pool.hpp>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <atomic>
#include <vector>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage_tracing.hpp"

namespace hailo_analytics::pipeline::muxing
{

BundleStreamsStage::BundleStreamsStage(std::string name, std::string carrier_stream_id,
                                       std::vector<std::string> passenger_stream_ids, size_t queue_size, bool leaky,
                                       bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(std::move(name), queue_size, leaky, trace_processing_operations),
      m_carrier_stream_id(std::move(carrier_stream_id)), m_passenger_stream_ids(std::move(passenger_stream_ids))
{
    auto wake = [this] { notify_input_ready(); };
    m_carrier_queue = std::make_shared<Queue>(m_stage_name, m_carrier_stream_id, queue_size, leaky);
    m_carrier_queue->set_notify_callback(wake);
    m_queues.push_back(m_carrier_queue);
    m_passenger_queues.reserve(m_passenger_stream_ids.size());
    for (const auto &id : m_passenger_stream_ids)
    {
        auto stage_queue = std::make_shared<Queue>(m_stage_name, id, queue_size, leaky);
        stage_queue->set_notify_callback(wake);
        m_queues.push_back(stage_queue);
        m_passenger_queues.push_back(std::move(stage_queue));
    }
}

BundleStreamsStage::~BundleStreamsStage()
{
    // The queues outlive m_ready_mutex/m_ready_cv; detach the callback so ~Queue() can't touch them.
    for (auto &queue : m_queues)
        queue->set_notify_callback(nullptr);
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

void BundleStreamsStage::push(BufferPtr data, std::string publisher_name)
{
    if (lists_all_other_streams(data, publisher_name))
    {
        // Complete frame: the frontend produced a buffer for every bundled stream, so this buffer's
        // partners will arrive. This also proves the streams share frames.
        m_streams_share_frames.store(true, std::memory_order_relaxed);
    }
    else if (m_streams_share_frames.load(std::memory_order_relaxed))
    {
        // Incomplete frame: the frontend skipped one of the bundled streams (e.g. its buffer pool was
        // empty). No partner will ever match this buffer, and queued it would block the producer.
        m_unmatched_drops.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // Streams that never share frames (e.g. another stage's output) are always queued as they come.
    ThreadedStage::push(std::move(data), std::move(publisher_name));
}

bool BundleStreamsStage::lists_all_other_streams(const BufferPtr &data, const std::string &publisher_name) const
{
    auto media_buffer = data ? data->get_buffer() : nullptr;
    if (!media_buffer)
        return false;
    auto is_listed = [&](const std::string &stream_id) {
        return stream_id == publisher_name || media_buffer->concurrent_stream_ids.count(stream_id) > 0;
    };
    return is_listed(m_carrier_stream_id) &&
           std::all_of(m_passenger_stream_ids.begin(), m_passenger_stream_ids.end(), is_listed);
}

void BundleStreamsStage::notify_input_ready()
{
    // Without the lock, a notify between collect_frame_set()'s check and wait is lost.
    std::lock_guard<std::mutex> lock(m_ready_mutex);
    m_ready_cv.notify_one();
}

bool BundleStreamsStage::all_inputs_ready() const
{
    if (m_carrier_queue->size() == 0)
        return false;
    for (const auto &queue : m_passenger_queues)
        if (queue->size() == 0)
            return false;
    return true;
}

BundleStreamsStage::CollectStatus BundleStreamsStage::collect_frame_set(BufferPtr &carrier,
                                                                        std::vector<BufferPtr> &passengers,
                                                                        std::chrono::seconds timeout)
{
    // This thread is the only consumer, so queues seen non-empty stay non-empty until popped here;
    // a null pop() therefore means a concurrent flush() on shutdown.
    {
        std::unique_lock<std::mutex> lock(m_ready_mutex);
        if (!m_ready_cv.wait_for(lock, timeout, [this] { return m_end_of_stream || all_inputs_ready(); }))
            return CollectStatus::TIMED_OUT;
        if (m_end_of_stream)
            return CollectStatus::END_OF_STREAM;
    }

    carrier = m_carrier_queue->pop();
    if (!carrier)
        return CollectStatus::END_OF_STREAM;
    passengers.assign(m_passenger_queues.size(), nullptr);
    for (size_t index = 0; index < m_passenger_queues.size(); ++index)
    {
        passengers[index] = m_passenger_queues[index]->pop();
        if (!passengers[index])
            return CollectStatus::END_OF_STREAM;
    }
    return CollectStatus::FRAME_SET_READY;
}

void BundleStreamsStage::log_missing_streams(std::chrono::seconds timeout) const
{
    std::string missing = m_carrier_queue->size() ? "" : m_carrier_stream_id;
    for (size_t index = 0; index < m_passenger_queues.size(); ++index)
    {
        if (m_passenger_queues[index]->size())
            continue;
        missing += (missing.empty() ? "" : ", ") + m_passenger_stream_ids[index];
    }
    HAILO_ANALYTICS_LOG_WARN("BundleStreamsStage[{}]: no complete frame set for {}s, still waiting for stream(s): {}",
                             m_stage_name, timeout.count(), missing);
}

void BundleStreamsStage::report_unmatched_drops()
{
    const auto now = std::chrono::steady_clock::now();
    if (now - m_last_drop_report < COLLECT_TIMEOUT)
        return;
    const size_t dropped = m_unmatched_drops.exchange(0, std::memory_order_relaxed);
    if (dropped == 0)
        return;
    m_last_drop_report = now;
    HAILO_ANALYTICS_LOG_WARN("BundleStreamsStage[{}]: dropped {} unpaired buffer(s) (another stream skipped their "
                             "frame, e.g. empty buffer pool)",
                             m_stage_name, dropped);
}

void BundleStreamsStage::loop()
{
    while (!m_end_of_stream)
    {
        BufferPtr carrier;
        std::vector<BufferPtr> passengers;
        const auto status = collect_frame_set(carrier, passengers, COLLECT_TIMEOUT);
        if (status == CollectStatus::END_OF_STREAM)
            break;
        report_unmatched_drops();
        if (status == CollectStatus::TIMED_OUT)
        {
            log_missing_streams(COLLECT_TIMEOUT);
            continue;
        }

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
