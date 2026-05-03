#pragma once

#include <chrono>
#include <fstream>
#include <memory>
#include <string>

// Media library includes
#include "gsthailobuffermeta.hpp"
#include "media_library/buffer_pool.hpp"
#include "media_library/media_library_types.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::sources
{

/**
 * @brief Common file reader module utility for reading NV12 video files
 *
 * This class encapsulates the logic for reading raw video files frame by frame,
 * managing file validation, looping, and frame timing. It can be used by both
 * FileSourceStage and FrontendStageFromFile to avoid code duplication.
 * Buffer pool management is handled by the stages that use this FileReader.
 */
class FileReader
{
  private:
    std::string m_file_location;
    size_t m_width;
    size_t m_height;
    size_t m_frame_size;
    size_t m_y_plane_size;
    size_t m_uv_plane_size;
    size_t m_total_frames;
    size_t m_current_frame_index;
    size_t m_total_frames_processed; // Total frames processed including loops (for PTS calculation)
    bool m_loop_enabled;
    double m_fps;
    std::chrono::milliseconds m_frame_interval;

    std::ifstream m_file_stream;
    std::string m_name;

  public:
    /**
     * @brief Constructor for FileReader
     * @param name Name for logging purposes
     * @param file_location Path to the raw video file (NV12 format)
     * @param width Video width in pixels
     * @param height Video height in pixels
     * @param fps Frames per second for playback timing
     * @param loop_enabled Whether to loop the video when it reaches the end
     */
    FileReader(const std::string &name, const std::string &file_location, size_t width, size_t height, double fps,
               bool loop_enabled);

    /**
     * @brief Destructor - automatically cleans up resources
     */
    ~FileReader();

    /**
     * @brief Initialize the file reader
     * @return AppStatus indicating success or failure
     */
    AppStatus init();

    /**
     * @brief Deinitialize the file reader
     * @return AppStatus indicating success or failure
     */
    AppStatus deinit();

    /**
     * @brief Read the next frame from the file into a buffer
     * @param buffer Buffer to read the frame into (must be pre-allocated)
     * @return True if frame was read successfully, false if end of file (and no loop)
     */
    bool read_next_frame(HailoMediaLibraryBufferPtr buffer);

    /**
     * @brief Get the frame interval for timing purposes
     * @return Frame interval in milliseconds
     */
    std::chrono::milliseconds get_frame_interval() const;

    /**
     * @brief Get the total number of frames in the file
     * @return Total frame count
     */
    size_t get_total_frames() const;

    /**
     * @brief Get the current frame index
     * @return Current frame index
     */
    size_t get_current_frame_index() const;

    /**
     * @brief Check if looping is enabled
     * @return True if looping is enabled
     */
    bool is_loop_enabled() const;

    /**
     * @brief Get the configured FPS
     * @return Frames per second
     */
    double get_fps() const;

    /**
     * @brief Reset to the beginning of the file
     */
    void reset();

  private:
    /**
     * @brief Calculate frame size based on width and height (assuming NV12 format)
     * @return Frame size in bytes
     */
    size_t calculate_frame_size() const;

    /**
     * @brief Validate that the file exists and has the expected size
     * @return True if file is valid, false otherwise
     */
    bool validate_file();
};

} // namespace hailo_analytics::pipeline::sources
