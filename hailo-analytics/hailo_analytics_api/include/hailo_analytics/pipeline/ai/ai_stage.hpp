#pragma once

/**
 * @file ai_stage.hpp
 * @brief Stage that performs AI inference on video frames.
 **/

// General includes
#include <atomic>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

// HailoRT includes
#include "hailo/hailort.hpp"

// Postprocess Tools includes
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"

// Media library includes
#include "media_library/media_library_types.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::ai
{

/**
 * @brief Class representing an asynchronous HailoRT stage in a connected stage pipeline.
 *
 * This class is responsible for managing the HailoRT inference model, setting up
 * buffer pools, and executing asynchronous inference jobs.
 */
class HailortAsyncStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    // pool members
    int m_output_pool_size; ///< Size of the output buffer pool.
    std::unordered_map<std::string, MediaLibraryBufferPoolPtr>
        m_tensor_buffer_pools; ///< Buffer pools for each output tensor.

    // hailort members
    std::unique_ptr<hailort::VDevice> m_vdevice;            ///< HailoRT virtual device.
    std::shared_ptr<hailort::InferModel> m_infer_model;     ///< HailoRT inference model.
    hailort::ConfiguredInferModel m_configured_infer_model; ///< Configured HailoRT inference model.
    hailort::ConfiguredInferModel::Bindings m_bindings;     ///< Bindings for connecting buffers to the inference model.
    std::unordered_map<std::string, hailo_vstream_info_t> m_vstream_infos; ///< Information about each virtual stream.
    std::shared_ptr<hailort::AsyncInferJob> m_last_infer_job; ///< Pointer to the last asynchronous inference job.

    // network members
    std::string m_hef_path;                            ///< Path to the Hailo Execution File (HEF).
    std::string m_group_id;                            ///< Group ID for the HailoRT device.
    bool m_use_hailort_service;                        ///< Whether to use hailort_server service mode.
    int m_batch_size;                                  ///< Batch size for inference.
    int m_scheduler_threshold;                         ///< Threshold for the scheduler.
    bool m_dynamic_threshold;                          ///< Whether to use dynamic thresholding.
    float32_t m_nms_score_threshold;                   ///< NMS score threshold for filtering detections.
    std::vector<bool> m_nms_classes_filter_mask;       ///< NMS classes filter mask (false at index i filters class i).
    size_t m_nms_max_accumulated_mask_size_multiplier; ///< NMS max accumulated mask size multiplier (0 = no change).
    std::chrono::milliseconds m_scheduler_timeout;     ///< Timeout for the scheduler.

    std::atomic<size_t> m_active_jobs;        ///< Number of active inference jobs.
    size_t m_jobs_limit;                      ///< Limit on the number of active inference jobs.
    std::mutex m_active_jobs_mutex;           ///< Mutex for the active jobs counter.
    std::condition_variable m_active_jobs_cv; ///< Condition variable for the active jobs counter.
    std::condition_variable m_available_buffers_cv;
    std::mutex m_buff_pool_mutex;
    StagePoolMode m_pool_mode; //< Pool mode for the buffer pool used in this stage

    std::string generate_unique_timestamp_str(BufferPtr data);
    uint64_t get_unique_buffer_identifier(BufferPtr data);
    void inference_tracing_begin(BufferPtr data);
    void inference_tracing_end(BufferPtr data);
    void setup_pool_notifications();

  public:
    /**
     * @brief Construct a new HailortAsyncStage object.
     *
     * @param name Name of the stage.
     * @param hef_path Path to the Hailo Execution File (HEF).
     * @param queue_size Size of the processing queue.
     * @param output_pool_size Size of the output buffer pool.
     * @param group_id Group ID for the HailoRT device.
     * @param batch_size Batch size for inference.
     * @param scheduler_threshold Threshold for the scheduler.
     * @param scheduler_timeout Timeout for the scheduler.
     * @param print_fps Whether to print frames per second information.
     */
    HailortAsyncStage(std::string name, std::string hef_path, size_t queue_size, int output_pool_size,
                      std::string group_id, int batch_size, size_t job_limit, int scheduler_threshold = 4,
                      bool dynamic_threshold = false,
                      std::chrono::milliseconds scheduler_timeout = std::chrono::milliseconds(100),
                      StagePoolMode pool_mode = StagePoolMode::FAIL_ON_EMPTY_POOL, float32_t nms_score_threshold = 0.0f,
                      std::vector<bool> nms_classes_filter_mask = {},
                      size_t nms_max_accumulated_mask_size_multiplier = 0, bool trace_processing_operations = true,
                      bool use_hailort_service = false);

    /**
     * @brief Initialize the HailoRT stage.
     *
     * @return AppStatus Status of the initialization.
     */
    AppStatus init() override;

    /**
     * @brief Deinitialize the HailoRT stage.
     *
     * @return AppStatus Status of the deinitialization.
     */
    AppStatus deinit() override;

    /**
     * @brief Set pixel buffer for the inference input.
     *
     * @param buffer Buffer containing the pixel data.
     * @return AppStatus Status of setting the pixel buffer.
     */
    AppStatus set_pix_buf(const HailoMediaLibraryBufferPtr buffer);

    /**
     * @brief Acquire and set tensor buffers for the inference output.
     *
     * @param tensor_buffers Map of tensor buffers to be acquired and set.
     * @return AppStatus Status of acquiring and setting the tensor buffers.
     */
    AppStatus acquire_and_set_tensor_buffers(std::unordered_map<std::string, BufferPtr> &tensor_buffers);

    /**
     * @brief Perform inference on the input buffer by triggering an async infer job.
     * This job calls the given callback on completion.
     *
     * @param input_buffer Buffer containing the input data.
     * @param tensor_buffers Map of tensor buffers for the inference output.
     * @return AppStatus Status of the inference.
     */
    AppStatus infer(BufferPtr input_buffer, const std::unordered_map<std::string, BufferPtr> &tensor_buffers);

    /**
     * @brief Process the data in the buffer.
     *
     * @param data Buffer containing the data to be processed.
     * @return AppStatus Status of the processing.
     */
    AppStatus process(BufferPtr data) override;
};

class HailortAsyncStageBuild : public HailortAsyncStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_hef_path;
        size_t m_queue_size = 10;
        int m_output_pool_size = -1;
        std::optional<std::string> m_group_id;
        int m_batch_size = -1;
        size_t m_job_limit = 0;
        int m_scheduler_threshold = 4;
        bool m_dynamic_threshold = false;
        std::chrono::milliseconds m_scheduler_timeout = std::chrono::milliseconds(100);
        StagePoolMode m_pool_mode = StagePoolMode::FAIL_ON_EMPTY_POOL;
        float32_t m_nms_score_threshold = 0.0f;
        std::vector<bool> m_nms_classes_filter_mask;
        size_t m_nms_max_accumulated_mask_size_multiplier = 0;
        bool m_trace = true;
        bool m_use_hailort_service = false;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_hef_path(std::string path);
        Builder &set_queue_size(size_t size);
        Builder &set_output_pool_size(int size);
        Builder &set_group_id(std::string id);
        Builder &set_batch_size(int size);
        Builder &set_job_limit(size_t size);
        Builder &set_scheduler_threshold_opt(int threshold);
        Builder &set_dynamic_threshold_opt(bool activate);
        Builder &set_scheduler_timeout_opt(std::chrono::milliseconds timeout);
        Builder &set_printfps_opt(bool activate);
        Builder &set_pool_mode_opt(StagePoolMode mode);
        Builder &set_nms_score_threshold(float32_t score_threshold);
        Builder &set_nms_classes_filter_mask(std::vector<bool> mask);
        Builder &set_nms_max_accumulated_mask_size_multiplier(size_t multiplier);
        Builder &set_trace_opt(bool activate);
        Builder &set_use_hailort_service(bool use_service);
        std::shared_ptr<HailortAsyncStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::ai
