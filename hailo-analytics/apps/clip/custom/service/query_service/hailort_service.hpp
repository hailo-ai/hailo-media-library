
#pragma once
#include <infer_model.hpp>
#include <vdevice.hpp>

#include "hailort_buffer_manager.hpp"
#include "common_utils.hpp"

// Custom status enum for HailortService
enum class HailortServiceStatus
{
    SUCCESS = 0,
    INVALID_ARGUMENT,
    CONFIGURATION_ERROR,
    BUFFER_ALLOCATION_ERROR,
    HAILORT_ERROR,
    UNINITIALIZED,
    UNKNOWN_ERROR
};

class HailortService
{
  private:
    std::string m_hailort_device_id;
    std::shared_ptr<hailort::VDevice> m_vdevice;
    std::shared_ptr<hailort::InferModel> m_infer_model;
    hailort::ConfiguredInferModel::Bindings m_bindings;
    hailort::ConfiguredInferModel m_configured_infer_model;
    std::string m_input_name;
    std::string m_output_name;
    std::string m_model_path;
    int m_batch_size = 1;

    std::unique_ptr<HailortBufferManager> m_input_buffer_mgr;
    std::unique_ptr<HailortBufferManager> m_output_buffer_mgr;
    SystemUtils::AsyncCallbackHandler<std::vector<float>> m_output_callback_handler;
    std::shared_ptr<hailort::AsyncInferJob> m_last_infer_job; ///< Pointer to the last asynchronous inference job.

    bool m_initialized = false;

  public:
    HailortService(const std::string &model_path, const std::string &hailort_device_id, int batch_size);
    ~HailortService();

    // Delete copy constructor and copy assignment operator
    HailortService(const HailortService &) = delete;
    HailortService &operator=(const HailortService &) = delete;

    // Register a callback for processing output data
    void register_output_callback(const std::function<void(std::vector<float>)> &callback);

    HailortServiceStatus initialize();
    HailortServiceStatus infer(const std::vector<float> &input_data);
};
