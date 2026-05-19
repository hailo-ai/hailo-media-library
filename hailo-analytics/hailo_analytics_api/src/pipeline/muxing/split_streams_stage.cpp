#include "hailo_analytics/pipeline/muxing/split_streams_stage.hpp"

#include <hailo_postprocess_tools/objects/hailo_objects.hpp>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

namespace hailo_analytics::pipeline::muxing
{

SplitStreamsStage::SplitStreamsStage(std::string name, std::string carrier_stream_id, bool propagate_roi,
                                     size_t queue_size, bool leaky, bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(std::move(name), queue_size, leaky, trace_processing_operations),
      m_carrier_stream_id(std::move(carrier_stream_id)), m_propagate_roi(propagate_roi)
{
}

AppStatus SplitStreamsStage::init()
{
    // Derive the routing roster from the registered subscribers' stream_ids (set at connect time
    // by the 3-arg PipelineBuilder::connect(src, stream_id, dst)). Every subscriber must have a
    // non-empty stream_id; exactly one matches the carrier; the rest become the passenger roster.
    m_passenger_stream_ids.clear();
    bool carrier_found = false;
    std::set<std::string> seen;

    if (m_subscribers.size() != m_subscriber_stream_ids.size())
    {
        HAILO_ANALYTICS_LOG_ERROR(
            "SplitStreamsStage[{}]: internal: m_subscribers and m_subscriber_stream_ids have different sizes",
            m_stage_name);
        return AppStatus::INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < m_subscribers.size(); ++i)
    {
        const auto &stream_id_opt = m_subscriber_stream_ids[i];
        if (!stream_id_opt.has_value() || stream_id_opt->empty())
        {
            HAILO_ANALYTICS_LOG_ERROR("SplitStreamsStage[{}]: subscriber '{}' connected without stream_id "
                                      "(use 3-arg connect(split, stream_id, target))",
                                      m_stage_name, m_subscribers[i]->get_name());
            return AppStatus::INVALID_ARGUMENT;
        }
        const auto &id = *stream_id_opt;
        if (!seen.insert(id).second)
        {
            HAILO_ANALYTICS_LOG_ERROR("SplitStreamsStage[{}]: duplicate subscriber stream_id '{}'", m_stage_name, id);
            return AppStatus::INVALID_ARGUMENT;
        }
        if (id == m_carrier_stream_id)
        {
            carrier_found = true;
        }
        else
        {
            m_passenger_stream_ids.insert(id);
        }
    }

    if (!carrier_found)
    {
        HAILO_ANALYTICS_LOG_ERROR("SplitStreamsStage[{}]: no subscriber registered for carrier stream_id '{}'",
                                  m_stage_name, m_carrier_stream_id);
        return AppStatus::INVALID_ARGUMENT;
    }

    return AppStatus::SUCCESS;
}

AppStatus SplitStreamsStage::process(BufferPtr buffer)
{
    if (!buffer)
    {
        HAILO_ANALYTICS_LOG_ERROR("SplitStreamsStage[{}]: null buffer", m_stage_name);
        return AppStatus::INVALID_ARGUMENT;
    }

    // The carrier holds N AttachedStreamMetadata instances — one per passenger.
    auto md_list = buffer->get_metadata_of_type(MetadataType::ATTACHED_STREAM);
    if (md_list.empty())
    {
        HAILO_ANALYTICS_LOG_ERROR("SplitStreamsStage[{}]: no AttachedStreamMetadata on incoming buffer", m_stage_name);
        return AppStatus::INVALID_ARGUMENT;
    }

    // Validate the roster while collecting typed pointers for dispatch.
    std::vector<AttachedStreamMetadataPtr> passengers;
    passengers.reserve(md_list.size());
    std::set<std::string> seen;
    for (const auto &md : md_list)
    {
        auto stream_md = std::dynamic_pointer_cast<AttachedStreamMetadata>(md);
        if (!stream_md)
        {
            HAILO_ANALYTICS_LOG_ERROR(
                "SplitStreamsStage[{}]: ATTACHED_STREAM metadata is not an AttachedStreamMetadata", m_stage_name);
            return AppStatus::INVALID_ARGUMENT;
        }
        const auto &id = stream_md->get_stream_id();
        if (id.empty())
        {
            HAILO_ANALYTICS_LOG_ERROR("SplitStreamsStage[{}]: passenger metadata has empty stream id", m_stage_name);
            return AppStatus::INVALID_ARGUMENT;
        }
        if (m_passenger_stream_ids.find(id) == m_passenger_stream_ids.end())
        {
            HAILO_ANALYTICS_LOG_ERROR("SplitStreamsStage[{}]: passenger stream id '{}' is not in the declared roster",
                                      m_stage_name, id);
            return AppStatus::INVALID_ARGUMENT;
        }
        if (!seen.insert(id).second)
        {
            HAILO_ANALYTICS_LOG_ERROR("SplitStreamsStage[{}]: duplicate passenger stream id '{}'", m_stage_name, id);
            return AppStatus::INVALID_ARGUMENT;
        }
        passengers.push_back(std::move(stream_md));
    }
    for (const auto &expected : m_passenger_stream_ids)
    {
        if (seen.find(expected) == seen.end())
        {
            HAILO_ANALYTICS_LOG_ERROR(
                "SplitStreamsStage[{}]: expected passenger stream id '{}' missing from incoming bundle", m_stage_name,
                expected);
            return AppStatus::INVALID_ARGUMENT;
        }
    }

    if (m_propagate_roi)
    {
        auto carrier_roi = buffer->get_roi();
        auto carrier_tensors = buffer->get_metadata_of_type(MetadataType::TENSOR);
        for (const auto &passenger_md : passengers)
        {
            auto passenger_buffer = passenger_md->get_buffer();
            if (!passenger_buffer)
                continue;
            if (carrier_roi)
                passenger_buffer->set_roi(carrier_roi);
            for (const auto &tensor_metadata : carrier_tensors)
                passenger_buffer->add_metadata(tensor_metadata);
        }
    }

    // Strip every ATTACHED_STREAM metadata from the carrier before forwarding it downstream.
    for (const auto &md : md_list)
        buffer->remove_metadata(md);

    // Dispatch by stream id — the connection's stream_id at PipelineBuilder::connect time is
    // what ThreadedStage::send_to_subscriber_by_stream_id matches against.
    send_to_subscriber_by_stream_id(m_carrier_stream_id, buffer);
    for (const auto &md : passengers)
        send_to_subscriber_by_stream_id(md->get_stream_id(), md->get_buffer());

    return AppStatus::SUCCESS;
}

// ---- Builder ----

SplitStreamsStageBuild::Builder &SplitStreamsStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = std::move(name);
    return *this;
}

SplitStreamsStageBuild::Builder &SplitStreamsStageBuild::Builder::set_carrier_stream_id(std::string id)
{
    m_carrier_stream_id = std::move(id);
    return *this;
}

SplitStreamsStageBuild::Builder &SplitStreamsStageBuild::Builder::set_propagate_roi_opt(bool activate)
{
    m_propagate_roi = activate;
    return *this;
}

SplitStreamsStageBuild::Builder &SplitStreamsStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}

SplitStreamsStageBuild::Builder &SplitStreamsStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

SplitStreamsStageBuild::Builder &SplitStreamsStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<SplitStreamsStage> SplitStreamsStageBuild::Builder::buildptr() const
{
    if (!m_stage_name.has_value())
        throw std::invalid_argument("SplitStreamsStage: stage_name is required");
    if (!m_carrier_stream_id.has_value())
        throw std::invalid_argument("SplitStreamsStage: carrier_stream_id is required");

    return std::make_shared<SplitStreamsStage>(m_stage_name.value(), m_carrier_stream_id.value(), m_propagate_roi,
                                               m_queue_size, m_leaky, m_trace);
}

SplitStreamsStageBuild::Builder SplitStreamsStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::muxing
