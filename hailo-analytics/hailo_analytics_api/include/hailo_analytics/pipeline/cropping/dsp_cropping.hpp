#pragma once

/**
 * @file dsp_cropping.hpp
 * @brief Stage base class that performs digital signal processing (DSP) cropping on video frames.
 **/

// General includes
#include <algorithm>

// Postprocess Tools includes
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// Media library includes
#include "media_library/dsp_utils.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::cropping
{

class DspBaseCropStage : public hailo_analytics::pipeline::ThreadedStage
{
  protected:
    MediaLibraryBufferPoolPtr m_buffer_pool; /**< Buffer pool for managing media library buffers */
    int m_output_pool_size;                  /**< Size of the output buffer pool */
    int m_input_width;                       /**< Width of the input data */
    int m_input_height;                      /**< Height of the input data */
    int m_output_width;                      /**< Width of the output data */
    int m_output_height;                     /**< Height of the output data */

    std::string m_main_subscriber; /**< Name of the main subscriber */
    std::string m_sub_subscriber;  /**< Name of the sub-subscriber */
    std::condition_variable m_available_buffers_cv;
    std::mutex m_buff_pool_mutex;

    StagePoolMode m_pool_mode;         //< Pool mode for the buffer pool used in this stage
    int m_crop_every_x_frames;         // Crop every n frames (default 1)
    int m_frame_counter;               // Internal frame counter
    dsp_scaling_mode_t m_scaling_mode; // Scaling mode (letterbox or stretch)
    dsp_color_t m_letterbox_color;     // Letterbox padding color

  public:
    /**
     * @brief Constructor to initialize the stage with specified parameters.
     * @param name Name of the stage.
     * @param output_pool_size Size of the output buffer pool.
     * @param input_width Width of the input data.
     * @param input_height Height of the input data.
     * @param output_width Width of the output data.
     * @param output_height Height of the output data.
     * @param main_sub_name Name of the main subscriber.
     * @param sub_sub_name Name of the sub-subscriber.
     * @param queue_size Size of the queue.
     * @param leaky Boolean flag for leaky behavior.
     * @param print_fps Boolean flag for printing FPS.
     * @param crop_every_n_frames Crop every n frames (default 1)
     */
    DspBaseCropStage(std::string name, int output_pool_size, int input_width, int input_height, int output_width,
                     int output_height, std::string main_sub_name, std::string sub_sub_name, size_t queue_size,
                     bool leaky = false, bool trace_processing_operations = true,
                     StagePoolMode pool_mode = StagePoolMode::FAIL_ON_EMPTY_POOL, size_t crop_every_x_frames = 1,
                     dsp_scaling_mode_t scaling_mode = DSP_SCALING_MODE_STRETCH,
                     dsp_color_t letterbox_color = {.y = 0, .u = 128, .v = 128});

    /**
     * @brief Prepares cropping dimensions for a single bounding box.
     * @param bbox Bounding box for cropping.
     * @param crop_resize_dims Vector to store crop resize dimensions.
     */
    virtual void prepare_single_crop_dim(HailoBBox bbox, std::vector<dsp_crop_api_t> &crop_resize_dims);

    /**
     * @brief Prepares crop dimensions for the input buffer.
     * @param input_buffer Input buffer.
     * @param crop_resize_dims Vector to store crop dimensions.
     */
    virtual void prepare_crops(BufferPtr input_buffer, std::vector<dsp_crop_api_t> &crop_resize_dims) = 0;

    /**
     * @brief Gets the bounding box for a specific crop.
     * @param index Index of the crop.
     * @return Bounding box of the crop.
     */
    virtual HailoBBox get_crop_bbox(int index) = 0;

    /**
     * @brief Performs post-processing after cropping.
     * @param input_buffer Input buffer.
     */
    virtual void post_crop(BufferPtr input_buffer) = 0;

    /**
     * @brief Performs pre-processing before cropping.
     * @param input_buffer Input buffer.
     */
    virtual void pre_crop(BufferPtr input_buffer) = 0;

    /**
     * @brief Gets the ROI for a specific crop.
     * @param index Index of the crop.
     * @return ROI of the crop.
     */
    virtual HailoROIPtr get_crop_roi(int index) = 0;

    /**
     * @brief Processes the data buffer, performing cropping and resizing.
     * @param data Data buffer.
     * @return Status of the operation.
     */
    AppStatus process(BufferPtr data) override;
};

} // namespace hailo_analytics::pipeline::cropping
