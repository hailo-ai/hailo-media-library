#pragma once

// General includes
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

// Media-Library includes
#include "media_library/buffer_pool.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/sources/file_reader_module.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"

namespace hailo_analytics::pipeline::sources
{

/**
 * @brief Frontend stage that reads video data from a file instead of a camera
 *
 * This class extends FrontendStage to read raw video data from a file and feeds it
 * to the MediaLibrary frontend, which then processes it through the configured pipeline.
 * It inherits all the frontend subscription and stream management functionality from
 * the base FrontendStage class.
 */
class FrontendStageFromFile : public FrontendStage
{
  private:
    std::shared_ptr<FileReader> m_file_reader;
    std::shared_ptr<MediaLibraryBufferPool> m_buffer_pool;
    bool m_feeding_thread_active;
    std::thread m_feeding_thread;

    // File parameters for creating FileReader
    std::string m_file_location;
    size_t m_width;
    size_t m_height;
    double m_fps;
    bool m_loop_enabled;
    size_t m_buffer_pool_size;

    // Pool mode support
    StagePoolMode m_pool_mode;
    std::mutex m_buff_pool_mutex;
    std::condition_variable m_available_buffers_cv;

  public:
    /**
     * @brief Constructor for FrontendStageFromFile
     * @param name Stage name
     * @param file_location Path to the raw video file (NV12 format)
     * @param width Video width in pixels
     * @param height Video height in pixels
     * @param fps Frames per second for playback
     * @param loop_enabled Whether to loop the video when it reaches the end
     * @param queue_size Queue size for the stage
     * @param leaky Whether the queue is leaky
     * @param print_fps Whether to print FPS information
     * @param buffer_pool_size Size of the buffer pool (must be > 0)
     * @param trace_processing_operations Whether to trace processing operations
     */
    FrontendStageFromFile(std::string name, const std::string &file_location, size_t width, size_t height, double fps,
                          bool loop_enabled, size_t queue_size, bool leaky, size_t buffer_pool_size,
                          StagePoolMode pool_mode = StagePoolMode::BLOCKING, bool trace_processing_operations = true);

    /**
     * @brief Create and configure the frontend stage with file input
     * @param frontend MediaLibrary frontend instance
     * @return AppStatus indicating success or failure
     */
    AppStatus create(MediaLibraryFrontend &frontend);

    /**
     * @brief Stop the frontend stage
     * @return AppStatus indicating success or failure
     */
    virtual AppStatus stop() override;

    /**
     * @brief Initialize the frontend stage
     * @return AppStatus indicating success or failure
     */
    AppStatus init() override;

    /**
     * @brief Deinitialize the frontend stage
     * @return AppStatus indicating success or failure
     */
    AppStatus deinit() override;

    /**
     * @brief Configure the frontend stage
     * @param frontend MediaLibrary frontend instance
     * @return AppStatus indicating success or failure
     */
    AppStatus configure(MediaLibraryFrontend &frontend);

  private:
    /**
     * @brief Thread function that reads frames from file and feeds them to the frontend
     */
    void feeding_thread_func();

    /**
     * @brief Setup buffer pool notifications for blocking mode
     */
    void setup_pool_notifications();

    /**
     * @brief Start processing trace if tracing is enabled
     * @param buffer Optional buffer to extract ISP timestamp from
     */
    void trace_processing_start(HailoMediaLibraryBufferPtr buffer = nullptr);

    /**
     * @brief End processing trace if tracing is enabled
     * @param buffer Optional buffer parameter (currently unused)
     */
    void trace_processing_end(HailoMediaLibraryBufferPtr buffer = nullptr);
};

/**
 * @brief Builder class for FrontendStageFromFile
 */
class FrontendStageFromFileBuild : public FrontendStageFromFile
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_file_location;
        std::optional<size_t> m_width;
        std::optional<size_t> m_height;
        std::optional<double> m_fps;
        std::optional<size_t> m_buffer_pool_size; // Now required, no default
        bool m_loop_enabled = true;
        size_t m_queue_size = FRONTEND_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        StagePoolMode m_pool_mode = StagePoolMode::BLOCKING;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_file_location(std::string file_location);
        Builder &set_width(size_t width);
        Builder &set_height(size_t height);
        Builder &set_fps(double fps);
        Builder &set_loop_enabled_opt(bool loop_enabled);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_buffer_pool_size(size_t size);
        Builder &set_pool_mode_opt(StagePoolMode mode);
        Builder &set_trace_opt(bool activate);

        std::shared_ptr<FrontendStageFromFile> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::sources
