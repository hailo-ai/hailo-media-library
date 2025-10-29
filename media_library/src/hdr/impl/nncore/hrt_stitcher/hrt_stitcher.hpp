#pragma once

#include "hailo/hailort.hpp"
#include "media_library_logger.hpp"

#define HAILO_STITCH_ERROR -1
#define HAILO_STITCH_SUCCESS 0

struct TensorInfo
{
    std::string m_input_lef_tensor_name;
    std::string m_input_sef1_tensor_name;
    std::string m_input_sef2_tensor_name;
    std::string m_input_wb_tensor_name;
    std::string m_output_stitched_tensor_name;

    void init(int num_exp);
};

class HailortAsyncStitching
{
  private:
    static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;

    std::function<void(std::shared_ptr<void> stitch_context)> m_on_infer_finish;
    std::string m_hef_path;
    std::string m_group_id;
    int m_scheduler_threshold;
    int m_scheduler_timeout_in_ms;
    int m_num_exp;
    TensorInfo m_tensors_info;
    hailort::AsyncInferJob m_last_infer_job;

    std::unique_ptr<hailort::VDevice> m_vdevice;
    std::shared_ptr<hailort::InferModel> m_infer_model;
    hailort::ConfiguredInferModel m_configured_infer_model;
    hailort::ConfiguredInferModel::Bindings m_bindings;

  public:
    HailortAsyncStitching();

    void set_on_infer_finish(std::function<void(std::shared_ptr<void> stitch_context)> on_infer_finish);

    int init(const std::string &hef_path, const std::string &group_id, int scheduler_threshold,
             int scheduler_timeout_in_ms, int num_exp);

    int process(int *input_buffers, int awb_buffer, int output_buffer, std::shared_ptr<void> stitch_context);

  private:
    int set_input_buffer(int input_buffer, std::string tensor_name);
    int set_input_buffers(int *input_buffers, int awb_buffer);

    int set_output_buffer(int output_buffer, std::string tensor_name);
    int set_output_buffers(int output_buffer);

    int infer(std::shared_ptr<void> stitch_context);
};

using HailortAsyncStitchingPtr = std::shared_ptr<HailortAsyncStitching>;
