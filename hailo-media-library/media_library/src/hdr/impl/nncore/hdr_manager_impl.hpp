#pragma once

#include <asm/ioctl.h>
#include <linux/videodev2.h>
#include <stddef.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "buffer_pool.hpp"
#include "hdr_manager.hpp"
#include "dma_buffer.hpp"
#include "isp_manager.hpp"
#include "sensor_types.hpp"
#include "hrt_stitcher/hrt_stitcher.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"

class HdrManager::Impl
{
    struct StitchContext
    {
        HailoMediaLibraryBufferPtr m_stitched_buffer;
        HDR::DMABuffer m_wb_buffer;
        volatile bool m_in_use;
    };
    typedef std::shared_ptr<StitchContext> StitchContextPtr;

    IspManager &m_isp_manager;
    static constexpr int SCHEDULER_THRESHOLD = 1;
    static constexpr std::chrono::milliseconds SCHEDULER_TIMEOUT{1000};
    static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;

    static constexpr int RAW_CAPTURE_DEFAULT_FPS = 20;
    static constexpr float WB_COMPENSATION = 0.03143406;
    static constexpr int CFA_NUM_CHANNELS = 4;
    static constexpr int VIDEO_WAIT_FOR_STREAM_START = _IO('D', BASE_VIDIOC_PRIVATE + 3);
    static constexpr const char *DMA_HEAP_PATH = "/dev/dma_heap/linux,cma";

    bool m_initialized = false;
    Resolution m_current_resolution;
    std::unique_ptr<HailortAsyncStitching> m_stitcher;
    std::vector<StitchContextPtr> m_stitch_contexts;
    int m_dol;
    output_resolution_t m_input_resolution;
    std::mutex m_change_state_mutex;
    bool m_wb_clipping_warned = false;
    std::optional<std::string> get_hdr_hef_path(hdr_dol_t dol, Resolution resolution);

    std::atomic<size_t> m_infer_jobs_contexts_queue_size = 0;

    void on_infer(std::shared_ptr<void> ptr);

    bool handle_frame(HailoMediaLibraryBufferPtr raw_buffer);
    void wait_for_yuv_stream_start();
    bool update_wb_gains(HDR::DMABuffer &dma_wb_buffer);
    std::optional<std::vector<StitchContextPtr>> alloc_stitch_contexts(HDR::DMABufferAllocator &allocator,
                                                                       int wb_buffer_size);
    void free_stitch_contexts();
    std::optional<StitchContextPtr> get_stitch_context();
    void mark_stitch_context_unused(StitchContextPtr context);
    bool is_supported_format(int fmt);

  public:
    Impl(IspManager &isp_manager);
    ~Impl();

    bool configure(const frontend_config_t &frontend_config);
    void deinit();

    static StitchMode get_stitch_mode();

    static bool is_dol_supported(hdr_dol_t dol);
};
