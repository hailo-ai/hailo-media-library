#include "hrt_stitcher.hpp"
#include "logger_macros.hpp"
#include "hailo_media_library_perfetto.hpp"
#include <iostream>

void TensorInfo::init(int num_exp)
{
    m_input_lef_tensor_name = "hdr/input_layer1";
    m_input_sef1_tensor_name = "hdr/input_layer2";
    constexpr std::string_view SEF2_OR_WB = "hdr/input_layer3"; // SEF2 for 2dol, WB for 3dol
    constexpr std::string_view DOL3_WB = "hdr/input_layer4";
    m_output_stitched_tensor_name = "hdr/concat_out";

    if (num_exp == 2)
    { // 2dol
        m_input_wb_tensor_name = SEF2_OR_WB;
    }
    else
    { // 3dol
        m_input_sef2_tensor_name = SEF2_OR_WB;
        m_input_wb_tensor_name = DOL3_WB;
    }
}

HailortAsyncStitching::HailortAsyncStitching()
{
}

void HailortAsyncStitching::set_on_infer_finish(
    std::function<void(std::shared_ptr<void> stitch_context)> on_infer_finish)
{
    m_on_infer_finish = on_infer_finish;
}

int HailortAsyncStitching::init(const std::string &hef_path, const std::string &group_id, int scheduler_threshold,
                                int scheduler_timeout_in_ms, int num_exp)
{
    m_hef_path = hef_path;
    m_group_id = group_id;
    m_scheduler_threshold = scheduler_threshold;
    m_scheduler_timeout_in_ms = scheduler_timeout_in_ms;
    m_tensors_info.init(num_exp);

    hailo_vdevice_params_t vdevice_params;
    memset(&vdevice_params, 0, sizeof(vdevice_params));
    hailo_init_vdevice_params(&vdevice_params);
    vdevice_params.group_id = m_group_id.c_str();

    auto vdevice_exp = hailort::VDevice::create(vdevice_params);
    if (!vdevice_exp)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed create vdevice, status = {}", vdevice_exp.status());
        return vdevice_exp.status();
    }
    m_vdevice = vdevice_exp.release();

    auto infer_model_exp = m_vdevice->create_infer_model(m_hef_path.c_str());
    if (!infer_model_exp)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to create infer model, status = {}", infer_model_exp.status());
        return infer_model_exp.status();
    }
    m_infer_model = infer_model_exp.release();

    m_infer_model->set_batch_size(1);

    // input order
    m_infer_model->input(m_tensors_info.m_input_lef_tensor_name)->set_format_order(HAILO_FORMAT_ORDER_NHWC);
    m_infer_model->input(m_tensors_info.m_input_lef_tensor_name)->set_format_type(HAILO_FORMAT_TYPE_UINT16);
    m_infer_model->input(m_tensors_info.m_input_sef1_tensor_name)->set_format_order(HAILO_FORMAT_ORDER_NHWC);
    m_infer_model->input(m_tensors_info.m_input_sef1_tensor_name)->set_format_type(HAILO_FORMAT_TYPE_UINT16);
    if (num_exp == 3)
    {
        m_infer_model->input(m_tensors_info.m_input_sef2_tensor_name)->set_format_order(HAILO_FORMAT_ORDER_NHWC);
        m_infer_model->input(m_tensors_info.m_input_sef2_tensor_name)->set_format_type(HAILO_FORMAT_TYPE_UINT16);
    }

    m_infer_model->input(m_tensors_info.m_input_wb_tensor_name)->set_format_order(HAILO_FORMAT_ORDER_NHWC);
    m_infer_model->input(m_tensors_info.m_input_wb_tensor_name)->set_format_type(HAILO_FORMAT_TYPE_UINT8);

    // output order
    m_infer_model->output(m_tensors_info.m_output_stitched_tensor_name)->set_format_order(HAILO_FORMAT_ORDER_NHWC);
    m_infer_model->output(m_tensors_info.m_output_stitched_tensor_name)->set_format_type(HAILO_FORMAT_TYPE_UINT8);

    auto configured_infer_model_exp = m_infer_model->configure();
    if (!configured_infer_model_exp)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to create configured infer model, status = {}",
                              configured_infer_model_exp.status());
        return configured_infer_model_exp.status();
    }
    m_configured_infer_model = configured_infer_model_exp.release();
    m_configured_infer_model.set_scheduler_threshold(m_scheduler_threshold);
    m_configured_infer_model.set_scheduler_timeout(std::chrono::milliseconds(m_scheduler_timeout_in_ms));
    m_configured_infer_model.set_scheduler_priority(HAILO_SCHEDULER_PRIORITY_MAX);

    auto bindings = m_configured_infer_model.create_bindings();
    if (!bindings)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to create infer bindings, status = {}", bindings.status());
        return bindings.status();
    }
    m_bindings = bindings.release();
    m_num_exp = num_exp;
    return HAILO_STITCH_SUCCESS;
}

int HailortAsyncStitching::process(int *input_buffers, int awb_buffer, int output_buffer,
                                   std::shared_ptr<void> stitch_context)
{
    if (set_input_buffers(input_buffers, awb_buffer) != HAILO_STITCH_SUCCESS)
    {
        std::cerr << "can't set input buffer" << std::endl;
        return HAILO_STITCH_ERROR;
    }

    if (set_output_buffers(output_buffer) != HAILO_STITCH_SUCCESS)
    {
        std::cerr << "cant set output buffer" << std::endl;
        return HAILO_STITCH_ERROR;
    }

    if (infer(stitch_context) != HAILO_STITCH_SUCCESS)
    {
        std::cerr << "running infer failed" << std::endl;
        return HAILO_STITCH_ERROR;
    }

    return HAILO_STITCH_SUCCESS;
}

int HailortAsyncStitching::set_input_buffer(int input_buffer, std::string tensor_name)
{
    auto size = m_infer_model->input(tensor_name)->get_frame_size();
    auto status = m_bindings.input(tensor_name)->set_dma_buffer({.fd = input_buffer, .size = size});

    if (HAILO_STITCH_SUCCESS != status)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to set infer input: {} buffer, status = {}", tensor_name, status);
        return status;
    }

    return HAILO_STITCH_SUCCESS;
}

int HailortAsyncStitching::set_input_buffers(int *input_buffers, int awb_buffer)
{
    if (set_input_buffer(input_buffers[0], m_tensors_info.m_input_lef_tensor_name) != HAILO_STITCH_SUCCESS)
    {
        std::cerr << "cant set lef input buffer" << std::endl;
        return HAILO_STITCH_ERROR;
    }

    if (set_input_buffer(input_buffers[1], m_tensors_info.m_input_sef1_tensor_name) != HAILO_STITCH_SUCCESS)
    {
        std::cerr << "cant set sef1 input buffer" << std::endl;
        return HAILO_STITCH_ERROR;
    }

    if (m_num_exp == 3)
    {
        if (set_input_buffer(input_buffers[2], m_tensors_info.m_input_sef2_tensor_name) != HAILO_STITCH_SUCCESS)
        {
            std::cerr << "cant set sef2 input buffer" << std::endl;
            return HAILO_STITCH_ERROR;
        }
    }

    if (set_input_buffer(awb_buffer, m_tensors_info.m_input_wb_tensor_name) != HAILO_STITCH_SUCCESS)
    {
        std::cerr << "cant set wb input buffer" << std::endl;
        return HAILO_STITCH_ERROR;
    }

    return HAILO_STITCH_SUCCESS;
}

int HailortAsyncStitching::set_output_buffer(int output_buffer, std::string tensor_name)
{
    auto size = m_infer_model->output(tensor_name)->get_frame_size();
    auto status = m_bindings.output(tensor_name)->set_dma_buffer({.fd = output_buffer, .size = size});

    if (HAILO_STITCH_SUCCESS != status)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to set infer output: {} buffer, status = {}", tensor_name, status);
        return status;
    }

    return HAILO_STITCH_SUCCESS;
}

int HailortAsyncStitching::set_output_buffers(int output_buffer)
{
    if (set_output_buffer(output_buffer, m_tensors_info.m_output_stitched_tensor_name) != HAILO_STITCH_SUCCESS)
    {
        return HAILO_STITCH_ERROR;
    }

    return HAILO_STITCH_SUCCESS;
}

int HailortAsyncStitching::infer(std::shared_ptr<void> stitch_context)
{
    auto status = m_configured_infer_model.wait_for_async_ready(std::chrono::milliseconds(10000));
    if (HAILO_STITCH_SUCCESS != status)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to wait for async ready, status = {}", status);
        return status;
    }

    HAILO_MEDIA_LIBRARY_TRACE_ASYNC_EVENT_BEGIN("Inference", (uint64_t)stitch_context.get(), HDR_TRACK);

    auto job = m_configured_infer_model.run_async(
        m_bindings,
        [stitch_context = std::move(stitch_context), this](const hailort::AsyncInferCompletionInfo &completion_info) {
            if (completion_info.status != HAILO_STITCH_SUCCESS)
            {
                LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to run async infer, status = {}", completion_info.status);
                return HAILO_STITCH_ERROR;
            }

            HAILO_MEDIA_LIBRARY_TRACE_ASYNC_EVENT_END("Inference", (uint64_t)stitch_context.get(), HDR_TRACK);
            m_on_infer_finish(std::move(stitch_context));
            return HAILO_STITCH_SUCCESS;
        });

    if (!job)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to start async infer job, status = {}", job.status());
        return job.status();
    }

    job->detach();

    return HAILO_STITCH_SUCCESS;
}
