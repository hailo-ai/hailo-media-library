#pragma once

#include <hailo/hailort.h>
#include <stddef.h>
#include <chrono>
#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "hailo/infer_model.hpp"
#include "hailo/vdevice.hpp"
#include "buffer_pool.hpp"
#include "media_library_types.hpp"

struct hailort_configured_device_t
{
    std::shared_ptr<hailort::InferModel> infer_model;
    hailort::ConfiguredInferModel configured_infer_model;
    hailort::ConfiguredInferModel::Bindings bindings;
};

enum PlaneId
{
    ZERO = 0,
    ONE = 1
};

struct TensorBinding
{
    HailoMediaLibraryBufferPtr buffer;
    PlaneId plane_id;
    std::string buffer_name;
    std::string tensor_name;
    hailo_format_order_t format_order;
};
using TensorBindings = std::vector<TensorBinding>;

struct NetworkInferenceBindings
{
    std::vector<TensorBinding> inputs;
    std::vector<TensorBinding> outputs;
    std::vector<TensorBinding> gain_inputs;
    std::vector<TensorBinding> skip_inputs;
};
using NetworkInferenceBindingsPtr = std::shared_ptr<NetworkInferenceBindings>;

HailoMediaLibraryBufferPtr get_output_buffer(NetworkInferenceBindingsPtr bindings, int index);
void bind_output_buffer(NetworkInferenceBindingsPtr bindings, int index, HailoMediaLibraryBufferPtr buffer);
void bind_input_buffer(NetworkInferenceBindingsPtr bindings, int index, HailoMediaLibraryBufferPtr buffer);
void bind_gain_input_buffer(NetworkInferenceBindingsPtr bindings, int index, HailoMediaLibraryBufferPtr buffer);
void bind_skip_input_buffer(NetworkInferenceBindingsPtr bindings, int index, HailoMediaLibraryBufferPtr buffer);

enum class HailortAsyncDenoiseType
{
    PostISP,  // AI-ISP Gen1
    PreISPVd, // AI-ISP Gen2 (VDM)
    PreISPHdm // AI-ISP Gen3 (HDM)
};

class HailortAsyncDenoise
{
  public:
    using OnInferCb = std::function<void(NetworkInferenceBindingsPtr bindings)>;

    HailortAsyncDenoise(const OnInferCb &on_infer_finish);
    virtual ~HailortAsyncDenoise();

    bool set_config(const denoise_config_t &denoise_config, const std::string &group_id, int scheduler_threshold,
                    const std::chrono::milliseconds &scheduler_timeout, int batch_size,
                    bool use_hailort_service = false);
    void wait_for_all_jobs_to_finish();
    bool has_pending_jobs() const;
    bool process(NetworkInferenceBindingsPtr bindings);
    virtual bool is_packed_output() const = 0;
    virtual NetworkInferenceBindingsPtr create_bindings(const denoise_config_t &denoise_config,
                                                        HailoMediaLibraryBufferPtr input_buffer,
                                                        HailoMediaLibraryBufferPtr output_buffer) const = 0;
    virtual int get_denoised_output_index() const = 0;
    virtual media_library_return bind_loopback_buffers(NetworkInferenceBindingsPtr bindings,
                                                       const TensorBindings &loopback_buffers) const = 0;
    virtual HailortAsyncDenoiseType type() const = 0;
    virtual std::string get_network_path(const denoise_config_t &denoise_config) const = 0;

    size_t get_input_frame_size(const std::string &tensor_name) const;
    size_t get_output_frame_size(const std::string &tensor_name) const;
    hailo_3d_image_shape_t get_input_frame_shape(const std::string &tensor_name) const;
    hailo_3d_image_shape_t get_output_frame_shape(const std::string &tensor_name) const;
    hailo_quant_info_t get_output_quant_info(const std::string &tensor_name) const;

  protected:
    OnInferCb m_on_infer_finish;
    std::string m_group_id;
    bool m_use_hailort_service = false;
    int m_scheduler_threshold;
    std::chrono::milliseconds m_scheduler_timeout;
    denoise_config_t m_denoise_config;
    hailort::AsyncInferJob m_last_infer_job;
    std::atomic<uint64_t> m_last_inserted_infer_output_buffer_timestamp = 0;
    std::atomic<uint64_t> m_last_result_infer_output_buffer_timestamp = 0;

    std::string m_current_vdevice_name;
    std::shared_ptr<hailort::VDevice> m_vdevice;
    std::unordered_map<std::string, std::shared_ptr<hailort_configured_device_t>> m_configured_devices;

    // Opaque wrapper for the cached perfetto::NamedTrack — keeps <hailo_perfetto.h> out of this public header.
    struct PerfettoImpl;
    std::unique_ptr<PerfettoImpl> m_perfetto;

    static constexpr std::chrono::seconds WAIT_FOR_LAST_INFER_TIMEOUT{1};
    bool set_input_buffer(int fd, const std::string &tensor_name);
    bool set_input_buffer(HailoMediaLibraryBufferPtr input_buffer, int plane_id, const std::string &buffer_name,
                          const std::string &tensor_name);
    bool set_output_buffer(int fd, const std::string &tensor_name);
    bool set_output_buffer(HailoMediaLibraryBufferPtr output_buffer, int plane_id, const std::string &buffer_name,
                           const std::string &tensor_name);

    bool set_input_buffers(const TensorBindings &inputs, const TensorBindings &gain_inputs,
                           const TensorBindings &skip_inputs);
    bool set_output_buffers(const TensorBindings &outputs);
    bool infer(NetworkInferenceBindingsPtr bindings);

    void set_infer_layers(std::shared_ptr<hailort::InferModel> infer_model, NetworkInferenceBindingsPtr bindings);
};

class HailortAsyncDenoisePostISP final : public HailortAsyncDenoise
{
  public:
    using HailortAsyncDenoise::HailortAsyncDenoise;
    enum InputIndex
    {
        Y_CHANNEL = 0,
        UV_CHANNEL = 1,
        LOOPBACK_Y_CHANNEL = 2,
        LOOPBACK_UV_CHANNEL = 3,
        INPUT_SIZE = 4
    };
    enum OutputIndex
    {
        OUTPUT_Y_CHANNEL = 0,
        OUTPUT_UV_CHANNEL = 1,
        OUTPUT_SIZE = 2
    };
    NetworkInferenceBindingsPtr create_bindings(const denoise_config_t &denoise_config,
                                                HailoMediaLibraryBufferPtr input_buffer,
                                                HailoMediaLibraryBufferPtr output_buffer) const override;
    media_library_return bind_loopback_buffers(NetworkInferenceBindingsPtr bindings,
                                               const TensorBindings &loopback_buffers) const override;
    int get_denoised_output_index() const override;

    static constexpr bool PACKED_OUTPUT = true;
    bool is_packed_output() const override
    {
        return PACKED_OUTPUT;
    }
    HailortAsyncDenoiseType type() const override
    {
        return HailortAsyncDenoiseType::PostISP;
    }

    std::string get_network_path(const denoise_config_t &denoise_config) const override;
};

class HailortAsyncDenoisePreISP : public HailortAsyncDenoise
{
  public:
    using HailortAsyncDenoise::HailortAsyncDenoise;
    enum GainIndex
    {
        DG_GAIN_CHANNEL = 0,
        BLS_CHANNEL = 1,
        GAIN_SIZE = 2
    };
    static bool is_using_dgain_and_bls(const denoise_config_t &denoise_config);

    std::string get_network_path(const denoise_config_t &denoise_config) const override;
};

class HailortAsyncDenoisePreISPVd final : public HailortAsyncDenoisePreISP
{
  public:
    using HailortAsyncDenoisePreISP::HailortAsyncDenoisePreISP;
    enum InputIndex
    {
        BAYER_CHANNEL = 0,
        LOOPBACK_BAYER_CHANNEL = 1,
        INPUT_SIZE = 2
    };
    enum OutputIndex
    {
        OUTPUT_BAYER_CHANNEL = 0,
        OUTPUT_SIZE = 1
    };
    NetworkInferenceBindingsPtr create_bindings(const denoise_config_t &denoise_config,
                                                HailoMediaLibraryBufferPtr input_buffer,
                                                HailoMediaLibraryBufferPtr output_buffer) const override;
    media_library_return bind_loopback_buffers(NetworkInferenceBindingsPtr bindings,
                                               const TensorBindings &loopback_buffers) const override;
    int get_denoised_output_index() const override;

    static constexpr bool PACKED_OUTPUT = true;
    bool is_packed_output() const override
    {
        return PACKED_OUTPUT;
    }
    HailortAsyncDenoiseType type() const override
    {
        return HailortAsyncDenoiseType::PreISPVd;
    }
};

class HailortAsyncDenoisePreISPHdm final : public HailortAsyncDenoisePreISP
{
  public:
    using HailortAsyncDenoisePreISP::HailortAsyncDenoisePreISP;
    enum InputIndex
    {
        BAYER_CHANNEL = 0,
        FUSION_CHANNEL = 1,
        GAMMA_CHANNEL = 2,
        INPUT_SIZE = 3
    };
    enum OutputIndex
    {
        OUTPUT_BAYER_CHANNEL = 0,
        OUTPUT_FUSION_CHANNEL = 1,
        OUTPUT_GAMMA_CHANNEL = 2,
        OUTPUT_SIZE = 3
    };
    enum SkipIndex
    {
        SKIP0_FUSION_CHANNEL = 0,
        SKIP1_FUSION_CHANNEL = 1,
        SKIP_SIZE = 2
    };
    NetworkInferenceBindingsPtr create_bindings(const denoise_config_t &denoise_config,
                                                HailoMediaLibraryBufferPtr input_buffer,
                                                HailoMediaLibraryBufferPtr output_buffer) const override;
    media_library_return bind_loopback_buffers(NetworkInferenceBindingsPtr bindings,
                                               const TensorBindings &loopback_buffers) const override;
    int get_denoised_output_index() const override;

    static constexpr bool PACKED_OUTPUT = false;
    bool is_packed_output() const override
    {
        return PACKED_OUTPUT;
    }
    HailortAsyncDenoiseType type() const override
    {
        return HailortAsyncDenoiseType::PreISPHdm;
    }

    static bool is_using_fusion_skips(const denoise_config_t &denoise_config);
};
