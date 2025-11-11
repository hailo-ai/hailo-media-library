#pragma once

#include "hailo/infer_model.hpp"
#include "hailo/vdevice.hpp"

#include "buffer_pool.hpp"
#include <chrono>
#include <cstdint>

struct hailort_configured_device_t
{
    std::shared_ptr<hailort::InferModel> infer_model;
    hailort::ConfiguredInferModel configured_infer_model;
    hailort::ConfiguredInferModel::Bindings bindings;
};

class HailortAsyncDenoise
{
  public:
    using OnInferCb = std::function<void(HailoMediaLibraryBufferPtr output_buffer)>;

    HailortAsyncDenoise(const OnInferCb &on_infer_finish);
    ~HailortAsyncDenoise();

    bool set_config(const denoise_config_t &denoise_config, const std::string &group_id, int scheduler_threshold,
                    const std::chrono::milliseconds &scheduler_timeout, int batch_size);
    bool process(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr loopback_input_buffer,
                 HailoMediaLibraryBufferPtr output_buffer);
    bool map_buffer_to_hailort(int fd, size_t size);
    bool unmap_buffer_to_hailort(int fd, size_t size);
    void wait_for_all_jobs_to_finish();
    bool has_pending_jobs();

  private:
    OnInferCb m_on_infer_finish;
    std::string m_group_id;
    int m_scheduler_threshold;
    std::chrono::milliseconds m_scheduler_timeout;
    denoise_config_t m_denoise_config;
    hailort::AsyncInferJob m_last_infer_job;
    std::atomic<uint64_t> m_last_inserted_infer_output_buffer_timestamp = 0;
    std::atomic<uint64_t> m_last_result_infer_output_buffer_timestamp = 0;

    std::string m_current_vdevice_name;
    std::shared_ptr<hailort::VDevice> m_vdevice;
    std::unordered_map<std::string, std::shared_ptr<hailort_configured_device_t>> m_configured_devices;

    static constexpr std::chrono::seconds WAIT_FOR_LAST_INFER_TIMEOUT{1};
    bool set_input_buffer(int fd, const std::string &tensor_name);
    bool set_pre_isp_input_buffers(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr loopback_buffer);
    bool set_post_isp_input_buffers(HailoMediaLibraryBufferPtr input_buffer,
                                    HailoMediaLibraryBufferPtr loopback_buffer);
    bool set_input_buffers(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr loopback_buffer);

    bool set_output_buffer(int fd, const std::string &tensor_name);
    bool set_pre_isp_output_buffers(HailoMediaLibraryBufferPtr output_buffer);
    bool set_post_isp_output_buffers(HailoMediaLibraryBufferPtr output_buffer);
    bool set_output_buffers(HailoMediaLibraryBufferPtr output_buffer);

    bool infer(HailoMediaLibraryBufferPtr output_buffer);
};
using HailortAsyncDenoisePtr = std::shared_ptr<HailortAsyncDenoise>;
