#include "hailort_denoise.hpp"

#include "logger_macros.hpp"
#include "media_library_logger.hpp"
#include <chrono>
#include "hailo_media_library_perfetto.hpp"

#define MODULE_NAME LoggerType::Denoise

HailoMediaLibraryBufferPtr get_output_buffer(NetworkInferenceBindingsPtr bindings, int index)
{
    return bindings->outputs[index].buffer;
}

void bind_output_buffer(NetworkInferenceBindingsPtr bindings, int index, HailoMediaLibraryBufferPtr buffer)
{
    bindings->outputs[index].buffer = buffer;
}

void bind_input_buffer(NetworkInferenceBindingsPtr bindings, int index, HailoMediaLibraryBufferPtr buffer)
{
    bindings->inputs[index].buffer = buffer;
}

void bind_gain_input_buffer(NetworkInferenceBindingsPtr bindings, int index, HailoMediaLibraryBufferPtr buffer)
{
    bindings->gain_inputs[index].buffer = buffer;
}

void bind_skip_input_buffer(NetworkInferenceBindingsPtr bindings, int index, HailoMediaLibraryBufferPtr buffer)
{
    bindings->skip_inputs[index].buffer = buffer;
}

static inline bool is_using_dgain_and_bls_pre_isp(const denoise_config_t &denoise_configs)
{
    return !denoise_configs.bayer_network_config.dgain_channel.empty() &&
           !denoise_configs.bayer_network_config.bls_channel.empty();
}

/*
    HailortAsyncDenoise
*/

HailortAsyncDenoise::HailortAsyncDenoise(const OnInferCb &on_infer_finish) : m_on_infer_finish(on_infer_finish)
{
}

HailortAsyncDenoise::~HailortAsyncDenoise()
{
    wait_for_all_jobs_to_finish();
}

void HailortAsyncDenoise::wait_for_all_jobs_to_finish()
{
    // Wait for last infer to finish
    auto status = m_last_infer_job.wait(WAIT_FOR_LAST_INFER_TIMEOUT);
    if (HAILO_SUCCESS != status)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to wait for infer to finish, status = {}", status);
    }
    m_last_infer_job = hailort::AsyncInferJob();
}

bool HailortAsyncDenoise::has_pending_jobs() const
{
    // Hailort wait api output error code
    if (m_last_result_infer_output_buffer_timestamp == m_last_inserted_infer_output_buffer_timestamp)
    {
        return false;
    }
    return true;
}

bool HailortAsyncDenoise::set_config(const denoise_config_t &denoise_config, const std::string &group_id,
                                     int scheduler_threshold, const std::chrono::milliseconds &scheduler_timeout,
                                     int batch_size)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Configuring hailoRT denoise");

    // Get network path from config
    std::string network_path = get_network_path(denoise_config);

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
    set_infer_layers(infer_model_exp.value(), create_bindings(denoise_config, nullptr, nullptr));

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

bool HailortAsyncDenoise::set_input_buffer(HailoMediaLibraryBufferPtr input_buffer, int plane_id,
                                           const std::string &buffer_name, const std::string &tensor_name)
{
    const int fd = input_buffer->get_plane_fd(plane_id);
    if (fd < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get file descriptor of {} plane {}, fd={}", buffer_name, plane_id,
                              fd);
        return false;
    }
    if (!set_input_buffer(fd, tensor_name))
    {
        return false;
    }
    return true;
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

bool HailortAsyncDenoise::set_output_buffer(HailoMediaLibraryBufferPtr output_buffer, int plane_id,
                                            const std::string &buffer_name, const std::string &tensor_name)
{
    const int fd = output_buffer->get_plane_fd(plane_id);
    if (fd < 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get file descriptor of {} plane {}, fd={}", buffer_name, plane_id,
                              fd);
        return false;
    }

    if (!set_output_buffer(fd, tensor_name))
    {
        return false;
    }

    return true;
}

bool HailortAsyncDenoise::infer(NetworkInferenceBindingsPtr bindings)
{
    auto status = m_configured_devices[m_current_vdevice_name]->configured_infer_model.wait_for_async_ready(
        std::chrono::milliseconds(10000));
    if (HAILO_SUCCESS != status)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to wait for async ready, status = {}", status);
        return false;
    }

    auto output_buffer = get_output_buffer(bindings, get_denoised_output_index());
    HAILO_MEDIA_LIBRARY_TRACE_ASYNC_EVENT_BEGIN("Inference", output_buffer->isp_timestamp_ns, DENOISE_TRACK);

    auto job = m_configured_devices[m_current_vdevice_name]->configured_infer_model.run_async(
        m_configured_devices[m_current_vdevice_name]->bindings,
        [bindings = std::move(bindings), this](const hailort::AsyncInferCompletionInfo &completion_info) {
            if (completion_info.status != HAILO_SUCCESS)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "[Denoise] Failed to run async infer, status = {}",
                                      completion_info.status);
            }
            auto output_buffer = get_output_buffer(bindings, get_denoised_output_index());
            m_on_infer_finish(std::move(bindings));
            m_last_result_infer_output_buffer_timestamp.store(output_buffer->isp_timestamp_ns,
                                                              std::memory_order_seq_cst);
            HAILO_MEDIA_LIBRARY_TRACE_ASYNC_EVENT_END("Inference", output_buffer->isp_timestamp_ns, DENOISE_TRACK);
        });

    if (!job)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start async infer job, status = {}", job.status());
        return false;
    }

    job->detach();
    m_last_infer_job = job.release();
    m_last_inserted_infer_output_buffer_timestamp = output_buffer->isp_timestamp_ns;

    return true;
}

void HailortAsyncDenoise::set_infer_layers(std::shared_ptr<hailort::InferModel> infer_model,
                                           NetworkInferenceBindingsPtr bindings)
{
    for (const auto &binding : bindings->inputs)
    {
        infer_model->input(binding.tensor_name)->set_format_order(binding.format_order);
    }

    for (const auto &binding : bindings->outputs)
    {
        infer_model->output(binding.tensor_name)->set_format_order(binding.format_order);
    }

    for (const auto &binding : bindings->gain_inputs)
    {
        infer_model->input(binding.tensor_name)->set_format_order(binding.format_order);
    }

    for (const auto &binding : bindings->skip_inputs)
    {
        infer_model->input(binding.tensor_name)->set_format_order(binding.format_order);
    }
}

bool HailortAsyncDenoise::set_input_buffers(const TensorBindings &inputs, const TensorBindings &gain_inputs,
                                            const TensorBindings &skip_inputs)
{
    for (const auto &binding : inputs)
    {
        if (!set_input_buffer(binding.buffer, binding.plane_id, binding.buffer_name, binding.tensor_name))
        {
            return false;
        }
    }

    for (const auto &binding : gain_inputs)
    {
        if (!set_input_buffer(binding.buffer, binding.plane_id, binding.buffer_name, binding.tensor_name))
        {
            return false;
        }
    }

    for (const auto &binding : skip_inputs)
    {
        if (!set_input_buffer(binding.buffer, binding.plane_id, binding.buffer_name, binding.tensor_name))
        {
            return false;
        }
    }

    return true;
}

bool HailortAsyncDenoise::set_output_buffers(const TensorBindings &outputs)
{
    for (const auto &binding : outputs)
    {
        if (!set_output_buffer(binding.buffer, binding.plane_id, binding.buffer_name, binding.tensor_name))
        {
            return false;
        }
    }

    return true;
}

bool HailortAsyncDenoise::process(NetworkInferenceBindingsPtr bindings)
{
    if (!set_input_buffers(bindings->inputs, bindings->gain_inputs, bindings->skip_inputs))
    {
        return false;
    }

    if (!set_output_buffers(bindings->outputs))
    {
        return false;
    }

    if (!infer(std::move(bindings)))
    {
        return false;
    }

    return true;
}

/*
    HailortAsyncDenoisePostISP
*/

std::string HailortAsyncDenoisePostISP::get_network_path(const denoise_config_t &denoise_config) const
{
    return denoise_config.network_config.network_path;
}

media_library_return HailortAsyncDenoisePostISP::bind_loopback_buffers(NetworkInferenceBindingsPtr bindings,
                                                                       const TensorBindings &loopback_buffers) const
{
    if (loopback_buffers[OutputIndex::OUTPUT_Y_CHANNEL].buffer == nullptr)
    {
        return media_library_return::MEDIA_LIBRARY_BUFFER_NOT_FOUND;
    }
    bindings->inputs[InputIndex::LOOPBACK_Y_CHANNEL].buffer = loopback_buffers[OutputIndex::OUTPUT_Y_CHANNEL].buffer;

    if (loopback_buffers[OutputIndex::OUTPUT_UV_CHANNEL].buffer == nullptr)
    {
        return media_library_return::MEDIA_LIBRARY_BUFFER_NOT_FOUND;
    }
    bindings->inputs[InputIndex::LOOPBACK_UV_CHANNEL].buffer = loopback_buffers[OutputIndex::OUTPUT_UV_CHANNEL].buffer;

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

NetworkInferenceBindingsPtr HailortAsyncDenoisePostISP::create_bindings(const denoise_config_t &denoise_config,
                                                                        HailoMediaLibraryBufferPtr input_buffer,
                                                                        HailoMediaLibraryBufferPtr output_buffer) const
{
    NetworkInferenceBindingsPtr bindings = std::make_shared<NetworkInferenceBindings>();
    bindings->inputs.resize(InputIndex::INPUT_SIZE);
    bindings->outputs.resize(OutputIndex::OUTPUT_SIZE);
    bindings->inputs[InputIndex::Y_CHANNEL] = TensorBinding{.buffer = input_buffer,
                                                            .plane_id = PlaneId::ZERO,
                                                            .buffer_name = "input buffer",
                                                            .tensor_name = denoise_config.network_config.y_channel,
                                                            .format_order = HAILO_FORMAT_ORDER_NHCW};

    bindings->inputs[InputIndex::UV_CHANNEL] = TensorBinding{.buffer = input_buffer,
                                                             .plane_id = PlaneId::ONE,
                                                             .buffer_name = "input buffer",
                                                             .tensor_name = denoise_config.network_config.uv_channel,
                                                             .format_order = HAILO_FORMAT_ORDER_NHWC};

    bindings->inputs[InputIndex::LOOPBACK_Y_CHANNEL] =
        TensorBinding{.buffer = nullptr,
                      .plane_id = PlaneId::ZERO,
                      .buffer_name = "input loopback buffer",
                      .tensor_name = denoise_config.network_config.feedback_y_channel,
                      .format_order = HAILO_FORMAT_ORDER_NHCW};

    bindings->inputs[InputIndex::LOOPBACK_UV_CHANNEL] =
        TensorBinding{.buffer = nullptr,
                      .plane_id = PlaneId::ONE,
                      .buffer_name = "input loopback buffer",
                      .tensor_name = denoise_config.network_config.feedback_uv_channel,
                      .format_order = HAILO_FORMAT_ORDER_NHWC};

    bindings->outputs[OutputIndex::OUTPUT_Y_CHANNEL] =
        TensorBinding{.buffer = output_buffer,
                      .plane_id = PlaneId::ZERO,
                      .buffer_name = "output buffer",
                      .tensor_name = denoise_config.network_config.output_y_channel,
                      .format_order = HAILO_FORMAT_ORDER_NHCW};

    bindings->outputs[OutputIndex::OUTPUT_UV_CHANNEL] =
        TensorBinding{.buffer = output_buffer,
                      .plane_id = PlaneId::ONE,
                      .buffer_name = "output buffer",
                      .tensor_name = denoise_config.network_config.output_uv_channel,
                      .format_order = HAILO_FORMAT_ORDER_NHWC};

    return bindings;
}

bool HailortAsyncDenoisePostISP::is_packed_output() const
{
    return true;
}

int HailortAsyncDenoisePostISP::get_denoised_output_index() const
{
    return OutputIndex::OUTPUT_Y_CHANNEL;
}

/*
    HailortAsyncDenoisePreISP
*/

bool HailortAsyncDenoisePreISP::is_using_dgain_and_bls(const denoise_config_t &denoise_config)
{
    return is_using_dgain_and_bls_pre_isp(denoise_config);
}

std::string HailortAsyncDenoisePreISP::get_network_path(const denoise_config_t &denoise_config) const
{
    return denoise_config.bayer_network_config.network_path;
}

/*
    HailortAsyncDenoisePreISPVd
*/

media_library_return HailortAsyncDenoisePreISPVd::bind_loopback_buffers(NetworkInferenceBindingsPtr bindings,
                                                                        const TensorBindings &loopback_buffers) const
{
    if (loopback_buffers[OutputIndex::OUTPUT_BAYER_CHANNEL].buffer == nullptr)
    {
        return media_library_return::MEDIA_LIBRARY_BUFFER_NOT_FOUND;
    }
    bindings->inputs[InputIndex::LOOPBACK_BAYER_CHANNEL].buffer =
        loopback_buffers[OutputIndex::OUTPUT_BAYER_CHANNEL].buffer;
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

bool HailortAsyncDenoisePreISPVd::is_packed_output() const
{
    return false;
}

NetworkInferenceBindingsPtr HailortAsyncDenoisePreISPVd::create_bindings(const denoise_config_t &denoise_config,
                                                                         HailoMediaLibraryBufferPtr input_buffer,
                                                                         HailoMediaLibraryBufferPtr output_buffer) const
{
    NetworkInferenceBindingsPtr bindings = std::make_shared<NetworkInferenceBindings>();
    bindings->inputs.resize(InputIndex::INPUT_SIZE);
    bindings->outputs.resize(OutputIndex::OUTPUT_SIZE);
    bindings->inputs[InputIndex::BAYER_CHANNEL] =
        TensorBinding{.buffer = input_buffer,
                      .plane_id = PlaneId::ZERO,
                      .buffer_name = "input buffer",
                      .tensor_name = denoise_config.bayer_network_config.bayer_channel,
                      .format_order = HAILO_FORMAT_ORDER_NHCW};

    bindings->inputs[InputIndex::LOOPBACK_BAYER_CHANNEL] =
        TensorBinding{.buffer = nullptr,
                      .plane_id = PlaneId::ZERO,
                      .buffer_name = "input loopback buffer",
                      .tensor_name = denoise_config.bayer_network_config.feedback_bayer_channel,
                      .format_order = HAILO_FORMAT_ORDER_NHCW};

    bindings->outputs[OutputIndex::OUTPUT_BAYER_CHANNEL] =
        TensorBinding{.buffer = output_buffer,
                      .plane_id = PlaneId::ZERO,
                      .buffer_name = "output buffer",
                      .tensor_name = denoise_config.bayer_network_config.output_bayer_channel,
                      .format_order = HAILO_FORMAT_ORDER_NHCW};

    if (is_using_dgain_and_bls(denoise_config))
    {
        bindings->gain_inputs.resize(GainIndex::GAIN_SIZE);
        bindings->gain_inputs[GainIndex::DG_GAIN_CHANNEL] =
            TensorBinding{.buffer = nullptr,
                          .plane_id = PlaneId::ZERO,
                          .buffer_name = "input dgain buffer",
                          .tensor_name = denoise_config.bayer_network_config.dgain_channel,
                          .format_order = HAILO_FORMAT_ORDER_NC};
        bindings->gain_inputs[GainIndex::BLS_CHANNEL] =
            TensorBinding{.buffer = nullptr,
                          .plane_id = PlaneId::ZERO,
                          .buffer_name = "input bls buffer",
                          .tensor_name = denoise_config.bayer_network_config.bls_channel,
                          .format_order = HAILO_FORMAT_ORDER_NC};
    }

    return bindings;
}

int HailortAsyncDenoisePreISPVd::get_denoised_output_index() const
{
    return OutputIndex::OUTPUT_BAYER_CHANNEL;
}

/*
    HailortAsyncDenoisePreISPHdm
*/

bool HailortAsyncDenoisePreISPHdm::is_packed_output() const
{
    return false;
}

bool HailortAsyncDenoisePreISPHdm::is_using_fusion_skips(const denoise_config_t &denoise_configs)
{
    return !denoise_configs.bayer_network_config.skip0_fusion_channel.empty() &&
           !denoise_configs.bayer_network_config.skip1_fusion_channel.empty();
}

media_library_return HailortAsyncDenoisePreISPHdm::bind_loopback_buffers(NetworkInferenceBindingsPtr bindings,
                                                                         const TensorBindings &loopback_buffers) const
{
    if (loopback_buffers[OutputIndex::OUTPUT_FUSION_CHANNEL].buffer == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "bind_loopback_buffers failed on fusion channel");
        return media_library_return::MEDIA_LIBRARY_BUFFER_NOT_FOUND;
    }
    bindings->inputs[InputIndex::FUSION_CHANNEL].buffer = loopback_buffers[OutputIndex::OUTPUT_FUSION_CHANNEL].buffer;
    if (loopback_buffers[OutputIndex::OUTPUT_GAMMA_CHANNEL].buffer == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "bind_loopback_buffers failed on gamma channel");
        return media_library_return::MEDIA_LIBRARY_BUFFER_NOT_FOUND;
    }
    bindings->inputs[InputIndex::GAMMA_CHANNEL].buffer = loopback_buffers[OutputIndex::OUTPUT_GAMMA_CHANNEL].buffer;
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

NetworkInferenceBindingsPtr HailortAsyncDenoisePreISPHdm::create_bindings(
    const denoise_config_t &denoise_config, HailoMediaLibraryBufferPtr input_buffer,
    HailoMediaLibraryBufferPtr output_buffer) const
{
    NetworkInferenceBindingsPtr bindings = std::make_shared<NetworkInferenceBindings>();
    bindings->inputs.resize(InputIndex::INPUT_SIZE);
    bindings->outputs.resize(OutputIndex::OUTPUT_SIZE);
    bindings->inputs[InputIndex::BAYER_CHANNEL] =
        TensorBinding{.buffer = input_buffer,
                      .plane_id = PlaneId::ZERO,
                      .buffer_name = "input buffer",
                      .tensor_name = denoise_config.bayer_network_config.bayer_channel,
                      .format_order = HAILO_FORMAT_ORDER_NHCW};

    bindings->inputs[InputIndex::GAMMA_CHANNEL] =
        TensorBinding{.buffer = nullptr,
                      .plane_id = PlaneId::ZERO,
                      .buffer_name = "input gamma buffer",
                      .tensor_name = denoise_config.bayer_network_config.input_gamma_feedback,
                      .format_order = HAILO_FORMAT_ORDER_NHCW};

    bindings->inputs[InputIndex::FUSION_CHANNEL] =
        TensorBinding{.buffer = nullptr,
                      .plane_id = PlaneId::ZERO,
                      .buffer_name = "input fusion buffer",
                      .tensor_name = denoise_config.bayer_network_config.input_fusion_feedback,
                      .format_order = HAILO_FORMAT_ORDER_NHCW};

    bindings->outputs[OutputIndex::OUTPUT_BAYER_CHANNEL] =
        TensorBinding{.buffer = output_buffer,
                      .plane_id = PlaneId::ZERO,
                      .buffer_name = "output buffer",
                      .tensor_name = denoise_config.bayer_network_config.output_bayer_channel,
                      .format_order = HAILO_FORMAT_ORDER_NHCW};

    bindings->outputs[OutputIndex::OUTPUT_GAMMA_CHANNEL] =
        TensorBinding{.buffer = nullptr,
                      .plane_id = PlaneId::ZERO,
                      .buffer_name = "output gamma buffer",
                      .tensor_name = denoise_config.bayer_network_config.output_gamma_feedback,
                      .format_order = HAILO_FORMAT_ORDER_NHCW};

    bindings->outputs[OutputIndex::OUTPUT_FUSION_CHANNEL] =
        TensorBinding{.buffer = nullptr,
                      .plane_id = PlaneId::ZERO,
                      .buffer_name = "output fusion buffer",
                      .tensor_name = denoise_config.bayer_network_config.output_fusion_feedback,
                      .format_order = HAILO_FORMAT_ORDER_NHCW};

    if (is_using_dgain_and_bls(denoise_config))
    {
        bindings->gain_inputs.resize(GainIndex::GAIN_SIZE);
        bindings->gain_inputs[GainIndex::DG_GAIN_CHANNEL] =
            TensorBinding{.buffer = nullptr,
                          .plane_id = PlaneId::ZERO,
                          .buffer_name = "input dgain buffer",
                          .tensor_name = denoise_config.bayer_network_config.dgain_channel,
                          .format_order = HAILO_FORMAT_ORDER_NC};
        bindings->gain_inputs[GainIndex::BLS_CHANNEL] =
            TensorBinding{.buffer = nullptr,
                          .plane_id = PlaneId::ZERO,
                          .buffer_name = "input bls buffer",
                          .tensor_name = denoise_config.bayer_network_config.bls_channel,
                          .format_order = HAILO_FORMAT_ORDER_NC};
    }
    if (is_using_fusion_skips(denoise_config))
    {
        bindings->skip_inputs.resize(SkipIndex::SKIP_SIZE);
        bindings->skip_inputs[SkipIndex::SKIP0_FUSION_CHANNEL] =
            TensorBinding{.buffer = nullptr,
                          .plane_id = PlaneId::ZERO,
                          .buffer_name = "input skip0 fusion buffer",
                          .tensor_name = denoise_config.bayer_network_config.skip0_fusion_channel,
                          .format_order = HAILO_FORMAT_ORDER_NHCW};
        bindings->skip_inputs[SkipIndex::SKIP1_FUSION_CHANNEL] =
            TensorBinding{.buffer = nullptr,
                          .plane_id = PlaneId::ZERO,
                          .buffer_name = "input skip1 fusion buffer",
                          .tensor_name = denoise_config.bayer_network_config.skip1_fusion_channel,
                          .format_order = HAILO_FORMAT_ORDER_NHCW};
    }
    return bindings;
}

int HailortAsyncDenoisePreISPHdm::get_denoised_output_index() const
{
    return OutputIndex::OUTPUT_BAYER_CHANNEL;
}
