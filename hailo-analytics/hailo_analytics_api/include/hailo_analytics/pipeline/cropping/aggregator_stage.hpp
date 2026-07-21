#pragma once

/**
 * @file aggregator_stage.hpp
 * @brief Stage base class that performs aggregation of cropped video frames.
 **/

// General includes
#include <unordered_set>
#include <vector>
#include <string>
#include <functional>
#include <cstddef>
#include <atomic>
#include <chrono>
#include <optional>
#include <tl/expected.hpp>

// Postprocess Tools includes
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::cropping
{

/**
 * @brief Status of subframe retrieval
 */
enum class SubframeStatus
{
    END_OF_STREAM = 0, ///< End of stream reached
    TIMEOUT            ///< Timeout occurred while waiting for subframes
};

/**
 * @brief Stage for aggregating cropped subframes back into the main frame
 *
 * This stage aggregates detection results from multiple cropped subframes (e.g., tiles or
 * bounding box crops) back into the coordinate space of the main frame. It handles:
 * - Coordinate transformation from subframe to main frame space
 * - NMS (Non-Maximum Suppression) across subframes
 * - Border detection removal to avoid duplicate detections at tile boundaries
 * - Multi-scale aggregation support
 * - Metadata migration from subframes to main frame
 */
class AggregatorStage : public hailo_analytics::pipeline::ThreadedStage
{
  protected:
    std::string m_main_inlet_name;
    size_t m_main_queue_size;
    std::string m_sub_inlet_name;
    size_t m_sub_queue_size;
    std::optional<int> m_static_sub_frames;
    bool m_multi_scale;
    float m_iou_threshold;
    float m_border_threshold;
    bool m_skip_migration;
    bool m_copy_sub_frame_tensor_to_metadata;
    // When set, the aggregator waits at most this long for a main frame's subframes; when unset it
    // waits indefinitely. Bounding it lets a main buffer be released if the main/subframe pairing
    // desyncs (e.g. a runtime tile-layout change) instead of being held until teardown.
    std::optional<std::chrono::milliseconds> m_subframe_wait_timeout;

  public:
    // Default subframe wait: bounded (enabled), well above normal detection/aggregation latency.
    static constexpr std::chrono::milliseconds DEFAULT_SUBFRAME_WAIT_TIMEOUT{300};
    /**
     * @brief Constructor for AggregatorStage
     * @param name Stage name for identification
     * @param main_inlet_name Name of the main frame inlet
     * @param main_queue_size Size of the main frame queue
     * @param main_queue_leaky If true, main queue drops old frames when full
     * @param sub_inlet_name Name of the subframe inlet
     * @param sub_queue_size Size of the subframe queue
     * @param sub_queue_leaky If true, subframe queue drops old frames when full
     * @param multi_scale Enable multi-scale aggregation (default: false)
     * @param iou_threshold IOU threshold for NMS (default: 0.3)
     * @param m_border_threshold Border threshold for removing edge detections (default: 0.1)
     * @param skip_migration Skip metadata migration (default: false)
     * @param trace_processing_operations Enable tracing (default: true)
     * @param static_sub_frames Fixed number of subframes, if known (default: nullopt)
     * @param copy_sub_frame_tensor_to_metadata Copy tensor metadata from subframes (default: false)
     * @param subframe_wait_timeout Max time to wait for a main frame's subframes; nullopt waits
     *        indefinitely (default: DEFAULT_SUBFRAME_WAIT_TIMEOUT, i.e. bounded/enabled)
     */
    AggregatorStage(std::string name, std::string main_inlet_name, size_t main_queue_size, bool main_queue_leaky,
                    std::string sub_inlet_name, size_t sub_queue_size, bool sub_queue_leaky, bool multi_scale = false,
                    float iou_threshold = 0.3, float m_border_threshold = 0.1, bool skip_migration = false,
                    bool trace_processing_operations = true, std::optional<int> static_sub_frames = std::nullopt,
                    bool copy_sub_frame_tensor_to_metadata = false,
                    std::optional<std::chrono::milliseconds> subframe_wait_timeout = DEFAULT_SUBFRAME_WAIT_TIMEOUT);

    /**
     * @brief Destructor
     */
    ~AggregatorStage();

    /**
     * @brief Add a queue to the stage
     * @param name Name of the queue to add
     */
    void add_queue(std::string name) override;

    /**
     * @brief Create flattened bounding box in parent coordinate space
     * @param bbox Bounding box in local coordinates
     * @param parent_bbox Parent bounding box
     * @return Flattened bounding box
     */
    HailoBBox create_flattened_bbox(const HailoBBox &bbox, const HailoBBox &parent_bbox);

    /**
     * @brief Flatten ROI hierarchy to parent ROI
     * @param roi ROI to flatten
     * @param parent_roi Parent ROI
     * @param filter_type Object type to filter
     */
    void flatten_hailo_roi(HailoROIPtr roi, HailoROIPtr parent_roi, hailo_object_t filter_type);

    /**
     * Remove detections close to the boundary of the tile.
     * Not including tile borders that located on one of the borders of the full frame.
     *
     * @param[in] hailo_tile_roi  HailoTileROIPtr taken from the buffer.
     * @param[in] border_threshold    float.  threshold - 0 - 1 value of 'close to border' ratio.
     * @return void.
     */
    void remove_exceeded_bboxes(HailoROIPtr hailo_tile_roi, float border_threshold);

    /**
     * @brief Calculate Intersection over Union between two bounding boxes
     * @param box_1 First bounding box
     * @param box_2 Second bounding box
     * @return IOU value (0.0 to 1.0)
     */
    float iou_calc(const HailoBBox &box_1, const HailoBBox &box_2);

    /**
     * @brief Perform IOU based NMS on detection objects of HailoRoi
     *
     * @param hailo_roi  -  HailoROIPtr
     *        The HailoROI contains detections to perform NMS on.
     *
     * @param iou_thr  -  float
     *        Threshold for IOU filtration
     */
    void nms(HailoROIPtr hailo_roi, const float iou_thr);

    /**
     * @brief Count expected number of subframes for a main buffer
     * @param main_buffer Main frame buffer
     * @return Number of subframes
     */
    int count_subframes(BufferPtr main_buffer);

    /**
     * @brief Stamp buffer with timestamp and send to subscribers
     * @param buffer Buffer to stamp and send
     */
    void stamp_and_send(BufferPtr buffer);

    /**
     * @brief Migrate metadata from subframes to main buffer
     * @param main_buffer Main frame buffer
     * @param subframes Vector of subframe buffers
     */
    void migrate_metadata(BufferPtr main_buffer, std::vector<BufferPtr> &subframes);

    /**
     * @brief Get subframes for a main buffer
     * @param main_buffer Main frame buffer
     * @param num_subframes Number of subframes to retrieve
     * @return Expected containing vector of subframes or status
     */
    virtual tl::expected<std::vector<BufferPtr>, SubframeStatus> get_subframes(BufferPtr main_buffer,
                                                                               int num_subframes);

    /**
     * @brief Main processing loop
     */
    void loop() override;

    /**
     * @brief Toggle multi-scale aggregation at runtime.
     * @param enabled If true, child bboxes are rescaled into the parent's coordinate space.
     */
    void set_multiscale(bool enabled);

    /**
     * @brief Deinitialize the stage
     * @return AppStatus indicating success or failure
     */
    AppStatus deinit() override;
};

/**
 * @brief Builder pattern implementation for AggregatorStage
 *
 * Provides a fluent interface for constructing AggregatorStage instances
 * with configurable parameters.
 */
class AggregatorStageBuild : public AggregatorStage
{
  public:
    /**
     * @brief Builder class for constructing AggregatorStage instances
     */
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::optional<int> m_static_sub_frames = std::nullopt;
        std::optional<std::string> m_main_inlet_name;
        size_t m_main_queue_size = 10;
        bool m_main_queue_leaky = false;

        std::optional<std::string> m_sub_inlet_name;
        size_t m_sub_queue_size = 10;
        bool m_sub_queue_leaky = false;
        bool m_multi_scale = false;
        bool m_skip_migration = false;
        bool m_trace = true;
        float m_iou_threshold = 0.3;
        float m_border_threshold = 0.1;
        bool m_copy_sub_frame_tensor_to_metadata = false;
        std::optional<std::chrono::milliseconds> m_subframe_wait_timeout = AggregatorStage::DEFAULT_SUBFRAME_WAIT_TIMEOUT;

      public:
        /**
         * @brief Set the stage name
         * @param name Name for the stage
         * @return Builder reference for chaining
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Set static number of subframes
         * @param num Fixed number of subframes to expect
         * @return Builder reference for chaining
         */
        Builder &set_static_subframes_opt(int num);

        /**
         * @brief Set the main inlet name
         * @param name Name of the main frame inlet
         * @return Builder reference for chaining
         */
        Builder &set_main_inlet_name(std::string name);

        /**
         * @brief Set the main queue size
         * @param size Size of the main frame queue
         * @return Builder reference for chaining
         */
        Builder &set_main_queue_size(size_t size);

        /**
         * @brief Set the main queue leaky option
         * @param leaky If true, main queue drops old frames when full
         * @return Builder reference for chaining
         */
        Builder &set_main_leaky(bool leaky);

        /**
         * @brief Set the subframe inlet name
         * @param name Name of the subframe inlet
         * @return Builder reference for chaining
         */
        Builder &set_sub_inlet_name(std::string name);

        /**
         * @brief Set the subframe queue size
         * @param size Size of the subframe queue
         * @return Builder reference for chaining
         */
        Builder &set_sub_queue_size(size_t size);

        /**
         * @brief Set the subframe queue leaky option
         * @param leaky If true, subframe queue drops old frames when full
         * @return Builder reference for chaining
         */
        Builder &set_sub_leaky(bool leaky);

        /**
         * @brief Set the multi-scale option
         * @param multi_scale If true, enables multi-scale aggregation
         * @return Builder reference for chaining
         */
        Builder &set_multiscale_opt(bool multi_scale);

        /**
         * @brief Set the skip migration option
         * @param skip If true, skips metadata migration
         * @return Builder reference for chaining
         */
        Builder &set_skip_migration_opt(bool skip);

        /**
         * @brief Set the trace option
         * @param activate If true, enables tracing for processing operations
         * @return Builder reference for chaining
         */
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Set the IOU threshold for NMS
         * @param threshold IOU threshold value (0.0 to 1.0)
         * @return Builder reference for chaining
         */
        Builder &set_iou_threshold_opt(float threshold);

        /**
         * @brief Set the border threshold for edge detection removal
         * @param threshold Border threshold value (0.0 to 1.0)
         * @return Builder reference for chaining
         */
        Builder &set_border_threshold_opt(float threshold);

        /**
         * @brief Set whether to copy tensor metadata from subframes
         * @param copy If true, copies tensor metadata from subframes
         * @return Builder reference for chaining
         */
        Builder &set_copy_sub_frame_tensor_to_metadata_opt(bool copy);

        /**
         * @brief Set the subframe wait timeout
         * @param timeout Max time to wait for a main frame's subframes; nullopt waits indefinitely
         * @return Builder reference for chaining
         */
        Builder &set_subframe_wait_timeout(std::optional<std::chrono::milliseconds> timeout);

        /**
         * @brief Build and return shared pointer to AggregatorStage
         * @return Shared pointer to constructed AggregatorStage
         * @throws std::runtime_error if required parameters are missing
         */
        std::shared_ptr<AggregatorStage> buildptr() const;
    };

    /**
     * @brief Create a new Builder instance
     * @return Builder instance for constructing AggregatorStage
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::cropping
