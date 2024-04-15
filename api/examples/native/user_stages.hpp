#pragma once

#include "buffer_utils.hpp"
#include "infra/stages.hpp"
#include "infra/dsp_stages.hpp"

#define MAX_OBJECT 256
#define AI_INPUT_FRAME_WIDTH 1920
#define AI_INPUT_FRAME_HEIGHT 1080
#define DETECTOR_WIDTH 640
#define DETECTOR_HEIGHT 640

// Hailort post process
#define NUM_OF_CLASS           9
#define MAX_PROPOSAL_PER_CLASS 100
#define CELL_SIZE              5
#define BATCH_SIZE 4

class DummyStage : public ProducableStage<BufferPtr, BufferPtr>
{
public:
    DummyStage(std::string name, size_t queue_size) : ProducableStage(name, queue_size, drop_buffer) { }

    int process(BufferPtr data) override
    {
        return SUCCESS;
    }
};

class TillingCropStage : public DspBaseCropStage
{
public: 
    TillingCropStage(std::string name, size_t queue_size, int output_pool_size) : 
        DspBaseCropStage(name, output_pool_size, DETECTOR_WIDTH, DETECTOR_HEIGHT, queue_size) { }

    dsp_image_properties_t get_dsp_image_properties(BufferPtr buffer)
    {
        // Getting the image_properties from full HD media lib buffer
        auto fhd_media_lib_buffer = buffer->media_lib_buffers_list[MediaLibraryBufferType::FullHD];
        if (!fhd_media_lib_buffer) {
            std::cerr << "Buffer does not have Full HD media library stream" << std::endl;
            return dsp_image_properties_t();
        }

        if (!fhd_media_lib_buffer->hailo_pix_buffer) {
            std::cerr << "Failed to get hailo pix buffer" << std::endl;
            return dsp_image_properties_t();
        }

        return *fhd_media_lib_buffer->hailo_pix_buffer.get();
    }

    void prepare_crops(BufferPtr input_buffer, std::vector<dsp_utils::crop_resize_dims_t> &crop_resize_dims) override
    {
        for (int i=0; i < BATCH_SIZE; i++)
        {
            size_t start_x = AI_INPUT_FRAME_WIDTH / 20 + (i * AI_INPUT_FRAME_WIDTH / 3.84);
            size_t end_x = start_x + AI_INPUT_FRAME_WIDTH / 2.4;
            size_t start_y = 0.1 * AI_INPUT_FRAME_HEIGHT;
            size_t end_y = 0.6 * AI_INPUT_FRAME_HEIGHT;

            if (i == BATCH_SIZE - 1)
            {
                start_x = 0;
                end_x = AI_INPUT_FRAME_WIDTH;
                start_y = 0;
                end_y = AI_INPUT_FRAME_HEIGHT;
            }

            dsp_utils::crop_resize_dims_t crop_resize_dim = {
                .perform_crop = 1,
                .crop_start_x = start_x,
                .crop_end_x = end_x,
                .crop_start_y = start_y,
                .crop_end_y = end_y,
                .destination_width = DETECTOR_WIDTH,
                .destination_height = DETECTOR_HEIGHT,
            };

            crop_resize_dims.push_back(crop_resize_dim);
        }
    }
};


class PostProcessStage : public ProducableStage<BufferPtr, BufferPtr>
{
private:
    float m_confidence_threshold;
    std::vector<std::vector<BBox>> m_bboxes;

public:
    PostProcessStage(std::string name, size_t queue_size, float confidence_threshold) : 
        ProducableStage(name, queue_size, drop_buffer), 
        m_confidence_threshold(confidence_threshold) { }

   void post_process(float* buffer, std::vector<BBox>& bboxes)
    {
        std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

        uint32_t a=0, idx=0, offset=0;    

        for (uint32_t r = 0; r < NUM_OF_CLASS+offset; r++)
        {
            a=0;
            if ((buffer[r]) >=1)
            {
                for (uint32_t t = r+1; t < uint32_t(r+uint32_t(buffer[r])*CELL_SIZE); t=t+CELL_SIZE)
                {
                    if ((buffer[t+4]) > m_confidence_threshold)
                    {
                        BBox bbox;
                        bbox.confidence = buffer[t+4];
                        bbox.detection_class = idx+1;
                        bbox.x = DETECTOR_HEIGHT * buffer[t+0];
                        bbox.y = DETECTOR_WIDTH * buffer[t+1];
                        bbox.width = DETECTOR_HEIGHT * buffer[t+2];
                        bbox.height = DETECTOR_WIDTH * buffer[t+3];

                        // Make values even
                        bbox.x += (bbox.x % 2);
                        bbox.y += (bbox.y % 2);
                        bbox.width += (bbox.width % 2);
                        bbox.height += (bbox.height % 2);

                        bboxes.push_back(bbox);
                    }

                    offset += CELL_SIZE;
                    a+=CELL_SIZE;
                }
                r += a;
            }
            idx++;
        }

        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

        if (PRINT_STATS)
        {
            std::cout << "Post process time = " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() << "[micro]" << std::endl;
        }
    }

    int inline clamp(int val, int min, int max)
    {
        return std::max(min, std::min(max, val));
    }

    void create_random_bbox(std::vector<BBox>& bboxes)
    {
        int num_of_bboxes = rand() % 20 + 1;

        for (int i = 0; i < num_of_bboxes; i++)
        {
            BBox bbox;
            bbox.confidence = (float)(rand() % 100) / 100;
            bbox.detection_class = rand() % 9;
            bbox.x = (float)(rand() % 500) * 2;
            bbox.y = (float)(rand() % 500) * 2;
            bbox.width = (float)(rand() % 320) * 2;
            bbox.height = (float)(rand() % 240) * 2;

            bbox.width = clamp(bbox.width, 40, 60);
            bbox.height = clamp(bbox.height, 100, 136);

            bboxes.push_back(bbox);
        }
    }

    int process(BufferPtr data) override
    {
        std::vector<BBox> bboxes;
        post_process(reinterpret_cast<float*>(data->media_lib_buffers_list[MediaLibraryBufferType::Hailort]->get_plane(0)), bboxes);
        bboxes.clear();

        create_random_bbox(bboxes);

        if (m_bboxes.size() != BATCH_SIZE - 1) {
            m_bboxes.push_back(bboxes);
            return SUCCESS;
        }

        for (auto &stored_bboxes : m_bboxes) {
            bboxes.insert(bboxes.end(), stored_bboxes.begin(), stored_bboxes.end());
        }

        m_bboxes.clear();

        CroppedBufferMetadataPtr cropped_buffer_metadata = get_metadata<CroppedBufferMetadata>(data, BufferMetadataType::Cropped);
        if (!cropped_buffer_metadata) {
            std::cerr << "Failed to get cropped buffer metadata" << std::endl;
            return ERROR;
        }

        data->remove_metadata(BufferMetadataType::Cropped);
        BufferPtr output_buffer = std::make_shared<Buffer>(false);
        output_buffer->copy_media_lib_buffers(cropped_buffer_metadata->parent_buffer);
        BBoxBufferMetadataPtr bbox_metadata = std::make_shared<BBoxBufferMetadata>(bboxes);
        output_buffer->append_metadata(bbox_metadata);
        output_buffer->copy_metadata(data);

        send_to_subscribers(output_buffer);

        return SUCCESS;
    }
};


class FrontendAggregatorStage : public ProducableBufferStage
{
private:
    SmartQueue<BufferPtr> m_4k_queue;
    SmartQueue<BufferPtr> m_fhd_queue;
public:
    FrontendAggregatorStage(std::string name, size_t queue_size) : 
        ProducableBufferStage(name, queue_size, false),
        m_4k_queue("4k_agg_queue", 5, drop_buffer, false),
        m_fhd_queue("fhd_agg_queue", 5, drop_buffer, false) { }
 
    int process(BufferPtr data) override
    {
        HailoMediaLibraryBufferPtr media_lib_buffer = data->media_lib_buffers_list[MediaLibraryBufferType::Unknown];
        int width = media_lib_buffer->hailo_pix_buffer->width;
        int height = media_lib_buffer->hailo_pix_buffer->height;

        if (width == 3840 && height == 2160)
        {
            m_4k_queue.push(data);
        }
        else if (width == 1920 && height == 1080)
        {
            m_fhd_queue.push(data);
        }
        else
        {
            std::cerr << "Invalid buffer size " << width << "x" << height << std::endl;
            return ERROR;
        }

        bool both_queues_has_buffer = m_4k_queue.size() > 0 && m_fhd_queue.size() > 0;

        if (!both_queues_has_buffer)
        {
            return SUCCESS;
        }

        BufferPtr output_buffer = std::make_shared<Buffer>(false);
        output_buffer->add_media_lib_buffer(MediaLibraryBufferType::Stream4K, std::move(m_4k_queue.pop()->media_lib_buffers_list[MediaLibraryBufferType::Unknown]));
        output_buffer->add_media_lib_buffer(MediaLibraryBufferType::FullHD, std::move(m_fhd_queue.pop()->media_lib_buffers_list[MediaLibraryBufferType::Unknown]));

        send_to_subscribers(output_buffer);

        return SUCCESS;
    }
};
