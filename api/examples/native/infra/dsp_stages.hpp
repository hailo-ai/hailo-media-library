#pragma once

#include "stages.hpp"
#include "utils.hpp"

class DspBaseCropStage : public ProducableBufferStage
{
private:
    MediaLibraryBufferPoolPtr m_buffer_pool;
    int m_output_pool_size;

protected:
    int m_max_output_width;
    int m_max_output_height;

public:
    DspBaseCropStage(std::string name, int output_pool_size, int max_output_width, int max_output_height, size_t queue_size, bool leaky=true,
     int non_leaky_timeout_in_ms = 1000) : ProducableBufferStage(name, queue_size, leaky, non_leaky_timeout_in_ms), 
             m_output_pool_size(output_pool_size), m_max_output_width(max_output_width), m_max_output_height(max_output_height) {}

    int init() override
    {
        auto bytes_per_line = dsp_utils::get_dsp_desired_stride_from_width(m_max_output_width);
        m_buffer_pool = std::make_shared<MediaLibraryBufferPool>(m_max_output_width, m_max_output_height, DSP_IMAGE_FORMAT_NV12,
                                                                 m_output_pool_size, CMA, bytes_per_line);

        if (m_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
        {
            return ERROR;
        }

        return SUCCESS;
    }

    virtual void prepare_crops(BufferPtr input_buffer, std::vector<dsp_utils::crop_resize_dims_t> &crop_resize_dims) = 0;

    virtual void post_crop(BufferPtr input_buffer) {}

    virtual dsp_image_properties_t get_dsp_image_properties(BufferPtr input_buffer)
    {
        return *input_buffer->media_lib_buffers_list[MediaLibraryBufferType::Stream4K]->hailo_pix_buffer.get();
    }

    int process(BufferPtr data) override
    {
        std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

        std::vector<dsp_utils::crop_resize_dims_t> crop_resize_dims;
        prepare_crops(data, crop_resize_dims);

        for (auto &dims : crop_resize_dims)
        {
            std::chrono::steady_clock::time_point begin_crop = std::chrono::steady_clock::now();

            hailo_media_library_buffer* cropped_buffer = new hailo_media_library_buffer;
            if (m_buffer_pool->acquire_buffer(*cropped_buffer) != MEDIA_LIBRARY_SUCCESS)
            {
                std::cerr << "Failed to acquire buffer" << std::endl;
                return ERROR;
            }

            dsp_image_properties_t input_image_properties = get_dsp_image_properties(data);

            dsp_utils::perform_crop_and_resize(&input_image_properties, cropped_buffer->hailo_pix_buffer.get(), 
                                               dims, INTERPOLATION_TYPE_BILINEAR);

            BufferPtr cropped_buffer_shared = create_buffer_ptr_with_deleter({{MediaLibraryBufferType::Cropped, cropped_buffer}});
            std::shared_ptr<CroppedBufferMetadata> metadata = std::make_shared<CroppedBufferMetadata>(data, dims.crop_start_x,
                                                                                                      dims.crop_end_x, dims.crop_start_y, dims.crop_end_y);

            cropped_buffer_shared->append_metadata(metadata);

            send_to_subscribers(cropped_buffer_shared);

            std::chrono::steady_clock::time_point end_crop = std::chrono::steady_clock::now();

            if (PRINT_STATS)
            {
                std::cout << "----> Crop and resize time = " << std::chrono::duration_cast<std::chrono::milliseconds>(end_crop - begin_crop).count() << "[milliseconds]" << std::endl;
            }
        }

        post_crop(data);
        data->decrease_refcounts();

        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

        if (PRINT_STATS)
        {
            std::cout << "Crop and resize time = " << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "[milliseconds]" << std::endl;
        }

        return SUCCESS;
    }
};

class BBoxCropStage : public DspBaseCropStage
{
public:
    BBoxCropStage(std::string name, size_t queue_size, int output_pool_size, int max_output_width, int max_output_height) : 
        DspBaseCropStage(name, output_pool_size, max_output_width, max_output_height, queue_size) {}

    void prepare_crops(BufferPtr input_buffer, std::vector<dsp_utils::crop_resize_dims_t> &crop_resize_dims) override
    {        
        auto bbox_metadata = get_metadata<BBoxBufferMetadata>(input_buffer, BufferMetadataType::BBox);
        if (!bbox_metadata) {
            std::cerr << "Failed to get bbox metadata" << std::endl;
            return;
        }
        
        std::vector<BBox> bboxes = bbox_metadata->m_bboxes;

        for (auto &bbox : bboxes)
        {
            if (static_cast<int>(bbox.width) > m_max_output_width || static_cast<int>(bbox.height) > m_max_output_height)
            {
                std::cerr << "BBox is too big, skipping" << std::endl;
                continue;
            }

            dsp_utils::crop_resize_dims_t crop_resize_dim = {
                .perform_crop = 1,
                .crop_start_x = bbox.x,
                .crop_end_x = bbox.x + bbox.width,
                .crop_start_y = bbox.y,
                .crop_end_y = bbox.y + bbox.height,
                .destination_width = bbox.width,
                .destination_height = bbox.height,
            };

            crop_resize_dims.push_back(crop_resize_dim);
        }
    }
};