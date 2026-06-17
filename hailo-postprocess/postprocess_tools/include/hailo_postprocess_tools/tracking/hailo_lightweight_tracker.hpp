/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <mutex>
#include <optional>
#include <atomic>
#include <map>
#include <memory>
#include <utility>

#include "hailo_postprocess_tools/objects/hailo_objects.hpp"

#define HAILO_LIGHTWEIGHT_TRACKER_DEFAULT_HISTORY_SIZE 3
#define HAILO_LIGHTWEIGHT_TRACKER_DEFAULT_SMOOTH_ALPHA 0.7f
#define HAILO_LIGHTWEIGHT_TRACKER_DEFAULT_IOU_THRESHOLD 0.4f
#define HAILO_LIGHTWEIGHT_TRACKER_DEFAULT_WEIGHTED_AVERAGE_DECAY 0.7f
#define HAILO_LIGHTWEIGHT_TRACKER_DEFAULT_GRID_SIZE 4           // Default grid size N for NxN grid
#define HAILO_LIGHTWEIGHT_TRACKER_DEFAULT_GRACE_PERIOD 5        // Default grace before deleting a tracking ID
#define HAILO_LIGHTWEIGHT_TRACKER_DEFAULT_ADD_TRACKING_ID false // Default to not show tracking ID in detections

/**
 * @class HailoLightweightTracker
 * @brief A lightweight tracker for smoothing bounding box detections over time.
 *
 * This class implements a simple tracking mechanism that maintains a history of bounding boxes
 * for each detected object (identified by class ID and label) and applies smoothing to reduce
 * jitter between frames. The smoothing is controlled by a configurable history size and smoothing
 * factor (alpha).
 *
 * @note This tracker is designed for lightweight use cases and does not perform object re-identification
 *       or advanced tracking logic.
 */
class HailoLightweightTracker
{
  public:
    class box_history_t
    {
      public:
        std::vector<HailoBBox> previous; // Previous bboxes for smoothing
        HailoBBox current;
        HailoDetectionPtr detection; // Optional detection pointer for additional info
        uint64_t tracking_id;
        uint64_t graces_recieved;
        std::vector<std::pair<size_t, size_t>> grid_cells; // Grid cells for fast lookup

        box_history_t(const HailoBBox &bbox, std::vector<std::pair<size_t, size_t>> grid_cells,
                      HailoDetectionPtr det = nullptr,
                      size_t history_size = HAILO_LIGHTWEIGHT_TRACKER_DEFAULT_HISTORY_SIZE,
                      bool assign_tracking_id = false)
            : previous(), current(bbox), detection(det), graces_recieved(0), grid_cells(std::move(grid_cells)),
              m_history_size(history_size)
        {
            if (assign_tracking_id)
                tracking_id = s_tracking_id_counter.fetch_add(1, std::memory_order_relaxed);
            previous.reserve(m_history_size + 1);
        } // Constructor with bbox and optional detection pointer

        box_history_t(const box_history_t &other)
            : previous(other.previous), current(other.current), detection(other.detection),
              tracking_id(other.tracking_id), graces_recieved(other.graces_recieved), grid_cells(other.grid_cells),
              m_history_size(other.m_history_size)
        {
        }

        box_history_t() : box_history_t(HailoBBox(0.0f, 0.0f, 0.0f, 0.0f), {})
        {
        } // Default constructor

        void update_current(const HailoDetectionPtr &detection, std::vector<std::pair<size_t, size_t>> grid_cells);

      private:
        size_t m_history_size;
        static std::atomic<uint64_t> s_tracking_id_counter; // Static counter for unique tracking IDs
    };

    typedef std::shared_ptr<box_history_t> BoxHistory;

    /**
     * @class TrackerParams
     * @brief Holds configuration parameters for the lightweight tracker.
     *
     * This class encapsulates all tunable parameters required to configure the behavior of the lightweight tracker.
     *
     * @var size_t TrackerParams::history_size
     *      The number of previous frames to keep in the tracker's history.
     * @var float TrackerParams::smooth_alpha
     *      Smoothing factor for updating tracked object positions.
     * @var float TrackerParams::iou_threshold
     *      Intersection-over-Union threshold for associating detections with existing tracks.
     * @var float TrackerParams::weighted_average_decay
     *      Decay factor used in as a weighted average for giving more weight to recent detections.
     * @var size_t TrackerParams::grid_size
     *      Size of the grid used for spatial partitioning during tracking.
     * @var int TrackerParams::grace_period
     *      Number of frames to keep a track alive without detection before deletion.
     * @var bool TrackerParams::add_tracking_id
     *      Whether to add a unique tracking ID to each tracked object.
     * @var bool TrackerParams::copy_nested_objects
     *      Whether to copy nested objects when tracking.
     */
    class TrackerParams
    {
      public:
        size_t history_size;
        float smooth_alpha;
        float iou_threshold;
        float weighted_average_decay;
        size_t grid_size;
        int grace_period;
        bool add_tracking_id;
        bool copy_nested_objects;

        TrackerParams(size_t history_size = HAILO_LIGHTWEIGHT_TRACKER_DEFAULT_HISTORY_SIZE,
                      float smooth_alpha = HAILO_LIGHTWEIGHT_TRACKER_DEFAULT_SMOOTH_ALPHA,
                      float iou_threshold = HAILO_LIGHTWEIGHT_TRACKER_DEFAULT_IOU_THRESHOLD,
                      float weighted_average_decay = HAILO_LIGHTWEIGHT_TRACKER_DEFAULT_WEIGHTED_AVERAGE_DECAY,
                      size_t grid_size = HAILO_LIGHTWEIGHT_TRACKER_DEFAULT_GRID_SIZE,
                      int grace_period = HAILO_LIGHTWEIGHT_TRACKER_DEFAULT_GRACE_PERIOD,
                      bool add_tracking_id = HAILO_LIGHTWEIGHT_TRACKER_DEFAULT_ADD_TRACKING_ID,
                      bool copy_nested_objects = false)
            : history_size(history_size), smooth_alpha(smooth_alpha), iou_threshold(iou_threshold),
              weighted_average_decay(weighted_average_decay), grid_size(grid_size), grace_period(grace_period),
              add_tracking_id(add_tracking_id), copy_nested_objects(copy_nested_objects)
        {
        }
    };

    /**
     * @brief Constructor for HailoLightweightTracker.
     *
     * @param history_size The number of frames to maintain in the tracking history.
     * @param smooth_alpha The smoothing factor (0=old, 1=new) for bounding box updates.
     * @param grid_size The grid size N for NxN grid acceleration.
     */
    HailoLightweightTracker(TrackerParams params)
        : m_history_size(params.history_size), m_smooth_alpha(params.smooth_alpha),
          m_iou_threshold(params.iou_threshold), m_weighted_average_decay(params.weighted_average_decay),
          m_grid_size(params.grid_size), m_grace_period(params.grace_period), m_add_tracking_id(params.add_tracking_id),
          m_copy_nested_objects(params.copy_nested_objects)
    {
        m_bbox_history.resize(m_grid_size);
        m_bbox_pending_history.resize(m_grid_size);

        for (size_t i = 0; i < m_grid_size; ++i)
        {
            m_bbox_history[i].resize(m_grid_size);
            m_bbox_pending_history[i].resize(m_grid_size);
        }
    }
    ~HailoLightweightTracker() = default;

    std::vector<HailoDetectionPtr> update(std::vector<HailoDetectionPtr> &inputs);

  private:
    size_t m_history_size;          // Number of frames to smooth over
    float m_smooth_alpha;           // Smoothing factor (0=old, 1=new)
    float m_iou_threshold;          // IoU threshold for matching bboxes
    float m_weighted_average_decay; // Decay factor for weighted average
    size_t m_grid_size;             // Grid size N for NxN grid
    size_t m_grace_period;          // Grace period before deleting a tracking ID
    bool m_add_tracking_id;         // Whether to add tracking ID to the detection
    bool m_copy_nested_objects;     // Whether to copy nested objects in the detection

    // 2D grid for fast lookup: each cell contains indices to m_bbox_history
    std::vector<std::vector<std::map<uint64_t, BoxHistory>>> m_bbox_history;
    std::vector<std::vector<std::map<uint64_t, BoxHistory>>> m_bbox_pending_history;

    std::mutex m_mutex;

    std::vector<std::pair<size_t, size_t>> get_overlapping_grid_cells(const HailoBBox &bbox) const;
    HailoBBox smooth_bbox(HailoBBox bbox, std::vector<HailoBBox> &history);
    std::optional<BoxHistory> match_bbox_history(const HailoBBox &bbox, int class_id,
                                                 std::vector<std::pair<size_t, size_t>> grid_cells);
};
