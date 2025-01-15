#include "hailort_denoise.hpp"

#include "logger_macros.hpp"
#include "media_library_logger.hpp"
#include <chrono>

HailortAsyncDenoise::HailortAsyncDenoise(const OnInferCb &on_infer_finish) : m_on_infer_finish(on_infer_finish)
{
}

HailortAsyncDenoise::~HailortAsyncDenoise()
{
    // Wait for last infer to finish
    auto status = m_last_infer_job.wait(std::chrono::milliseconds(1000));
    if (HAILO_SUCCESS != status)
    {
        LOGGER__ERROR("Failed to wait for infer to finish, status = {}", status);
    }
}

bool HailortAsyncDenoise::set_config(const feedback_network_config_t &network_config, const std::string &group_id,
                                     int scheduler_threshold, const std::chrono::milliseconds &scheduler_timeout,
                                     int batch_size)
{
    LOGGER__INFO("Configuring hailoRT denoise");

    hailo_vdevice_params_t vdevice_params = {};
    hailo_init_vdevice_params(&vdevice_params);
    vdevice_params.group_id = group_id.c_str();

    auto vdevice_exp = hailort::VDevice::create(vdevice_params);
    if (!vdevice_exp)
    {
        LOGGER__ERROR("Failed create vdevice, status = {}", vdevice_exp.status());
        return false;
    }

    auto infer_model_exp = vdevice_exp.value()->create_infer_model(network_config.network_path.c_str());
    if (!infer_model_exp)
    {
        LOGGER__ERROR("Failed to create infer model, status = {}", infer_model_exp.status());
        return false;
    }
    infer_model_exp.value()->set_batch_size(batch_size);

    // input order
    infer_model_exp.value()->input(network_config.y_channel)->set_format_order(HAILO_FORMAT_ORDER_NHCW);
    infer_model_exp.value()->input(network_config.uv_channel)->set_format_order(HAILO_FORMAT_ORDER_NHWC);
    infer_model_exp.value()->input(network_config.feedback_y_channel)->set_format_order(HAILO_FORMAT_ORDER_NHCW);
    infer_model_exp.value()->input(network_config.feedback_uv_channel)->set_format_order(HAILO_FORMAT_ORDER_NHWC);

    auto configured_infer_model_exp = infer_model_exp.value()->configure();
    if (!configured_infer_model_exp)
    {
        LOGGER__ERROR("Failed to create configured infer model, status = {}", configured_infer_model_exp.status());
        return false;
    }
    configured_infer_model_exp.value().set_scheduler_threshold(scheduler_threshold);
    configured_infer_model_exp.value().set_scheduler_timeout(scheduler_timeout);

    auto bindings = configured_infer_model_exp.value().create_bindings();
    if (!bindings)
    {
        LOGGER__ERROR("Failed to create infer bindings, status = {}", bindings.status());
        return false;
    }

    m_group_id = group_id;
    m_scheduler_threshold = scheduler_threshold;
    m_scheduler_timeout = scheduler_timeout;
    m_network_config = network_config;

    m_vdevice = vdevice_exp.release();
    m_infer_model = infer_model_exp.release();
    m_configured_infer_model = configured_infer_model_exp.release();

    m_bindings = bindings.release();

    return true;
}

bool HailortAsyncDenoise::process(HailoMediaLibraryBufferPtr input_buffer,
                                  HailoMediaLibraryBufferPtr loopback_input_buffer,
                                  HailoMediaLibraryBufferPtr output_buffer)
{
    if (!set_input_buffers(input_buffer, loopback_input_buffer))
    {
        return false;
    }

    if (!set_output_buffers(output_buffer))
    {
        return false;
    }

    if (!infer(output_buffer))
    {
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::map_buffer_to_hailort(int fd, size_t size)
{
    auto status = m_vdevice->dma_map_dmabuf(fd, size, hailo_dma_buffer_direction_t::HAILO_DMA_BUFFER_DIRECTION_BOTH);
    if (HAILO_SUCCESS != status)
    {
        LOGGER__ERROR("Failed to map buffer to hailort, status = {}", status);
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::unmap_buffer_to_hailort(int fd, size_t size)
{
    auto status = m_vdevice->dma_unmap_dmabuf(fd, size, hailo_dma_buffer_direction_t::HAILO_DMA_BUFFER_DIRECTION_BOTH);
    if (HAILO_SUCCESS != status)
    {
        LOGGER__ERROR("Failed to unmap buffer to hailort, status = {}", status);
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::set_input_buffer(int fd, const std::string &tensor_name)
{
    auto input_frame_size = m_infer_model->input(tensor_name)->get_frame_size();
    hailo_dma_buffer_t dma_buffer = {fd, input_frame_size};
    auto status = m_bindings.input(tensor_name)->set_dma_buffer(dma_buffer);
    if (HAILO_SUCCESS != status)
    {
        LOGGER__ERROR("Failed to set infer input buffer, status = {}", status);
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::set_input_buffers(HailoMediaLibraryBufferPtr input_buffer,
                                            HailoMediaLibraryBufferPtr loopback_buffer)
{
    int fd;
    fd = input_buffer->get_plane_fd(0);
    if (fd < 0)
    {
        LOGGER__ERROR("Failed to get file descriptor of input buffer plane 0, fd={}", fd);
        return false;
    }
    if (!set_input_buffer(fd, m_network_config.y_channel))
    {
        return false;
    }

    fd = input_buffer->get_plane_fd(1);
    if (fd < 0)
    {
        LOGGER__ERROR("Failed to get file descriptor of input buffer plane 1, fd={}", fd);
        return false;
    }
    if (!set_input_buffer(fd, m_network_config.uv_channel))
    {
        return false;
    }

    fd = loopback_buffer->get_plane_fd(0);
    if (fd < 0)
    {
        LOGGER__ERROR("Failed to get file descriptor of loopback buffer plane 0, fd={}", fd);
        return false;
    }
    if (!set_input_buffer(fd, m_network_config.feedback_y_channel))
    {
        return false;
    }
    fd = loopback_buffer->get_plane_fd(1);
    if (fd < 0)
    {
        LOGGER__ERROR("Failed to get file descriptor of loopback buffer plane 1, fd={}", fd);
        return false;
    }
    if (!set_input_buffer(fd, m_network_config.feedback_uv_channel))
    {
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::set_output_buffer(int fd, const std::string &tensor_name)
{
    auto output_frame_size = m_infer_model->output(tensor_name)->get_frame_size();
    hailo_dma_buffer_t dma_buffer = {fd, output_frame_size};
    auto status = m_bindings.output(tensor_name)->set_dma_buffer(dma_buffer);
    if (HAILO_SUCCESS != status)
    {
        LOGGER__ERROR("Failed to set infer input buffer, status = {}", status);
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::set_output_buffers(HailoMediaLibraryBufferPtr output_buffer)
{
    int fd;
    fd = output_buffer->get_plane_fd(0);
    if (fd < 0)
    {
        LOGGER__ERROR("Failed to get file descriptor of output buffer plane 0, fd={}", fd);
        return false;
    }
    if (!set_output_buffer(fd, m_network_config.output_y_channel))
    {
        return false;
    }
    fd = output_buffer->get_plane_fd(1);
    if (fd < 0)
    {
        LOGGER__ERROR("Failed to get file descriptor of output buffer plane 1, fd={}", fd);
        return false;
    }

    if (!set_output_buffer(fd, m_network_config.output_uv_channel))
    {
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::infer(HailoMediaLibraryBufferPtr output_buffer)
{
    auto status = m_configured_infer_model.wait_for_async_ready(std::chrono::milliseconds(10000));
    if (HAILO_SUCCESS != status)
    {
        LOGGER__ERROR("Failed to wait for async ready, status = {}", status);
        return false;
    }

    auto job = m_configured_infer_model.run_async(
        m_bindings, [output_buffer, this](const hailort::AsyncInferCompletionInfo &completion_info) {
            if (completion_info.status != HAILO_SUCCESS)
            {
                LOGGER__ERROR("[Denoise] Failed to run async infer, status = {}", completion_info.status);
            }
            m_on_infer_finish(output_buffer);
        });

    if (!job)
    {
        LOGGER__ERROR("Failed to start async infer job, status = {}", job.status());
        return false;
    }

    job->detach();
    m_last_infer_job = job.release();

    return true;
}
