#pragma once

#include "base.hpp"

class BBoxBufferMetadata : public BufferMetadata
{
public:
    std::vector<BBox> m_bboxes;

    BBoxBufferMetadata(std::vector<BBox> bboxes) : BufferMetadata(BufferMetadataType::BBox),
    m_bboxes(bboxes) { }
};
using BBoxBufferMetadataPtr = std::shared_ptr<BBoxBufferMetadata>;


class CroppedBufferMetadata : public BufferMetadata
{
public:
    BufferPtr parent_buffer;
    size_t crop_start_x;
    size_t crop_end_x;
    size_t crop_start_y;
    size_t crop_end_y;

    CroppedBufferMetadata(BufferPtr parent_buffer, size_t crop_start_x, size_t crop_end_x, size_t crop_start_y, size_t crop_end_y) :
        BufferMetadata(BufferMetadataType::Cropped), 
        parent_buffer(parent_buffer), crop_start_x(crop_start_x), crop_end_x(crop_end_x), crop_start_y(crop_start_y), crop_end_y(crop_end_y)
        {
            parent_buffer->increase_refcounts();
        }

    int get_width()
    {
        return crop_end_x - crop_start_x;
    }

    int get_height()
    {
        return crop_end_y - crop_start_y;
    }

    ~CroppedBufferMetadata()
    {
        parent_buffer->decrease_refcounts();
    }
};
using CroppedBufferMetadataPtr = std::shared_ptr<CroppedBufferMetadata>; 
