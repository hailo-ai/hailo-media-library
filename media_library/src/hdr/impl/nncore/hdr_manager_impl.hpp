#pragma once

#include "hdr_manager.hpp"

#include "dma_buffer.hpp"
#include "sensor_types.hpp"
#include "files_utils.hpp"
#include "hrt_stitcher/hrt_stitcher.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "video_buffer.hpp"
#include "video_device.hpp"
#include "v4l2_ctrl.hpp"

class HdrManager::Impl
{
    struct StitchContext
    {
        HDR::VideoBuffer *m_raw_buffer;
        HDR::VideoBuffer *m_stitched_buffer;
        HDR::DMABuffer m_wb_buffer;
        volatile bool m_in_use;
    };
    typedef std::shared_ptr<StitchContext> StitchContextPtr;

    static constexpr int SCHEDULER_THRESHOLD = 1;
    static constexpr std::chrono::milliseconds SCHEDULER_TIMEOUT{1000};
    static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;

    static constexpr int STITCHED_PLANE_COUNT = 1;
    static constexpr int RAW_CAPTURE_BUFFERS_COUNT = 5;
    static constexpr int ISP_IN_BUFFERS_COUNT = 3;
    static constexpr const char *ISP_IN_PATH = "/dev/video10";
    static constexpr const char *DMA_HEAP_PATH = "/dev/dma_heap/linux,cma";
    static constexpr int RAW_CAPTURE_DEFAULT_FPS = 20;
    static constexpr float WB_COMPENSATION = 0.03143406;
    static constexpr int CFA_NUM_CHANNELS = 4;
    static constexpr int VIDEO_WAIT_FOR_STREAM_START = _IO('D', BASE_VIDIOC_PRIVATE + 3);
    static constexpr int STITCH_MODE = 2;

    std::unique_ptr<HDR::VideoOutputDevice> m_isp_in_device;
    std::unique_ptr<HDR::VideoCaptureDevice> m_raw_capture_device;
    std::unique_ptr<HailortAsyncStitching> m_stitcher;
    std::vector<StitchContextPtr> m_stitch_contexts;
    files_utils::SharedFd m_isp_fd;
    bool m_initialized = false;
    float m_ls_ratio = -1;
    float m_vs_ratio = -1;
    int m_dol;
    std::mutex m_change_state_mutex;
    std::thread m_hdr_thread;
    std::unordered_map<std::string, struct v4l2_query_ext_ctrl> m_ctrl_map;
    volatile bool m_running = false;
    bool m_wb_clipping_warned = false;
    std::shared_ptr<v4l2::v4l2ControlManager> m_v4l2_ctrl_manager;
    std::optional<std::string> get_hdr_hef_path(hdr_dol_t dol, Resolution resolution);

    std::atomic<size_t> m_infer_jobs_contexts_queue_size = 0;

    void on_infer(std::shared_ptr<void> ptr);

    void hdr_loop();
    void wait_for_yuv_stream_start();
    bool update_wb_gains(HDR::DMABuffer &dma_wb_buffer);
    bool set_ratio();
    static std::optional<std::vector<StitchContextPtr>> alloc_stitch_contexts(HDR::DMABufferAllocator &allocator,
                                                                              int wb_buffer_size);
    void free_stitch_contexts();
    std::optional<StitchContextPtr> get_stitch_context();
    void put_stitch_context(StitchContextPtr context);
    bool is_supported_format(int fmt);

  public:
    Impl(std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager);
    ~Impl();

    bool init(const frontend_config_t &frontend_config);
    bool start();
    void stop();
    void deinit();

    static inline int get_stitch_mode()
    {
        return STITCH_MODE;
    }

    inline std::shared_ptr<v4l2::v4l2ControlManager> get_v4l2_ctrl_manager()
    {
        return m_v4l2_ctrl_manager;
    }

    static bool is_dol_supported(hdr_dol_t dol);
};
