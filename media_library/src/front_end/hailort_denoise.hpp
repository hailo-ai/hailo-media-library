#pragma once

#include "hailo/hailort.hpp"
#include "media_library_utils.hpp"
#include "media_library_logger.hpp"
#include "buffer_pool.hpp"

#define ERROR -1
#define SUCCESS 0

class HailortAsyncDenoise
{
private:
    std::function<void(HailoMediaLibraryBufferPtr output_buffer)> m_on_infer_finish;
    std::string m_group_id;
    int m_scheduler_threshold;
    int m_scheduler_timeout_in_ms;
    feedback_network_config_t m_network_config;
    std::shared_ptr<hailort::AsyncInferJob> m_last_infer_job;

    std::unique_ptr<hailort::VDevice> m_vdevice;
    std::shared_ptr<hailort::InferModel> m_infer_model;
    hailort::ConfiguredInferModel m_configured_infer_model;
    hailort::ConfiguredInferModel::Bindings m_bindings;

public:
    HailortAsyncDenoise(std::function<void(HailoMediaLibraryBufferPtr output_buffer)> on_infer_finish) : m_on_infer_finish(on_infer_finish)
    {
        m_last_infer_job = nullptr;
    }

    ~HailortAsyncDenoise()
    {
        // Wait for last infer to finish
        if (m_last_infer_job)
{
            auto status = m_last_infer_job->wait(std::chrono::milliseconds(1000));
            if (HAILO_SUCCESS != status)
            {
                LOGGER__ERROR("Failed to wait for infer to finish, status = {}", status);
            }
        }
    }

    int init(feedback_network_config_t network_config, std::string group_id, int scheduler_threshold, int scheduler_timeout_in_ms, int batch_size)
    {
        m_group_id = group_id;
        m_scheduler_threshold = scheduler_threshold;
        m_scheduler_timeout_in_ms = scheduler_timeout_in_ms;
        m_network_config = network_config;

        hailo_vdevice_params_t vdevice_params = {};
        hailo_init_vdevice_params(&vdevice_params);
        vdevice_params.group_id = m_group_id.c_str();

        auto vdevice_exp = hailort::VDevice::create(vdevice_params);
        if (!vdevice_exp)
        {
            std::cerr << "Failed create vdevice, status = " << vdevice_exp.status() << std::endl;
            return vdevice_exp.status();
        }
        m_vdevice = vdevice_exp.release();

        auto infer_model_exp = m_vdevice->create_infer_model(m_network_config.network_path.c_str());
        if (!infer_model_exp)
        {
            std::cerr << "Failed to create infer model, status = " << infer_model_exp.status() << std::endl;
            return infer_model_exp.status();
        }
        m_infer_model = infer_model_exp.release();
        m_infer_model->set_batch_size(batch_size);

        // input order
        m_infer_model->input(m_network_config.y_channel)->set_format_order(HAILO_FORMAT_ORDER_NHCW);
        m_infer_model->input(m_network_config.uv_channel)->set_format_order(HAILO_FORMAT_ORDER_NHWC);
        m_infer_model->input(m_network_config.feedback_y_channel)->set_format_order(HAILO_FORMAT_ORDER_NHCW);
        m_infer_model->input(m_network_config.feedback_uv_channel)->set_format_order(HAILO_FORMAT_ORDER_NHWC);
        // output order
        m_infer_model->output(m_network_config.output_y_channel)->set_format_order(HAILO_FORMAT_ORDER_NHCW);
        m_infer_model->output(m_network_config.output_uv_channel)->set_format_order(HAILO_FORMAT_ORDER_FCR);

        auto configured_infer_model_exp = m_infer_model->configure();
        if (!configured_infer_model_exp)
        {
            std::cerr << "Failed to create configured infer model, status = " << configured_infer_model_exp.status() << std::endl;
            return configured_infer_model_exp.status();
        }
        m_configured_infer_model = configured_infer_model_exp.release();
        m_configured_infer_model.set_scheduler_threshold(m_scheduler_threshold);
        m_configured_infer_model.set_scheduler_timeout(std::chrono::milliseconds(m_scheduler_timeout_in_ms));

        auto bindings = m_configured_infer_model.create_bindings();
        if (!bindings)
        {
            std::cerr << "Failed to create infer bindings, status = " << bindings.status() << std::endl;
            return bindings.status();
        }
        m_bindings = bindings.release();

        return SUCCESS;
    }

    int process(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr loopback_input_buffer, HailoMediaLibraryBufferPtr output_buffer)
    {
        if (set_input_buffers(input_buffer, loopback_input_buffer) != SUCCESS)
        {
            return ERROR;
        }

        if (set_output_buffers(output_buffer) != SUCCESS)
        {
            return ERROR;
        }

        if (infer(output_buffer) != SUCCESS)
        {
            return ERROR;
        }

        return SUCCESS;
    }

private:
    int set_input_buffer(void *buffer_p, std::string tensor_name)
    {
        auto input_frame_size = m_infer_model->input(tensor_name)->get_frame_size();
        auto status = m_bindings.input(tensor_name)->set_buffer(hailort::MemoryView(buffer_p, input_frame_size));
        if (HAILO_SUCCESS != status)
        {
            std::cerr << "Failed to set infer input buffer, status = " << status << std::endl;
            return status;
        }

        return SUCCESS;
    }

    int set_input_buffer(int fd, std::string tensor_name)
    {
        auto input_frame_size = m_infer_model->input(tensor_name)->get_frame_size();
        hailo_dma_buffer_t dma_buffer = {fd, input_frame_size};
        auto status = m_bindings.input(tensor_name)->set_dma_buffer(dma_buffer);
        if (HAILO_SUCCESS != status)
        {
            std::cerr << "Failed to set infer input buffer, status = " << status << std::endl;
            return status;
        }

        return SUCCESS;
    }
    

    int set_input_buffers(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr loopback_buffer)
    {
        int fd;
        fd = input_buffer->get_fd(0);
        if(fd < 0)
        {
            std::cerr << "Failed to get file descriptor of input buffer plane 0"<< std::endl;
            return ERROR;
        }
        if (set_input_buffer(fd, m_network_config.y_channel) != SUCCESS)
        {
            return ERROR;
        }

        fd = input_buffer->get_fd(1);
        if(fd < 0)
        {
            std::cerr << "Failed to get file descriptor of input buffer plane 1" << fd << std::endl;
            return ERROR;
        }
        if (set_input_buffer(fd, m_network_config.uv_channel) != SUCCESS)
        {
            return ERROR;
        }

        fd = loopback_buffer->get_fd(0);
        if(fd < 0)
        {
            std::cerr << "Failed to get file descriptor of loopback buffer plane 0" << fd << std::endl;
            return ERROR;
        }
        if (set_input_buffer(fd, m_network_config.feedback_y_channel) != SUCCESS)
        {
            return ERROR;
        }
        fd = loopback_buffer->get_fd(1);
        if(fd < 0)
        {
            std::cerr << "Failed to get file descriptor of loopback buffer plane 1" << fd << std::endl;
            return ERROR;
        }
        if (set_input_buffer(fd, m_network_config.feedback_uv_channel) != SUCCESS)
        {
            return ERROR;
        }

        return SUCCESS;
    }

    int set_output_buffer(void *buffer_p, std::string tensor_name)
    {
        auto output_frame_size = m_infer_model->output(tensor_name)->get_frame_size();
        auto status = m_bindings.output(tensor_name)->set_buffer(hailort::MemoryView(buffer_p, output_frame_size));
        if (HAILO_SUCCESS != status)
        {
            std::cerr << "Failed to set infer input buffer, status = " << status << std::endl;
            return status;
        }

        return SUCCESS;
    }

    int set_output_buffer(int fd, std::string tensor_name)
    {
        auto output_frame_size = m_infer_model->output(tensor_name)->get_frame_size();
        hailo_dma_buffer_t dma_buffer = {fd, output_frame_size};
        auto status = m_bindings.output(tensor_name)->set_dma_buffer(dma_buffer);
        if (HAILO_SUCCESS != status)
        {
            std::cerr << "Failed to set infer input buffer, status = " << status << std::endl;
            return status;
        }

        return SUCCESS;
    }

    int set_output_buffers(HailoMediaLibraryBufferPtr output_buffer)
    {
        int fd;
        fd = output_buffer->get_fd(0);
        if(fd < 0)
        {
            std::cerr << "Failed to get file descriptor of output buffer plane 0" << fd << std::endl;
            return ERROR;
        }
        if (set_output_buffer(fd, m_network_config.output_y_channel) != SUCCESS)
        {
            return ERROR;
        }
        fd = output_buffer->get_fd(1);
        if(fd < 0)
        {
            std::cerr << "Failed to get file descriptor of output buffer plane 1" << fd << std::endl;
            return ERROR;
        }

        if (set_output_buffer(fd, m_network_config.output_uv_channel) != SUCCESS)
        {
            return ERROR;
        }

        return SUCCESS;
    }

    int infer(HailoMediaLibraryBufferPtr output_buffer)
    {
        auto status = m_configured_infer_model.wait_for_async_ready(std::chrono::milliseconds(10000));
        if (HAILO_SUCCESS != status)
        {
            std::cerr << "Failed to wait for async ready, status = " << status << std::endl;
            return status;
        }

        auto job = m_configured_infer_model.run_async(m_bindings, [output_buffer, this](const hailort::AsyncInferCompletionInfo &completion_info)
                                                      {
            if (completion_info.status != HAILO_SUCCESS) {
                std::cerr << "[Denoise] Failed to run async infer, status = " << completion_info.status << std::endl;
                return ERROR;
            }

            m_on_infer_finish(output_buffer);

            return SUCCESS; });

        if (!job)
        {
            std::cerr << "Failed to start async infer job, status = " << job.status() << std::endl;
            return job.status();
        }

        job->detach();
        m_last_infer_job = std::make_shared<hailort::AsyncInferJob>(job.release());

        return SUCCESS;
    }
};
using HailortAsyncDenoisePtr = std::shared_ptr<HailortAsyncDenoise>;
