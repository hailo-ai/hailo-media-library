#pragma once

#include "hailo/hailort.hpp"
#include <iostream>

#define ERROR -1
#define SUCCESS 0

struct tensors_info
{
    std::string input_LEF_tensor_name;
    std::string input_SEF1_tensor_name;
    std::string input_SEF2_tensor_name;
    std::string output_stitched_tensor_name;
};

typedef struct hdr_hailort_params
{
    std::string hef_path;
    std::string group_id;
    int scheduler_threshold;
    int scheduler_timeout_in_ms;
} hdr_hailort_params_t;

class HailortAsyncStitching
{
private:
    std::string m_hef_path;
    std::string m_group_id;
    int m_scheduler_threshold;
    int m_scheduler_timeout_in_ms;
    tensors_info m_tensors_info;
    std::function<void(void *output_buffer)> m_on_infer_finish;

    std::unique_ptr<hailort::VDevice> m_vdevice;
    std::shared_ptr<hailort::InferModel> m_infer_model;
    hailort::ConfiguredInferModel m_configured_infer_model;
    hailort::ConfiguredInferModel::Bindings m_bindings;

public:
    HailortAsyncStitching(std::function<void(void *output_buffer)> on_infer_finish)
        : m_on_infer_finish(on_infer_finish)
    {
        m_tensors_info.input_LEF_tensor_name = "hdr4k/input_layer1";
        m_tensors_info.input_SEF1_tensor_name = "hdr4k/input_layer2";
        m_tensors_info.input_SEF2_tensor_name = "hdr4k/input_layer3";
        m_tensors_info.output_stitched_tensor_name = "hdr4k/concat_ew_add2";
    }

    int init(hdr_hailort_params_t &hailort_params)
    {
        m_hef_path = hailort_params.hef_path;
        m_group_id = hailort_params.group_id;
        m_scheduler_threshold = hailort_params.scheduler_threshold;
        m_scheduler_timeout_in_ms = hailort_params.scheduler_timeout_in_ms;

        hailo_vdevice_params_t vdevice_params = {0};
        hailo_init_vdevice_params(&vdevice_params);
        vdevice_params.group_id = m_group_id.c_str();

        auto vdevice_exp = hailort::VDevice::create(vdevice_params);
        if (!vdevice_exp)
        {
            std::cerr << "Failed create vdevice, status = " << vdevice_exp.status() << std::endl;
            return vdevice_exp.status();
        }
        m_vdevice = vdevice_exp.release();

        auto infer_model_exp = m_vdevice->create_infer_model(m_hef_path.c_str());
        if (!infer_model_exp)
        {
            std::cerr << "Failed to create infer model, status = " << infer_model_exp.status() << std::endl;
            return infer_model_exp.status();
        }
        m_infer_model = infer_model_exp.release();
        m_infer_model->set_batch_size(1);

        // input order
        m_infer_model->input(m_tensors_info.input_LEF_tensor_name)->set_format_order(HAILO_FORMAT_ORDER_NHCW);
        m_infer_model->input(m_tensors_info.input_SEF1_tensor_name)->set_format_order(HAILO_FORMAT_ORDER_NHCW);
        m_infer_model->input(m_tensors_info.input_SEF2_tensor_name)->set_format_order(HAILO_FORMAT_ORDER_NHCW);
        m_infer_model->input(m_tensors_info.input_LEF_tensor_name)->set_format_type(HAILO_FORMAT_TYPE_UINT16);
        m_infer_model->input(m_tensors_info.input_SEF1_tensor_name)->set_format_type(HAILO_FORMAT_TYPE_UINT16);
        m_infer_model->input(m_tensors_info.input_SEF2_tensor_name)->set_format_type(HAILO_FORMAT_TYPE_UINT16);

        // output order
        m_infer_model->output(m_tensors_info.output_stitched_tensor_name)->set_format_order(HAILO_FORMAT_ORDER_FCR);
        m_infer_model->output(m_tensors_info.output_stitched_tensor_name)->set_format_type(HAILO_FORMAT_TYPE_UINT8);

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

    int process(void *input_buffer[3], void *output_buffer)
    {
        // LOGGER__ERROR("hailort_Stitching starting process");
        if (set_input_buffers(input_buffer) != SUCCESS)
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
    int set_input_buffer(void *ptr, std::string tensor_name)
    {
        auto size = m_infer_model->input(tensor_name)->get_frame_size();

        auto status = m_bindings.input(tensor_name)->set_buffer(hailort::MemoryView(ptr, size));
        if (HAILO_SUCCESS != status)
        {
            std::cerr << "Failed to set infer input: " << tensor_name << " buffer, status = " << status << std::endl;
            return status;
        }

        return SUCCESS;
    }

    int set_input_buffers(void *input_buffers[3])
    {
        if (set_input_buffer(input_buffers[0], m_tensors_info.input_LEF_tensor_name) != SUCCESS)
        {
            return ERROR;
        }

        if (set_input_buffer(input_buffers[1], m_tensors_info.input_SEF1_tensor_name) != SUCCESS)
        {
            return ERROR;
        }

        if (set_input_buffer(input_buffers[2], m_tensors_info.input_SEF2_tensor_name) != SUCCESS)
        {
            return ERROR;
        }

        return SUCCESS;
    }

    int set_output_buffer(void *output_buffer, std::string tensor_name)
    {
        auto size = m_infer_model->output(tensor_name)->get_frame_size();

        auto status = m_bindings.output(tensor_name)->set_buffer(hailort::MemoryView(output_buffer, size));
        if (HAILO_SUCCESS != status)
        {
            std::cerr << "Failed to set infer output: " << tensor_name << " buffer, status = " << status << std::endl;
            return status;
        }

        return SUCCESS;
    }

    int set_output_buffers(void *output_buffer)
    {
        if (set_output_buffer(output_buffer, m_tensors_info.output_stitched_tensor_name) != SUCCESS)
        {
            return ERROR;
        }

        return SUCCESS;
    }

    int infer(void *output_buffer)
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
                // std::cerr << "Failed to run async infer, status = " << completion_info.status << std::endl;
                m_on_infer_finish(output_buffer);
                return ERROR;
            }

            m_on_infer_finish(output_buffer);
            // LOGGER__ERROR("hailort_Stitching.hpp finished callback");
            return SUCCESS; });

        if (!job)
        {
            std::cerr << "Failed to start async infer job, status = " << job.status() << std::endl;
            return job.status();
        }

        job->detach();

        return SUCCESS;
    }
};
using HailortAsyncStitchingPtr = std::shared_ptr<HailortAsyncStitching>;

typedef struct hdr_params
{
    int fd_video2;
    int fd_video3;
    HailortAsyncStitching *stitcher;
} hdr_params_t;

int hdr_init(hdr_hailort_params_t params, hdr_params_t *hdr_params);
void hdr_loop(int fd_video2, int fd_video3, HailortAsyncStitching *stitcher);
void hdr_start_loop();
void hdr_stop_loop();
void hdr_finish(int fd_video2, int fd_video3, HailortAsyncStitching *stitcher);
