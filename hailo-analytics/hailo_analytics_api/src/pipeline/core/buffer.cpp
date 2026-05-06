#include "hailo_analytics/pipeline/core/buffer.hpp"

namespace hailo_analytics::pipeline
{

// Metadata class implementation
Metadata::Metadata(MetadataType type) : m_type(type)
{
}

MetadataType Metadata::get_type() const
{
    return m_type;
}

// SizeMetadata class implementation
SizeMetadata::SizeMetadata(std::string label, size_t size) : Metadata(MetadataType::SIZE), m_label(label), m_size(size)
{
}

std::string SizeMetadata::get_label()
{
    return m_label;
}

size_t SizeMetadata::get_size()
{
    return m_size;
}

// BufferMetadata class implementation
BufferMetadata::BufferMetadata(BufferPtr buffer, MetadataType type) : Metadata(type), m_buffer(buffer)
{
}

BufferPtr BufferMetadata::get_buffer()
{
    return m_buffer;
}

// TensorMetadata class implementation
TensorMetadata::TensorMetadata(BufferPtr buffer, std::string tensor_name)
    : BufferMetadata(buffer, MetadataType::TENSOR), m_tensor_name(tensor_name)
{
}

std::string TensorMetadata::get_tensor_name()
{
    return m_tensor_name;
}

// BatchMetadata class implementation
BatchMetadata::BatchMetadata(size_t total_size, size_t index)
    : Metadata(MetadataType::BATCH), m_total_size(total_size), m_index(index)
{
}

size_t BatchMetadata::get_total_size()
{
    return m_total_size;
}

size_t BatchMetadata::get_index()
{
    return m_index;
}

CroppingMetadata::CroppingMetadata(int num_crops) : Metadata(MetadataType::EXPECTED_CROPS), m_num_crops(num_crops)
{
}

int CroppingMetadata::get_num_crops()
{
    return m_num_crops;
}

// SkippedDetectionsMetadata class implementation
SkippedDetectionsMetadata::SkippedDetectionsMetadata(std::vector<HailoDetectionPtr> detections)
    : Metadata(MetadataType::SKIPPED_DETECTIONS), m_skipped_detections(std::move(detections))
{
}

const std::vector<HailoDetectionPtr> &SkippedDetectionsMetadata::get_skipped_detections() const
{
    return m_skipped_detections;
}

// Buffer class implementation
Buffer::Buffer(HailoMediaLibraryBufferPtr buffer) : m_buffer(buffer)
{
    m_roi = std::make_shared<HailoROI>(HailoROI(HailoBBox(0.0f, 0.0f, 1.0f, 1.0f)));
}

Buffer::Buffer(Buffer &other)
{
    if (this != &other)
    { // prevent self-assignment
        m_buffer = other.m_buffer;
        m_roi = std::make_shared<HailoROI>(*other.m_roi);
        //  shallow copy of metadata
        m_metadata.clear();
        m_metadata = other.m_metadata;
    }
}

Buffer::Buffer(HailoMediaLibraryBufferPtr buffer, HailoROIPtr roi) : m_buffer(buffer)
{
    if (roi)
    {
        m_roi = roi;
    }
    else
    {
        m_roi = std::make_shared<HailoROI>(HailoROI(HailoBBox(0.0f, 0.0f, 1.0f, 1.0f)));
    }
}

HailoMediaLibraryBufferPtr Buffer::get_buffer() const
{
    return m_buffer;
}

HailoROIPtr Buffer::get_roi() const
{
    return m_roi;
}

void Buffer::add_metadata(MetadataPtr metadata)
{
    m_metadata.push_back(metadata);
}

void Buffer::remove_metadata(MetadataPtr metadata)
{
    for (auto it = m_metadata.begin(); it != m_metadata.end(); ++it)
    {
        if (*it == metadata)
        {
            m_metadata.erase(it);
            break;
        }
    }
}

std::vector<MetadataPtr> Buffer::get_metadata_of_type(MetadataType metadata_type) const
{
    std::vector<MetadataPtr> metadata;
    for (const auto &m : m_metadata)
    {
        if (m->get_type() == metadata_type)
        {
            metadata.push_back(m);
        }
    }
    return metadata;
}

} // namespace hailo_analytics::pipeline
