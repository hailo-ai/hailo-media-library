#pragma once

#include "stages.hpp"
#include "hailo/hailort.hpp"

class HailortAsyncStage : public ProducableStage<BufferPtr, BufferPtr>
{
private:
    MediaLibraryBufferPoolPtr m_output_buffer_pool;
    std::unique_ptr<hailort::VDevice> m_vdevice;
    std::shared_ptr<hailort::InferModel> m_infer_model;
    hailort::ConfiguredInferModel m_configured_infer_model;
    hailort::ConfiguredInferModel::Bindings m_bindings;
    int m_output_pool_size;
    std::string m_hef_path;
    std::string m_group_id;
    int m_batch_size;
    std::queue<BufferPtr> m_batch_queue;
    
public:
    HailortAsyncStage(std::string name, size_t queue_size, int output_pool_size, std::string hef_path, std::string group_id, int batch_size) :
        ProducableStage(name, queue_size, drop_buffer), m_output_pool_size(output_pool_size), m_hef_path(hef_path), m_group_id(group_id), m_batch_size(batch_size) { }

    int init() override
    {
        hailo_vdevice_params_t vdevice_params = {0};
        hailo_init_vdevice_params(&vdevice_params);
        vdevice_params.group_id = m_group_id.c_str();

        auto vdevice_exp = hailort::VDevice::create(vdevice_params);
        if (!vdevice_exp) {
            std::cerr << "Failed create vdevice, status = " << vdevice_exp.status() << std::endl;
            return vdevice_exp.status();
        }
        m_vdevice = vdevice_exp.release();

        auto infer_model_exp = m_vdevice->create_infer_model(m_hef_path.c_str());
        if (!infer_model_exp) {
            std::cerr << "Failed to create infer model, status = " << infer_model_exp.status() << std::endl;
            return infer_model_exp.status();
        }
        m_infer_model = infer_model_exp.release();
        m_infer_model->set_batch_size(m_batch_size);

        auto configured_infer_model_exp = m_infer_model->configure();
        if (!configured_infer_model_exp) {
            std::cerr << "Failed to create configured infer model, status = " << configured_infer_model_exp.status() << std::endl;
            return configured_infer_model_exp.status();
        }
        m_configured_infer_model = configured_infer_model_exp.release();
        m_configured_infer_model.set_scheduler_threshold(4);
        m_configured_infer_model.set_scheduler_timeout(std::chrono::milliseconds(100));

        auto bindings = m_configured_infer_model.create_bindings();
        if (!bindings) {
            std::cerr << "Failed to create infer bindings, status = " << bindings.status() << std::endl;
            return bindings.status();
        }
        m_bindings = bindings.release();

        size_t output_frame_size = m_infer_model->output()->get_frame_size();

        m_output_buffer_pool = std::make_shared<MediaLibraryBufferPool>(output_frame_size, 1, DSP_IMAGE_FORMAT_GRAY8,
                                                                        m_output_pool_size, CMA, output_frame_size);

        if (m_output_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
        {
            return ERROR;
        }

        return SUCCESS;
    }

    int set_pix_buf(const HailoMediaLibraryBufferPtr buffer)
    {
        auto y_plane_buffer = buffer->get_plane(0);
        uint32_t y_plane_size = buffer->get_plane_size(0);

        auto uv_plane_buffer = buffer->get_plane(1);
        uint32_t uv_plane_size = buffer->get_plane_size(1);

        hailo_pix_buffer_t pix_buffer{};
        pix_buffer.memory_type = HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR;
        pix_buffer.number_of_planes = 2;
        pix_buffer.planes[0].bytes_used = y_plane_size;
        pix_buffer.planes[0].plane_size = y_plane_size; 
        pix_buffer.planes[0].user_ptr = reinterpret_cast<void*>(y_plane_buffer);

        pix_buffer.planes[1].bytes_used = uv_plane_size;
        pix_buffer.planes[1].plane_size = uv_plane_size;
        pix_buffer.planes[1].user_ptr = reinterpret_cast<void*>(uv_plane_buffer);

        auto status = m_bindings.input()->set_pix_buffer(pix_buffer);
        if (HAILO_SUCCESS != status) {
            std::cerr << "Failed to set infer input buffer, status = " << status << std::endl;
            return status;
        }

        return SUCCESS;
    }

    int prepare_output(HailoMediaLibraryBufferPtr output_buffer)
    {
        if (m_output_buffer_pool->acquire_buffer(*output_buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            std::cerr << "Failed to acquire buffer" << std::endl;
            return -1;
        }

        size_t output_frame_size = m_infer_model->output()->get_frame_size();
        auto status = m_bindings.output()->set_buffer(hailort::MemoryView(output_buffer->get_plane(0), output_frame_size));
        if (HAILO_SUCCESS != status) {
            std::cerr << "Failed to set infer output buffer, status = " << status << std::endl;
            return status;
        }

        return SUCCESS;
    }

    int infer(HailoMediaLibraryBufferPtr input_buffer, BufferPtr output_buffer)
    {
        auto status = m_configured_infer_model.wait_for_async_ready(std::chrono::milliseconds(1000));
        if (HAILO_SUCCESS != status) {
            std::cerr << "Failed to wait for async ready, status = " << status << std::endl;
            return status;
        }

        auto job = m_configured_infer_model.run_async(m_bindings, [output_buffer, input_buffer, this](const hailort::AsyncInferCompletionInfo& completion_info) {
            if (completion_info.status != HAILO_SUCCESS) {
                std::cerr << "Failed to run async infer, status = " << completion_info.status << std::endl;
                return ERROR;
            }

            send_to_subscribers(output_buffer);

            return SUCCESS;
        });

        if (!job) {
            std::cerr << "Failed to start async infer job, status = " << job.status() << std::endl;
            return job.status();
        }

        job->detach();

        return SUCCESS;
    }

    int process(BufferPtr data)
    {
        m_batch_queue.push(data);

        if (static_cast<int>(m_batch_queue.size()) < m_batch_size)
        {
            return SUCCESS;
        }

        std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

        for (int i=0; i < m_batch_size; i++)
        {
            auto input_buffer = m_batch_queue.front();
            m_batch_queue.pop();
            
            if (set_pix_buf(input_buffer->media_lib_buffers_list[MediaLibraryBufferType::Cropped]) != SUCCESS)
            {
                return ERROR;
            }

            BufferPtr output_buffer_ptr = create_buffer_ptr_with_deleter({{MediaLibraryBufferType::Hailort, new hailo_media_library_buffer}});
            output_buffer_ptr->copy_metadata(input_buffer);

            if (prepare_output(output_buffer_ptr->media_lib_buffers_list[MediaLibraryBufferType::Hailort]) != SUCCESS)
            {
                return ERROR;
            }

            if (infer(input_buffer->media_lib_buffers_list[MediaLibraryBufferType::Cropped], output_buffer_ptr) != SUCCESS)
            {
                return ERROR;
            }
        }

        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

        if (PRINT_STATS)
        {
            std::cout << "AI time = " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() << "[micro]" << std::endl;
        }

        return SUCCESS;
    }
};