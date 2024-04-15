#pragma once

#include "base.hpp"

BufferPtr create_buffer_ptr_with_deleter(std::unordered_map<MediaLibraryBufferType, hailo_media_library_buffer*> media_lib_buffers_list)
{
    BufferPtr buffer = std::make_shared<Buffer>(true);
    for (auto& [key, media_lib_bffer] : media_lib_buffers_list)
    {
        buffer->add_media_lib_buffer(key, HailoMediaLibraryBufferPtr(media_lib_bffer, [](hailo_media_library_buffer* buf) { buf->decrease_ref_count(); }));
    }
    return buffer;
}

void drop_buffer(BufferPtr buffer)
{
    buffer->decrease_refcounts();
}

template <typename T>
std::shared_ptr<T> get_metadata(BufferPtr buffer, const BufferMetadataType& key)
{
    static_assert(std::is_base_of<BufferMetadata, T>::value);
    BufferMetadataPtr buffer_metadata = buffer->get_metadata(key);
    return buffer_metadata ? std::static_pointer_cast<T>(buffer_metadata) : nullptr;

}
