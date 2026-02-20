#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/muxing/demuxer_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::muxing
{

/**
 * @brief Constructs a DemuxerStage with the specified configuration.
 */
DemuxerStage::DemuxerStage(std::string name, std::string main_outlet_name, std::string sub_outlet_name,
                           size_t queue_size, bool leaky, bool trace_processing_operations, bool copy_roi_metadata)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
      m_main_outlet_name(main_outlet_name), m_sub_outlet_name(sub_outlet_name), m_copy_roi_metadata(copy_roi_metadata)
{
}

/**
 * @brief Processes the buffer by extracting sub-buffers and routing to appropriate outlets.
 *
 * The processing flow:
 * 1. Searches for BufferMetadata in the incoming buffer
 * 2. Extracts the first sub-buffer found in the metadata
 * 3. Optionally copies HailoROI metadata to the sub-buffer
 * 4. Sends the sub-buffer to the sub outlet
 * 5. Removes the metadata from the main buffer
 * 6. Sends the main buffer to the main outlet
 */
AppStatus DemuxerStage::process(BufferPtr buffer)
{
    // Look for BufferMetadata in the buffer first
    auto metadata_list = buffer->get_metadata_of_type(MetadataType::UNKNOWN);
    for (auto &metadata : metadata_list)
    {
        // Cast to BufferMetadata to check if it contains a sub buffer
        auto buffer_metadata = std::dynamic_pointer_cast<BufferMetadata>(metadata);
        if (buffer_metadata != nullptr)
        {
            // Extract the sub buffer and send it to the sub outlet FIRST
            BufferPtr sub_buffer = buffer_metadata->get_buffer();
            if (sub_buffer != nullptr)
            {
                // Copy HailoROI metadata if enabled
                if (m_copy_roi_metadata)
                {
                    copy_hailo_roi_metadata(buffer, sub_buffer);
                }

                send_to_specific_subscriber(m_sub_outlet_name, sub_buffer);

                // Remove the metadata from the main buffer since we've extracted it
                buffer->remove_metadata(metadata);
                break; // Only process the first BufferMetadata found
            }
        }
    }

    // Send the main buffer forward AFTER extracting and sending sub buffer
    send_to_specific_subscriber(m_main_outlet_name, buffer);

    return AppStatus::SUCCESS;
}

/**
 * @brief Copies all HailoROI objects from the main buffer to the sub-buffer.
 *
 * Iterates through all objects in the main buffer's ROI and adds them to the
 * sub-buffer's ROI. This ensures that detection results and other AI metadata
 * are available in both output streams.
 */
void DemuxerStage::copy_hailo_roi_metadata(BufferPtr main_buffer, BufferPtr sub_buffer)
{
    // Get the HailoROI from the main buffer
    HailoROIPtr main_roi = main_buffer->get_roi();
    HailoROIPtr sub_roi = sub_buffer->get_roi();

    if (main_roi && sub_roi)
    {
        // Copy all objects from main ROI to sub ROI
        std::vector<HailoObjectPtr> objects = main_roi->get_objects();
        for (const auto &object : objects)
        {
            // Create a copy of the object and add it to the sub buffer's ROI
            sub_roi->add_object(object);
        }
    }
}

// Builder implementation
DemuxerStageBuild::Builder &DemuxerStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

DemuxerStageBuild::Builder &DemuxerStageBuild::Builder::set_main_outlet_name(std::string name)
{
    m_main_outlet_name = name;
    return *this;
}

DemuxerStageBuild::Builder &DemuxerStageBuild::Builder::set_sub_outlet_name(std::string name)
{
    m_sub_outlet_name = name;
    return *this;
}

DemuxerStageBuild::Builder &DemuxerStageBuild::Builder::set_queue_size(size_t size)
{
    m_queue_size = size;
    return *this;
}

DemuxerStageBuild::Builder &DemuxerStageBuild::Builder::set_leaky_opt(bool leaky)
{
    m_leaky = leaky;
    return *this;
}

DemuxerStageBuild::Builder &DemuxerStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

DemuxerStageBuild::Builder &DemuxerStageBuild::Builder::set_copy_roi_metadata_opt(bool copy_roi)
{
    m_copy_roi_metadata = copy_roi;
    return *this;
}

std::shared_ptr<DemuxerStage> DemuxerStageBuild::Builder::buildptr() const
{
    if (!m_stage_name.has_value())
    {
        throw std::invalid_argument("Stage name is required");
    }
    if (!m_main_outlet_name.has_value())
    {
        throw std::invalid_argument("Main outlet name is required");
    }
    if (!m_sub_outlet_name.has_value())
    {
        throw std::invalid_argument("Sub outlet name is required");
    }

    return std::make_shared<DemuxerStage>(m_stage_name.value(), m_main_outlet_name.value(), m_sub_outlet_name.value(),
                                          m_queue_size, m_leaky, m_trace, m_copy_roi_metadata);
}

DemuxerStageBuild::Builder DemuxerStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::muxing
