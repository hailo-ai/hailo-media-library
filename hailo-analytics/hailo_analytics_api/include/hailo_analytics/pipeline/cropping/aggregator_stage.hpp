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
#include <tl/expected.hpp>

// Postprocess Tools includes
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::cropping
{

enum class SubframeStatus
{
    END_OF_STREAM = 0,
    TIMEOUT
};

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

  public:
    AggregatorStage(std::string name, std::string main_inlet_name, size_t main_queue_size, bool main_queue_leaky,
                    std::string sub_inlet_name, size_t sub_queue_size, bool sub_queue_leaky, bool multi_scale = false,
                    float iou_threshold = 0.3, float m_border_threshold = 0.1, bool skip_migration = false,
                    bool trace_processing_operations = true, std::optional<int> static_sub_frames = std::nullopt,
                    bool copy_sub_frame_tensor_to_metadata = false);

    ~AggregatorStage();

    void add_queue(std::string name) override;
    HailoBBox create_flattened_bbox(const HailoBBox &bbox, const HailoBBox &parent_bbox);
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
    int count_subframes(BufferPtr main_buffer);
    void stamp_and_send(BufferPtr buffer);
    void migrate_metadata(BufferPtr main_buffer, std::vector<BufferPtr> &subframes);

    virtual tl::expected<std::vector<BufferPtr>, SubframeStatus> get_subframes(BufferPtr main_buffer,
                                                                               int num_subframes);

    void loop() override;
    AppStatus deinit() override;
};

class AggregatorStageBuild : public AggregatorStage
{
  public:
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

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_static_subframes_opt(int num);
        Builder &set_main_inlet_name(std::string name);
        Builder &set_main_queue_size(size_t size);
        Builder &set_main_leaky(bool leaky);
        Builder &set_sub_inlet_name(std::string name);
        Builder &set_sub_queue_size(size_t size);
        Builder &set_sub_leaky(bool leaky);
        Builder &set_multiscale_opt(bool multi_scale);
        Builder &set_skip_migration_opt(bool skip);
        Builder &set_trace_opt(bool activate);
        Builder &set_iou_threshold_opt(float threshold);
        Builder &set_border_threshold_opt(float threshold);
        Builder &set_copy_sub_frame_tensor_to_metadata_opt(bool copy);

        std::shared_ptr<AggregatorStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::cropping
