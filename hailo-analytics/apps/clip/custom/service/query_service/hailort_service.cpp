#include "hailort_service.hpp"

#include <cstring>

void HailortService::register_output_callback(const std::function<void(std::vector<float>)> &callback)
{
    m_output_callback_handler.register_callback(callback);
}

HailortService::HailortService(const std::string &model_path, const std::string &hailort_device_id, int batch_size)
    : m_hailort_device_id(hailort_device_id), m_model_path(model_path), m_batch_size(batch_size),
      m_input_buffer_mgr(nullptr), m_output_buffer_mgr(nullptr)
{
    m_last_infer_job = nullptr;
}

HailortService::~HailortService()
{
    // Wait for last infer to finish
    if (m_last_infer_job)
    {
        auto status = m_last_infer_job->wait(std::chrono::milliseconds(10000));
        if (HAILO_SUCCESS != status)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to wait for infer to finish, status = {}", status);
        }
    }
}

HailortServiceStatus HailortService::initialize()
{
    if (m_initialized)
    {
        HAILO_ANALYTICS_LOG_INFO("HailortService is already initialized.");
        return HailortServiceStatus::SUCCESS;
    }

    // Initialize HailoRT to use the same device group id.
    hailo_vdevice_params_t vdevice_params{};
    hailo_init_vdevice_params(&vdevice_params);
    vdevice_params.group_id = m_hailort_device_id.c_str();

    // Create a vdevice
    auto vdevice_exp = hailort::VDevice::create(vdevice_params);

    if (!vdevice_exp)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed create vdevice, Hailort status = {}", vdevice_exp.status());
        return HailortServiceStatus::HAILORT_ERROR;
    }
    m_vdevice = vdevice_exp.release();

    // Create infer model from HEF file and configure input and output to float32
    auto infer_model_exp = m_vdevice->create_infer_model(m_model_path.c_str());
    if (!infer_model_exp)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create infer model, Hailort status = {}", infer_model_exp.status());
        return HailortServiceStatus::HAILORT_ERROR;
    }
    m_infer_model = infer_model_exp.release();

    if (m_infer_model->get_input_names().size() != 1 || m_infer_model->get_output_names().size() != 1)
    {
        HAILO_ANALYTICS_LOG_ERROR("Currently Model support must have exactly one input and one output tensor");
        return HailortServiceStatus::CONFIGURATION_ERROR;
    }

    m_infer_model->set_batch_size(m_batch_size);
    m_infer_model->input()->set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
    m_infer_model->output()->set_format_type(HAILO_FORMAT_TYPE_FLOAT32);

    // Configure the infer model
    auto configured_infer_model_exp = m_infer_model->configure();
    if (!configured_infer_model_exp)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create configured infer model, Hailort status = {}",
                                  configured_infer_model_exp.status());
        return HailortServiceStatus::HAILORT_ERROR;
    }
    m_configured_infer_model = configured_infer_model_exp.release();

    // Create infer bindings
    auto bindings = m_configured_infer_model.create_bindings();
    if (!bindings)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create infer bindings, Hailort status = {}", bindings.status());
        return HailortServiceStatus::HAILORT_ERROR;
    }
    m_bindings = bindings.release();

    // Create buffer managers for input and output tensors
    for (const auto &input_name : m_infer_model->get_input_names())
    {
        size_t input_frame_size = m_infer_model->input(input_name)->get_frame_size();
        HAILO_ANALYTICS_LOG_INFO("Input tensor {} frame size: {} bytes", input_name, input_frame_size);
        m_input_buffer_mgr = std::make_unique<HailortBufferManager>(m_vdevice, input_frame_size, 10,
                                                                    HailortBufferManager::BufferType::INPUT);
        if (!m_input_buffer_mgr)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to create input buffer manager");
            return HailortServiceStatus::BUFFER_ALLOCATION_ERROR;
        }
        m_input_name = input_name;
        break;
    }

    for (const auto &output_name : m_infer_model->get_output_names())
    {
        size_t output_frame_size = m_infer_model->output(output_name)->get_frame_size();
        HAILO_ANALYTICS_LOG_INFO("Output tensor {} frame size: {} bytes", output_name, output_frame_size);
        m_output_buffer_mgr = std::make_unique<HailortBufferManager>(m_vdevice, output_frame_size, 10,
                                                                     HailortBufferManager::BufferType::OUTPUT);
        if (!m_output_buffer_mgr)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to create output buffer manager");
            return HailortServiceStatus::BUFFER_ALLOCATION_ERROR;
        }
        m_output_name = output_name;
        break;
    }

    m_initialized = true;
    return HailortServiceStatus::SUCCESS;
}

HailortServiceStatus HailortService::infer(const std::vector<float> &input_data)
{
    if (!m_initialized)
    {
        HAILO_ANALYTICS_LOG_ERROR("HailortService is not initialized.");
        return HailortServiceStatus::UNINITIALIZED;
    }

    auto infer_status = m_configured_infer_model.wait_for_async_ready(std::chrono::milliseconds(1000));
    if (HAILO_SUCCESS != infer_status)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to wait for async ready, Hailort status = {}", infer_status);
        return HailortServiceStatus::HAILORT_ERROR;
    }

    if (input_data.size() * sizeof(float) != m_infer_model->input(m_input_name)->get_frame_size())
    {
        HAILO_ANALYTICS_LOG_ERROR("Input data size does not match model input size.");
        return HailortServiceStatus::INVALID_ARGUMENT;
    }

    // Acquire input buffer
    auto input_buffer = m_input_buffer_mgr->get_buffer();
    if (!input_buffer)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to acquire input buffer");
        return HailortServiceStatus::BUFFER_ALLOCATION_ERROR;
    }

    // Copy input data to input buffer
    std::memcpy(input_buffer.get(), input_data.data(), input_data.size() * sizeof(float));

    // Set the input buffer for inference
    auto status = m_bindings.input(m_input_name)
                      ->set_buffer(hailort::MemoryView(input_buffer.get(), input_data.size() * sizeof(float)));
    if (HAILO_SUCCESS != status)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to set infer input buffer, Hailort status = {}", status);
        return HailortServiceStatus::HAILORT_ERROR;
    }

    // Acquire output buffer
    auto output_buffer = m_output_buffer_mgr->get_buffer();
    if (!output_buffer)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to acquire output buffer");
        return HailortServiceStatus::BUFFER_ALLOCATION_ERROR;
    }

    // Set the output buffer for inference
    status = m_bindings.output(m_output_name)
                 ->set_buffer(
                     hailort::MemoryView(output_buffer.get(), m_infer_model->output(m_output_name)->get_frame_size()));
    if (HAILO_SUCCESS != status)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to set infer output buffer, Hailort status = {}", status);
        return HailortServiceStatus::HAILORT_ERROR;
    }

    auto job = m_configured_infer_model.run_async(
        m_bindings, [output_buffer, input_buffer, this](const hailort::AsyncInferCompletionInfo &completion_info) {
            if (completion_info.status != HAILO_SUCCESS)
            {
                HAILO_ANALYTICS_LOG_ERROR("Failed to run async infer, Hailort status = {}", completion_info.status);
                return;
            }

            m_output_callback_handler.trigger_callbacks(
                std::vector<float>(reinterpret_cast<float *>(output_buffer.get()),
                                   reinterpret_cast<float *>(output_buffer.get()) +
                                       (m_infer_model->output(m_output_name)->get_frame_size() / sizeof(float))));

            return;
        });

    if (!job)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to start async infer job, status = {}", job.status());
        return HailortServiceStatus::HAILORT_ERROR;
    }

    // detach the job to run in it's own thread on the side
    job->detach();
    m_last_infer_job = std::make_shared<hailort::AsyncInferJob>(job.release());

    return HailortServiceStatus::SUCCESS;
}
