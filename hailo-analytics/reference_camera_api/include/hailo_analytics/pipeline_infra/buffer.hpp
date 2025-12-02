#pragma once

// general includes
#include <vector>

// medialibrary includes
#include "hailo/media_library/buffer_pool.hpp"

class Buffer;
using BufferPtr = std::shared_ptr<Buffer>;

enum class MetadataType
{
    UNKNOWN,
    TENSOR,
    EXPECTED_CROPS,
    SIZE,
    BATCH,
};

class Metadata
{
  private:
    MetadataType m_type;

  public:
    Metadata(MetadataType type = MetadataType::UNKNOWN);
    virtual ~Metadata() = default;

    MetadataType get_type() const;
};
using MetadataPtr = std::shared_ptr<Metadata>;

class Buffer
{
  private:
    HailoMediaLibraryBufferPtr m_buffer;
    std::vector<MetadataPtr> m_metadata;

  public:
    Buffer(HailoMediaLibraryBufferPtr buffer);
    Buffer(Buffer &other);

    // Buffer Content methods
    HailoMediaLibraryBufferPtr get_buffer() const;

    // Metadata methods
    void add_metadata(MetadataPtr metadata);
    void remove_metadata(MetadataPtr metadata);
    std::vector<MetadataPtr> get_metadata_of_type(MetadataType metadata_type) const;
};
