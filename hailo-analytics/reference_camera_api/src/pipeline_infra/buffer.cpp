#include "hailo_analytics/pipeline_infra/buffer.hpp"

// Metadata class implementation
Metadata::Metadata(MetadataType type) : m_type(type)
{
}

MetadataType Metadata::get_type() const
{
    return m_type;
}

// Buffer class implementation
Buffer::Buffer(HailoMediaLibraryBufferPtr buffer) : m_buffer(buffer)
{
}

Buffer::Buffer(Buffer &other)
{
    if (this != &other)
    { // prevent self-assignment
        m_buffer = other.m_buffer;
        //  shallow copy of metadata
        m_metadata.clear();
        m_metadata = other.m_metadata;
    }
}

HailoMediaLibraryBufferPtr Buffer::get_buffer() const
{
    return m_buffer;
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
