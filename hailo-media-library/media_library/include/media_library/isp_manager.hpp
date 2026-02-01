/*
 * Copyright (c) 2017-2025 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once
#include <atomic>
#include <optional>

#include "buffer_pool.hpp"
#include "config_attacher.hpp"
#include "config_manager.hpp"
#include "files_utils.hpp"
#include "isp_utils.hpp"
#include "media_library_types.hpp"
#include "v4l2_ctrl.hpp"
#include "video_device.hpp"

class IspManager
{
  public:
    enum class Mode
    {
        UNKNOWN,
        SDR,
        HDR_NNCORE_STITCH,
        HDR_ISP_STITCH,
        PRE_ISP_DENOISE,
        HDR_DENOISE
    };

    static inline const char *to_string(Mode mode)
    {
        switch (mode)
        {
        case Mode::UNKNOWN:
            return "UNKNOWN";
        case Mode::SDR:
            return "SDR";
        case Mode::HDR_NNCORE_STITCH:
            return "HDR_NNCORE_STITCH";
        case Mode::HDR_ISP_STITCH:
            return "HDR_ISP_STITCH";
        case Mode::PRE_ISP_DENOISE:
            return "PRE_ISP_DENOISE";
        default:
            return "INVALID_MODE";
        }
    }

    enum class BufferType
    {
        RAW_CAPTURE,
        ISP_INPUT
    };

    enum class FastToggleMode
    {
        OFF = 0,
        SDR_TO_HDR = 1,
        HDR_TO_SDR = 2,
    };

  private:
    float m_ls_ratio;
    float m_vs_ratio;

    std::atomic<bool> m_loop_running;
    std::thread m_get_raw_buffers_loop_thread;

    std::atomic<Mode> m_current_mode;
    std::mutex m_mode_mutex;
    std::condition_variable m_mode_cv;
    files_utils::SharedFd m_isp_out_fd;

    size_t m_currently_used_raw_capture_frames = 0;
    size_t m_currently_used_isp_input_frames = 0;

    output_resolution_t m_input_resolution;

    bool m_profile_based_mode;
    std::atomic<FastToggleMode> m_fast_toggle_mode;

    bool m_is_started = false;

    std::unique_ptr<HDR::VideoCaptureDevice> m_raw_capture_device; // unique ptr due to ioctls in d'tor in current impl
    std::unique_ptr<HDR::VideoOutputDevice> m_isp_in_device;
    HDR::DMABufferAllocator m_allocator;
    std::unique_ptr<ConfigAttacher> m_config_attacher;
    const ConfigManagerInteractor *m_config_manager_interactor;

    using RawBufferSubscriberCallback = std::function<void(HailoMediaLibraryBufferPtr)>;
    using SubscriberId = int;
    std::map<SubscriberId, RawBufferSubscriberCallback> m_raw_buffer_subscribers;
    v4l2::IspCtrl get_mcm_ctrl_type();
    v4l2::ImxCtrl get_wdr_ctrl_type();
    v4l2::CsiCtrl get_csi_ctrl_type();
    bool set_custom_rhs1_from_profile();

    bool init_isp_out_device();
    bool mode_has_raw_video_source(Mode mode);
    bool set_mcm_mode(Mode mode, std::optional<bool> is_input_packed = std::nullopt);
    void pull_raw_video_buffers_loop();
    bool wait_for_raw_video_source();
    HailoMediaLibraryBufferPtr hailo_buffer_from_isp_buffer(BufferType buffer_type, HDR::VideoBuffer *video_buffer,
                                                            bool is_packed_isp_input);

    bool is_current_mode_using_pre_isp_pipeline();
    bool is_current_mode_hdr();

    bool switch_to_sdr();
    bool switch_to_hdr(const frontend_config_t &frontend_config);
    bool set_hdr_ratios(float ls_ratio, float vs_ratio);
    bool switch_to_pre_isp_denoise(const frontend_config_t &frontend_config);
    bool switch_to_hdr_denoise(const frontend_config_t &frontend_config);
    bool setup_raw_capture_device(const frontend_config_t &frontend_config);
    bool setup_isp_input_device(const frontend_config_t &frontend_config);
    bool fast_toggle_ctrl();
    static size_t get_num_of_exposures(const frontend_config_t &frontend_config);
    static std::optional<uint32_t> get_packed_pixel_format(uint32_t sensor_pixel_format);

    static constexpr size_t RAW_CAPTURE_BITS_PER_PIXEL = 16;
    static constexpr size_t ISP_INPUT_BITS_PER_PACKED_PIXEL = 12;
    static constexpr size_t ISP_INPUT_BITS_PER_PADDED_PIXEL = 16;
    static constexpr int RAW_CAPTURE_DEFAULT_FPS = 30;
    static constexpr size_t BITS_PER_PADDED_PIXEL = 16;
    static constexpr size_t BITS_PER_PACKED_PIXEL = 12;
    static constexpr const char *ISP_IN_PATH = "/dev/video10";
    static constexpr const char *DMA_HEAP_PATH = "/dev/dma_heap/linux,cma";
    static constexpr int VIDEO_WAIT_FOR_STREAM_START = _IO('D', BASE_VIDIOC_PRIVATE + 3);

    struct ModePair
    {
        IspManager::Mode from;
        IspManager::Mode to;

        bool operator==(const ModePair &other) const;
    };

    struct ModePairHash
    {
        std::size_t operator()(const ModePair &pair) const;
    };

    const std::unordered_map<ModePair, IspManager::FastToggleMode, ModePairHash> fast_toggle_mode_map = {
        {{IspManager::Mode::SDR, IspManager::Mode::HDR_NNCORE_STITCH}, IspManager::FastToggleMode::SDR_TO_HDR},
        {{IspManager::Mode::HDR_NNCORE_STITCH, IspManager::Mode::SDR}, IspManager::FastToggleMode::HDR_TO_SDR}};

    FastToggleMode get_fast_toggle_mode(Mode switch_to_mode);
    bool prepare_for_fast_toggle(FastToggleMode switch_to_mode);
    bool remove_current_isp_config_files_symlinks();
    bool restore_isp_config_files_to_default();

  public:
    static constexpr int RAW_CAPTURE_BUFFERS_COUNT = 5;
    static constexpr int ISP_IN_BUFFERS_COUNT = 3;

    IspManager();
    ~IspManager();

    v4l2::v4l2ControlManager m_v4l2_ctrl_manager;

    bool set_subscriber(int module_id, RawBufferSubscriberCallback &&callback);
    void unset_subscriber(int module_id);
    std::optional<int> get_raw_capture_pix_format();
    bool modify_isp_config_files();

    void set_config_manager_interactor(const ConfigManagerInteractor *config_manager_interactor,
                                       bool profile_based_mode);
    bool set_config(const frontend_config_t &config);
    bool start();
    void stop();
    void deinit();
    bool put_buffer_into_isp(HailoMediaLibraryBufferPtr hailo_buffer);
    std::optional<HailoMediaLibraryBufferPtr> get_isp_input_buffer(bool is_packed_isp_input = false);
    static bool is_fast_toggle_supported();
    static bool is_input_isp_frame_packed(const frontend_config_t &frontend_config);
};
