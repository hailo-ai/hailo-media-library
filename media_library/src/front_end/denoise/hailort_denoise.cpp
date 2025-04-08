#include "hailort_denoise.hpp"

#include "logger_macros.hpp"
#include "media_library_logger.hpp"
#include <chrono>

#define MODULE_NAME LoggerType::Denoise

HailortAsyncDenoise::HailortAsyncDenoise(const OnInferCb &on_infer_finish) : m_on_infer_finish(on_infer_finish)
{
}

HailortAsyncDenoise::~HailortAsyncDenoise()
{
    // Wait for last infer to finish
    auto status = m_last_infer_job.wait(std::chrono::milliseconds(1000));
    if (HAILO_SUCCESS != status)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to wait for infer to finish, status = {}", status);
    }
}

bool HailortAsyncDenoise::set_config(const denoise_config_t &denoise_config, const std::string &group_id,
                                     int scheduler_threshold, const std::chrono::milliseconds &scheduler_timeout,
                                     int batch_size)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Configuring hailoRT denoise");

    // Get network path from config
    std::string network_path = denoise_config.bayer ? denoise_config.bayer_network_config.network_path
                                                    : denoise_config.network_config.network_path;

    if (m_configured_devices.find(network_path) != m_configured_devices.end())
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Vdevice already created, using existing vdevice {}", network_path);
        m_current_vdevice_name = network_path;
        return true;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Vdevice not created, creating and configuring new vdevice {}", network_path);
    hailo_vdevice_params_t vdevice_params = {};
    hailo_init_vdevice_params(&vdevice_params);
    vdevice_params.group_id = group_id.c_str();
    auto vdevice_exp = hailort::VDevice::create(vdevice_params);
    if (!vdevice_exp)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed create vdevice, status = {}", vdevice_exp.status());
        return false;
    }

    auto infer_model_exp = vdevice_exp.value()->create_infer_model(network_path);
    if (!infer_model_exp)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create infer model, status = {}", infer_model_exp.status());
        return false;
    }
    infer_model_exp.value()->set_batch_size(batch_size);

    // input order
    if (denoise_config.bayer)
    {
        infer_model_exp.value()
            ->input(denoise_config.bayer_network_config.bayer_channel)
            ->set_format_order(HAILO_FORMAT_ORDER_NHWC);
        infer_model_exp.value()
            ->input(denoise_config.bayer_network_config.feedback_bayer_channel)
            ->set_format_order(HAILO_FORMAT_ORDER_NHWC);
    }
    else
    {
        infer_model_exp.value()
            ->input(denoise_config.network_config.y_channel)
            ->set_format_order(HAILO_FORMAT_ORDER_NHCW);
        infer_model_exp.value()
            ->input(denoise_config.network_config.uv_channel)
            ->set_format_order(HAILO_FORMAT_ORDER_NHWC);
        infer_model_exp.value()
            ->input(denoise_config.network_config.feedback_y_channel)
            ->set_format_order(HAILO_FORMAT_ORDER_NHCW);
        infer_model_exp.value()
            ->input(denoise_config.network_config.feedback_uv_channel)
            ->set_format_order(HAILO_FORMAT_ORDER_NHWC);
    }

    auto configured_infer_model_exp = infer_model_exp.value()->configure();
    if (!configured_infer_model_exp)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create configured infer model, status = {}",
                              configured_infer_model_exp.status());
        return false;
    }
    configured_infer_model_exp.value().set_scheduler_threshold(scheduler_threshold);
    configured_infer_model_exp.value().set_scheduler_timeout(scheduler_timeout);
    configured_infer_model_exp.value().set_scheduler_priority(HAILO_SCHEDULER_PRIORITY_MAX);

    auto bindings = configured_infer_model_exp.value().create_bindings();
    if (!bindings)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create infer bindings, status = {}", bindings.status());
        return false;
    }

    m_group_id = group_id;
    m_scheduler_threshold = scheduler_threshold;
    m_scheduler_timeout = scheduler_timeout;
    m_denoise_config = denoise_config;

    m_current_vdevice_name = network_path;
    m_vdevice = vdevice_exp.release();
    m_configured_devices[m_current_vdevice_name] = std::make_shared<hailort_configured_device_t>();
    m_configured_devices[m_current_vdevice_name]->infer_model = infer_model_exp.release();
    m_configured_devices[m_current_vdevice_name]->configured_infer_model = configured_infer_model_exp.release();
    m_configured_devices[m_current_vdevice_name]->bindings = bindings.release();

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
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to map buffer to hailort, status = {}", status);
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::unmap_buffer_to_hailort(int fd, size_t size)
{
    auto status = m_vdevice->dma_unmap_dmabuf(fd, size, hailo_dma_buffer_direction_t::HAILO_DMA_BUFFER_DIRECTION_BOTH);
    if (HAILO_SUCCESS != status)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to unmap buffer to hailort, status = {}", status);
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::set_input_buffer(int fd, const std::string &tensor_name)
{
    auto input_frame_size =
        m_configured_devices[m_current_vdevice_name]->infer_model->input(tensor_name)->get_frame_size();
    hailo_dma_buffer_t dma_buffer = {fd, input_frame_size};
    auto status = m_configured_devices[m_current_vdevice_name]->bindings.input(tensor_name)->set_dma_buffer(dma_buffer);
    if (HAILO_SUCCESS != status)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set infer input buffer {}, status = {}", tensor_name, status);
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::set_post_isp_input_buffers(HailoMediaLibraryBufferPtr input_buffer,
                                                     HailoMediaLibraryBufferPtr loopback_buffer)
{
    int fd;
    fd = input_buffer->get_plane_fd(0);
    if (fd < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get file descriptor of input buffer plane 0, fd={}", fd);
        return false;
    }
    if (!set_input_buffer(fd, m_denoise_config.network_config.y_channel))
    {
        return false;
    }

    fd = input_buffer->get_plane_fd(1);
    if (fd < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get file descriptor of input buffer plane 1, fd={}", fd);
        return false;
    }
    if (!set_input_buffer(fd, m_denoise_config.network_config.uv_channel))
    {
        return false;
    }

    fd = loopback_buffer->get_plane_fd(0);
    if (fd < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get file descriptor of loopback buffer plane 0, fd={}", fd);
        return false;
    }
    if (!set_input_buffer(fd, m_denoise_config.network_config.feedback_y_channel))
    {
        return false;
    }
    fd = loopback_buffer->get_plane_fd(1);
    if (fd < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get file descriptor of loopback buffer plane 1, fd={}", fd);
        return false;
    }
    if (!set_input_buffer(fd, m_denoise_config.network_config.feedback_uv_channel))
    {
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::set_pre_isp_input_buffers(HailoMediaLibraryBufferPtr input_buffer,
                                                    HailoMediaLibraryBufferPtr loopback_buffer)
{
    int fd;
    fd = input_buffer->get_plane_fd(0);
    if (fd < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get file descriptor of input buffer plane 0, fd={}", fd);
        return false;
    }
    if (!set_input_buffer(fd, m_denoise_config.bayer_network_config.bayer_channel))
    {
        return false;
    }

    fd = loopback_buffer->get_plane_fd(0);
    if (fd < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get file descriptor of loopback input buffer plane 0, fd={}", fd);
        return false;
    }
    if (!set_input_buffer(fd, m_denoise_config.bayer_network_config.feedback_bayer_channel))
    {
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::set_input_buffers(HailoMediaLibraryBufferPtr input_buffer,
                                            HailoMediaLibraryBufferPtr loopback_buffer)
{
    if (m_denoise_config.bayer)
        return set_pre_isp_input_buffers(input_buffer, loopback_buffer);

    return set_post_isp_input_buffers(input_buffer, loopback_buffer);
}

bool HailortAsyncDenoise::set_output_buffer(int fd, const std::string &tensor_name)
{
    auto output_frame_size =
        m_configured_devices[m_current_vdevice_name]->infer_model->output(tensor_name)->get_frame_size();
    hailo_dma_buffer_t dma_buffer = {fd, output_frame_size};
    auto status =
        m_configured_devices[m_current_vdevice_name]->bindings.output(tensor_name)->set_dma_buffer(dma_buffer);
    if (HAILO_SUCCESS != status)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set infer output buffer {}, status = {}", tensor_name, status);
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::set_post_isp_output_buffers(HailoMediaLibraryBufferPtr output_buffer)
{
    int fd;
    fd = output_buffer->get_plane_fd(0);
    if (fd < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get file descriptor of output buffer plane 0, fd={}", fd);
        return false;
    }
    if (!set_output_buffer(fd, m_denoise_config.network_config.output_y_channel))
    {
        return false;
    }

    fd = output_buffer->get_plane_fd(1);
    if (fd < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get file descriptor of output buffer plane 1, fd={}", fd);
        return false;
    }
    if (!set_output_buffer(fd, m_denoise_config.network_config.output_uv_channel))
    {
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::set_pre_isp_output_buffers(HailoMediaLibraryBufferPtr output_buffer)
{
    int fd;
    fd = output_buffer->get_plane_fd(0);
    if (fd < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get file descriptor of output buffer plane 0, fd={}", fd);
        return false;
    }
    if (!set_output_buffer(fd, m_denoise_config.bayer_network_config.output_bayer_channel))
    {
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::set_output_buffers(HailoMediaLibraryBufferPtr output_buffer)
{
    if (m_denoise_config.bayer)
        return set_pre_isp_output_buffers(output_buffer);

    return set_post_isp_output_buffers(output_buffer);
}

bool HailortAsyncDenoise::infer(HailoMediaLibraryBufferPtr output_buffer)
{
    auto status = m_configured_devices[m_current_vdevice_name]->configured_infer_model.wait_for_async_ready(
        std::chrono::milliseconds(10000));
    if (HAILO_SUCCESS != status)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to wait for async ready, status = {}", status);
        return false;
    }

    auto job = m_configured_devices[m_current_vdevice_name]->configured_infer_model.run_async(
        m_configured_devices[m_current_vdevice_name]->bindings,
        [output_buffer, this](const hailort::AsyncInferCompletionInfo &completion_info) {
            if (completion_info.status != HAILO_SUCCESS)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "[Denoise] Failed to run async infer, status = {}",
                                      completion_info.status);
            }
            m_on_infer_finish(output_buffer);
        });

    if (!job)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start async infer job, status = {}", job.status());
        return false;
    }

    job->detach();
    m_last_infer_job = job.release();

    return true;
}
