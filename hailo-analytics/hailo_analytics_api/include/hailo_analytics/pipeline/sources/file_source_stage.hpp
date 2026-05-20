#pragma once

// Media-Library includes
#include "media_library/buffer_pool.hpp"
#include "media_library/dma_memory_allocator.hpp"

// Postporcess Tools includes
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// Infra includes
#include "hailo_analytics/pipeline/sources/file_reader_module.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"

namespace hailo_analytics::pipeline::sources
{

using EosCallback = std::function<void()>;

class FileSourceStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    std::shared_ptr<FileReader> m_file_reader;
    std::shared_ptr<MediaLibraryBufferPool> m_buffer_pool;

    // File parameters for creating FileReader
    std::string m_file_location;
    size_t m_width;
    size_t m_height;
    double m_fps;
    bool m_loop_enabled;
    size_t m_buffer_pool_size;

    // Optional callback invoked when the source reaches end-of-stream
    EosCallback m_eos_callback = nullptr;

  public:
    /**
     * @brief Constructor for FileSourceStage with file configuration
     * @param name Stage name
     * @param file_location Path to the raw video file (NV12 format)
     * @param width Video width in pixels
     * @param height Video height in pixels
     * @param fps Frames per second for playback
     * @param loop_enabled Whether to loop the video when it reaches the end
     * @param queue_size Queue size for the stage
     * @param leaky Whether the queue is leaky
     * @param buffer_pool_size Size of the buffer pool
     * @param trace_processing_operations Whether to trace processing operations
     */
    FileSourceStage(std::string name, const std::string &file_location, size_t width, size_t height, double fps,
                    bool loop_enabled, size_t queue_size, bool leaky, size_t buffer_pool_size,
                    bool trace_processing_operations = true);

    /**
     * @brief Initialize the file source stage
     * @return AppStatus indicating success or failure
     */
    AppStatus init() override;

    /**
     * @brief Deinitialize the file source stage
     * @return AppStatus indicating success or failure
     */
    AppStatus deinit() override;

    /**
     * @brief Main loop that reads frames from file and sends them to subscribers
     */
    void loop() override;

    /**
     * @brief Set a callback to be invoked when the source reaches end-of-stream.
     * @param callback Function to call on EOS (called from the source thread)
     */
    void set_eos_callback(EosCallback callback);

  private:
    /**
     * @brief Start processing trace if tracing is enabled
     * @param buffer Optional buffer to extract ISP timestamp from
     */
    void trace_processing_start(BufferPtr buffer = nullptr);

    /**
     * @brief End processing trace if tracing is enabled
     * @param buffer Optional buffer parameter (currently unused)
     */
    void trace_processing_end(BufferPtr buffer = nullptr);
};

class FileSourceStageBuild : public FileSourceStage
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
        size_t m_queue_size = 10;
        bool m_loop = true;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_file_location(std::string file_location);
        Builder &set_width(size_t width);
        Builder &set_height(size_t height);
        Builder &set_fps(double fps);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_buffer_pool_size(size_t size);
        Builder &set_trace_opt(bool activate);
        Builder &set_loop_opt(bool loop);

        std::shared_ptr<FileSourceStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::sources
