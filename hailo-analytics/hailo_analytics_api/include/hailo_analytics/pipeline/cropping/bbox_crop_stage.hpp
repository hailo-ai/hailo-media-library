#pragma once

/**
 * @file bbox_crop_stage.hpp
 * @brief Stage base class that performs bounding box cropping on video frames.
 **/

// Infra includes
#include "hailo_analytics/pipeline/cropping/dsp_cropping.hpp"

namespace hailo_analytics::pipeline::cropping
{

/**
 * @brief Derived class of DspBaseCropStage for handling cropping based on detected bounding boxes.
 */
class BBoxCropStage : public DspBaseCropStage
{
  private:
    std::vector<HailoBBox> m_detection_crops_bbox;   /**< Bounding boxes for detected crops */
    std::vector<HailoROIPtr> m_detection_rois;       /**< ROI pointers for detections */
    std::vector<std::string> m_target_labels;        /**< Target labels for filtering detections */
    bool m_use_letterbox;                            /**< Enable letterbox support */
    dsp_letterbox_alignment_t m_letterbox_alignment; /**< Letterbox alignment */
    dsp_color_t m_letterbox_color;                   /**< Letterbox padding color */
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
     * @param labels Target labels for filtering detections.
     * @param queue_size Size of the queue.
     * @param leaky Boolean flag for leaky behavior.
     * @param trace_processing_operations Boolean flag for tracing processing operations.
     * @param pool_mode Pool mode for buffer management.
     * @param crop_every_x_frames Crop every x frames.
     * @param use_letterbox Enable letterbox support.
     * @param letterbox_alignment Letterbox alignment (only used if use_letterbox=true).
     * @param letterbox_color Letterbox color (only used if use_letterbox=true).
     */
    BBoxCropStage(std::string name, int output_pool_size, int input_width, int input_height, int output_width,
                  int output_height, std::string main_sub_name, std::string sub_sub_name,
                  std::vector<std::string> labels, size_t queue_size, bool leaky = false,
                  bool trace_processing_operations = true, StagePoolMode pool_mode = StagePoolMode::FAIL_ON_EMPTY_POOL,
                  size_t crop_every_x_frames = 1, bool use_letterbox = false,
                  dsp_letterbox_alignment_t letterbox_alignment = DSP_LETTERBOX_MIDDLE,
                  dsp_color_t letterbox_color = DEFAULT_LETTERBOX_COLOR);

    /**
     * @brief Initializes the buffer pool.
     * @return Status of the operation.
     */
    AppStatus init() override;

    /**
     * @brief Prepares crop dimensions based on detected bounding boxes.
     * @param input_buffer Input buffer.
     * @param crop_resize_dims Vector to store crop dimensions.
     */
    void prepare_crops(BufferPtr input_buffer, std::vector<dsp_crop_api_t> &crop_resize_dims) override;

    /**
     * @brief Gets the bounding box for a specific detection.
     * @param index Index of the detection.
     * @return Bounding box of the detection.
     */
    HailoBBox get_crop_bbox(int index) override;

    /**
     * @brief Gets the ROI for a specific detection.
     * @param index Index of the detection.
     * @return ROI of the detection.
     */
    HailoROIPtr get_crop_roi(int index) override;

    /**
     * @brief Performs pre-processing before cropping.
     * @param input_buffer Input buffer.
     */
    void pre_crop(BufferPtr input_buffer) override;

    /**
     * @brief Clears detection data after cropping.
     * @param input_buffer Input buffer.
     */
    void post_crop(BufferPtr input_buffer) override;

    /**
     * @brief Override process to add letterbox support when enabled.
     * @param data Input buffer to process.
     * @return Status of the operation.
     */
    AppStatus process(BufferPtr data) override;
};

/**
 * @brief Builder pattern implementation for BBoxCropStage
 *
 * Provides a fluent interface for constructing BBoxCropStage instances
 * with configurable parameters.
 */
class BBoxCropStageBuild : public BBoxCropStage
{
  public:
    /**
     * @brief Builder class for constructing BBoxCropStage instances
     */
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        int m_output_pool_size = -1;
        int m_input_width = -1;
        int m_input_height = -1;
        int m_output_width = -1;
        int m_output_height = -1;
        std::optional<std::string> m_main_sub_name;
        std::optional<std::string> m_sub_sub_name;
        std::optional<std::vector<std::string>> m_labels;

        size_t m_queue_size = 10;
        bool m_leaky = false;
        bool m_trace = true;
        StagePoolMode m_pool_mode = StagePoolMode::FAIL_ON_EMPTY_POOL;
        size_t m_crop_every_x_frames = 1;
        bool m_use_letterbox = false;
        dsp_letterbox_alignment_t m_letterbox_alignment = DSP_LETTERBOX_MIDDLE;
        dsp_color_t m_letterbox_color = DEFAULT_LETTERBOX_COLOR;

      public:
        /**
         * @brief Set the stage name
         * @param name Name for the stage
         * @return Builder reference for chaining
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Set the output pool size
         * @param size Size of the output buffer pool
         * @return Builder reference for chaining
         */
        Builder &set_output_pool_size(int size);

        /**
         * @brief Set the input width
         * @param size Width of the input data in pixels
         * @return Builder reference for chaining
         */
        Builder &set_input_width(int size);

        /**
         * @brief Set the input height
         * @param size Height of the input data in pixels
         * @return Builder reference for chaining
         */
        Builder &set_input_height(int size);

        /**
         * @brief Set the output width
         * @param size Width of the output data in pixels
         * @return Builder reference for chaining
         */
        Builder &set_output_width(int size);

        /**
         * @brief Set the output height
         * @param size Height of the output data in pixels
         * @return Builder reference for chaining
         */
        Builder &set_output_height(int size);

        /**
         * @brief Set the main subscriber name
         * @param name Name of the main subscriber
         * @return Builder reference for chaining
         */
        Builder &set_main_sub_name(std::string name);

        /**
         * @brief Set the sub-subscriber name
         * @param name Name of the sub-subscriber
         * @return Builder reference for chaining
         */
        Builder &set_sub_sub_name(std::string name);

        /**
         * @brief Set the target labels for filtering detections
         * @param labels Vector of label strings
         * @return Builder reference for chaining
         */
        Builder &set_labels(std::vector<std::string> labels);

        /**
         * @brief Set the queue size
         * @param size Size of the processing queue
         * @return Builder reference for chaining
         */
        Builder &set_queue_size(size_t size);

        /**
         * @brief Set the leaky option
         * @param activate If true, queue drops old frames when full
         * @return Builder reference for chaining
         */
        Builder &set_leaky_opt(bool activate);

        /**
         * @brief Set the trace option
         * @param activate If true, enables tracing for processing operations
         * @return Builder reference for chaining
         */
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Set the pool mode
         * @param mode Pool mode for buffer management
         * @return Builder reference for chaining
         */
        Builder &set_pool_mode_opt(StagePoolMode mode);

        /**
         * @brief Set the crop frequency
         * @param crop_every_x_frames Crop every x frames (1 = every frame)
         * @return Builder reference for chaining
         */
        Builder &set_crop_every_x_frames(size_t crop_every_x_frames);

        /**
         * @brief Enable letterbox mode with alignment and color
         * @param alignment Letterbox alignment (default: DSP_LETTERBOX_MIDDLE)
         * @param color Letterbox padding color (default: black)
         * @return Builder reference for chaining
         */
        Builder &set_letterbox_opt(dsp_letterbox_alignment_t alignment = DSP_LETTERBOX_MIDDLE,
                                   dsp_color_t color = DEFAULT_LETTERBOX_COLOR);

        /**
         * @brief Build and return shared pointer to BBoxCropStage
         * @return Shared pointer to constructed BBoxCropStage
         * @throws std::runtime_error if required parameters are missing
         */
        std::shared_ptr<BBoxCropStage> buildptr() const;
    };

    /**
     * @brief Create a new Builder instance
     * @return Builder instance for constructing BBoxCropStage
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::cropping
