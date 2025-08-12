#pragma once

#include "hdr_manager.hpp"

#include "hrt_stitcher/hrt_stitcher.hpp"
#include "dma_buffer.hpp"
#include "video_buffer.hpp"
#include "video_device.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"

class HdrManager::Impl
{
    enum class SupportedResolution
    {
        RES_4K,
        RES_FHD,
        RES_4MP,
        RES_UNSUPPORTED
    };
    struct StitchContext
    {
        HDR::VideoBuffer *m_raw_buffer;
        HDR::VideoBuffer *m_stitched_buffer;
        HDR::DMABuffer m_wb_buffer;
        volatile bool m_in_use;
    };

    static constexpr int SCHEDULER_THRESHOLD = 1;
    static constexpr std::chrono::milliseconds SCHEDULER_TIMEOUT{1000};
    static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;

    static constexpr int STITCHED_PLANE_COUNT = 1;
    static constexpr int RAW_CAPTURE_BUFFERS_COUNT = 5;
    static constexpr int ISP_IN_BUFFERS_COUNT = 3;
    static constexpr const char *RAW_CAPTURE_PATH = "/dev/video2";
    static constexpr const char *ISP_IN_PATH = "/dev/video3";
    static constexpr const char *YUV_STREAM_DEVICE_PATH = "/dev/video0";
    static constexpr const char *DMA_HEAP_PATH = "/dev/dma_heap/linux,cma";
    static constexpr int RAW_CAPTURE_DEFAULT_FPS = 20;
    static constexpr float WB_COMPENSATION = 0.03143406;
    static constexpr int CFA_NUM_CHANNELS = 4;
    static constexpr std::pair<int, size_t> DEFAULT_PIXEL_FORMAT = {V4L2_PIX_FMT_SRGGB12, 16};
    static constexpr int VIDEO_WAIT_FOR_STREAM_START = _IO('D', BASE_VIDIOC_PRIVATE + 3);
    static constexpr int STITCH_MODE = 2;

    HDR::DMABufferAllocator m_allocator;
    std::unique_ptr<HDR::VideoDevice> m_isp_in_device;
    std::unique_ptr<HDR::VideoDevice> m_raw_capture_device;
    HailortAsyncStitching m_stitcher;
    std::vector<StitchContext> m_stitch_contexts;
    bool m_initialized = false;
    float m_ls_ratio = -1;
    float m_vs_ratio = -1;
    int m_dol;
    int m_isp_fd;
    int m_wb_buffer_size;
    std::mutex m_change_state_mutex;
    std::thread m_hdr_thread;
    std::unordered_map<std::string, struct v4l2_query_ext_ctrl> m_ctrl_map;
    volatile bool m_running = false;
    bool m_wb_clipping_warned = false;
    std::optional<HDR::DOL> get_dol(hdr_dol_t dol);
    static std::optional<HDR::InputResolution> get_input_resolution(const output_resolution_t &input_resolution);
    std::optional<std::string> get_hdr_hef_path(HDR::DOL dol, HDR::InputResolution resolution);

    void on_infer(void *ptr);

    void hdr_loop();
    void wait_for_yuv_stream_start();
    bool update_wb_gains(HDR::DMABuffer &dma_wb_buffer);
    bool set_ratio();
    bool alloc_stitch_contexts();
    void free_stitch_contexts();
    bool get_stitch_context(StitchContext **context);
    void put_stitch_context(StitchContext *context);
    bool is_supported_format(int fmt);

  public:
    Impl();
    ~Impl();

    bool init(const frontend_config_t &frontend_config);
    bool start();
    void stop();
    void deinit();

    static inline int get_stitch_mode()
    {
        return STITCH_MODE;
    }
    static bool is_resolution_supported(const output_resolution_t &resolution);
    static bool is_dol_supported(hdr_dol_t dol);
};
