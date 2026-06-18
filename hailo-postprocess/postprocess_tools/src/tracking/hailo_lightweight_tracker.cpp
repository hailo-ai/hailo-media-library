#include "hailo_postprocess_tools/tracking/hailo_lightweight_tracker.hpp"

#include <algorithm>

#include "hailo_postprocess_tools/objects/hailo_objects.hpp" // For HailoUniqueID, TRACKING_ID

// Define the static member for box_history_t
std::atomic<uint64_t> HailoLightweightTracker::box_history_t::s_tracking_id_counter{0};

using BoxHistory = HailoLightweightTracker::BoxHistory;

#define INCREMENT_GRACE_AND_CONTINUE(history, grace_period)                                                            \
    ++history->graces_recieved;                                                                                        \
    if (history->graces_recieved >= grace_period)                                                                      \
        continue;

inline HailoDetectionPtr create_detection_from_history(BoxHistory history, bool add_tracking_id,
                                                       bool copy_nested_objects)
{
    auto det = std::make_shared<HailoDetection>(history->current, history->detection->get_class_id(),
                                                history->detection->get_label(), history->detection->get_confidence());
    // Copy all sub-objects (analytics) from the original detection
    if (copy_nested_objects)
    {
        for (const auto &object : history->detection->get_objects())
        {
            if (object)
            {
                det->add_object(object);
            }
        }
    }
    // Add the tracking ID if requested
    if (add_tracking_id)
    {
        det->add_object(std::make_shared<HailoUniqueID>(history->tracking_id, TRACKING_ID));
    }
    return det;
}

inline void add_history_to_cache(std::vector<std::vector<std::map<uint64_t, BoxHistory>>> &history_cache,
                                 const BoxHistory &history)
{
    if (history->tracking_id == 0)
    {
        // If tracking_id is 0, it means this history is not assigned a tracking ID yet.
        // This should not happen in normal operation, but we handle it gracefully.
        return;
    }

    for (const auto &cell : history->grid_cells)
    {
        history_cache[cell.first][cell.second].emplace(history->tracking_id, history);
    }
}

inline std::vector<BoxHistory> flatten_history_cache(
    const std::vector<std::vector<std::map<uint64_t, BoxHistory>>> &history_cache)
{
    std::map<uint64_t, BoxHistory> union_map;
    for (const auto &cell : history_cache)
    {
        for (const auto &history_map : cell)
        {
            for (const auto &entry : history_map)
            {
                // Use tracking_id as key to ensure uniqueness
                union_map[entry.first] = entry.second; // Overwrite if exists, keeping the latest
            }
        }
    }

    std::vector<BoxHistory> flattened_history;
    flattened_history.reserve(union_map.size());
    for (const auto &entry : union_map)
    {
        flattened_history.emplace_back(entry.second);
    }

    return flattened_history;
}

void HailoLightweightTracker::box_history_t::update_current(const HailoDetectionPtr &detection,
                                                            std::vector<std::pair<size_t, size_t>> grid_cells)
{
    previous.emplace_back(current);
    if (previous.size() > m_history_size)
    {
        previous.erase(previous.begin()); // Maintain history size
    }
    current = detection->get_bbox();
    this->detection = detection;              // Update the detection pointer
    this->grid_cells = std::move(grid_cells); // Update grid cells
}

float iou_calc(const HailoBBox &box_1, const HailoBBox &box_2)
{
    // Calculate IOU between two detection boxes
    const float width_of_overlap_area = std::min(box_1.xmax(), box_2.xmax()) - std::max(box_1.xmin(), box_2.xmin());
    const float height_of_overlap_area = std::min(box_1.ymax(), box_2.ymax()) - std::max(box_1.ymin(), box_2.ymin());
    const float positive_width_of_overlap_area = std::max(width_of_overlap_area, 0.0f);
    const float positive_height_of_overlap_area = std::max(height_of_overlap_area, 0.0f);
    const float area_of_overlap = positive_width_of_overlap_area * positive_height_of_overlap_area;
    const float box_1_area = (box_1.ymax() - box_1.ymin()) * (box_1.xmax() - box_1.xmin());
    const float box_2_area = (box_2.ymax() - box_2.ymin()) * (box_2.xmax() - box_2.xmin());
    // The IOU is a ratio of how much the boxes overlap vs their size outside the overlap.
    // Boxes that are similar will have a higher overlap threshold.
    return area_of_overlap / (box_1_area + box_2_area - area_of_overlap);
}

std::vector<std::pair<size_t, size_t>> HailoLightweightTracker::get_overlapping_grid_cells(const HailoBBox &bbox) const
{
    std::vector<std::pair<size_t, size_t>> cells;
    cells.reserve(m_grid_size * m_grid_size); // Reserve space for all cells
    float x_min = bbox.xmin();
    float x_max = bbox.xmax();
    float y_min = bbox.ymin();
    float y_max = bbox.ymax();
    for (size_t i = 0; i < m_grid_size; ++i)
    {
        float cell_xmin = float(i) / m_grid_size;
        float cell_xmax = float(i + 1) / m_grid_size;
        if (x_max <= cell_xmin || x_min >= cell_xmax)
            continue;
        for (size_t j = 0; j < m_grid_size; ++j)
        {
            float cell_ymin = float(j) / m_grid_size;
            float cell_ymax = float(j + 1) / m_grid_size;
            if (y_max <= cell_ymin || y_min >= cell_ymax)
                continue;
            cells.emplace_back(i, j);
        }
    }
    return cells;
}

std::optional<HailoLightweightTracker::BoxHistory> HailoLightweightTracker::match_bbox_history(
    const HailoBBox &bbox, int class_id, std::vector<std::pair<size_t, size_t>> grid_cells)
{
    std::map<size_t, BoxHistory> candidate_histories;
    for (const auto &cell : grid_cells)
    {
        // Check if the cell exists in the history
        if (m_bbox_history[cell.first][cell.second].empty())
            continue; // No histories in this cell

        // Add all histories from this cell to candidate_histories
        auto &cell_histories = m_bbox_history[cell.first][cell.second];
        candidate_histories.insert(cell_histories.begin(), cell_histories.end());
    }

    BoxHistory best_history;
    float max_iou = 0.0f;
    bool found = false;

    // Check the current grid cell in the history
    for (auto &history : candidate_histories)
    {
        if (history.second->detection->get_class_id() != class_id)
            continue; // Skip if class ID does not match

        float iou = iou_calc(bbox, history.second->current);
        if (iou > max_iou && iou >= m_iou_threshold)
        {
            max_iou = iou;
            best_history = history.second; // Update best match
            found = true;
        }
    }
    if (!found)
    {
        return std::nullopt; // No match found
    }

    // remove the best history from all grid cells to avoid reusing it
    for (const auto &cell : best_history->grid_cells)
    {
        m_bbox_history[cell.first][cell.second].erase(best_history->tracking_id); // Remove by tracking ID
    }

    return best_history;
}

HailoBBox HailoLightweightTracker::smooth_bbox(HailoBBox bbox, std::vector<HailoBBox> &history)
{
    if (history.empty())
    {
        return bbox; // No previous bboxes to smooth with
    }

    // Weighted average: latest bbox gets highest weight, older get less
    float total_weight = 0.0f;
    float sum_xmin = 0.0f, sum_ymin = 0.0f, sum_width = 0.0f, sum_height = 0.0f;
    float weight = 1.0f;

    // Newest is last in previous
    for (auto it = history.rbegin(); it != history.rend(); ++it)
    {
        sum_xmin += it->xmin() * weight;
        sum_ymin += it->ymin() * weight;
        sum_width += it->width() * weight;
        sum_height += it->height() * weight;
        total_weight += weight;
        weight *= m_weighted_average_decay;
    }

    float avg_xmin = sum_xmin / total_weight;
    float avg_ymin = sum_ymin / total_weight;
    float avg_width = sum_width / total_weight;
    float avg_height = sum_height / total_weight;

    // Exponential moving average with the weighted mean of all previous bboxes
    float xmin = m_smooth_alpha * bbox.xmin() + (1 - m_smooth_alpha) * avg_xmin;
    float ymin = m_smooth_alpha * bbox.ymin() + (1 - m_smooth_alpha) * avg_ymin;
    float width = m_smooth_alpha * bbox.width() + (1 - m_smooth_alpha) * avg_width;
    float height = m_smooth_alpha * bbox.height() + (1 - m_smooth_alpha) * avg_height;

    return HailoBBox(xmin, ymin, width, height);
}

std::vector<HailoDetectionPtr> HailoLightweightTracker::update(std::vector<HailoDetectionPtr> &inputs)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<HailoDetectionPtr> outputs;
    if (inputs.empty()) // if no inputs, copy from history
    {
        auto flattened_history = flatten_history_cache(m_bbox_history);
        outputs.reserve(flattened_history.size());
        for (BoxHistory history : flattened_history)
        {
            INCREMENT_GRACE_AND_CONTINUE(history, m_grace_period)
            outputs.emplace_back(create_detection_from_history(history, m_add_tracking_id, m_copy_nested_objects));
        }
        return outputs;
    }

    m_bbox_pending_history.resize(m_grid_size);
    for (size_t i = 0; i < m_grid_size; ++i)
    {
        m_bbox_pending_history[i].resize(m_grid_size);
    }

    for (const auto &det : inputs)
    {
        // Match bbox to existing histories
        std::vector<std::pair<size_t, size_t>> grid_cells = get_overlapping_grid_cells(det->get_bbox());
        auto matched_history = match_bbox_history(det->get_bbox(), det->get_class_id(), grid_cells);
        if (!matched_history.has_value())
        {
            // No match found, create a new history entry
            // and add the current detection to outputs since we cannot match it
            BoxHistory new_history =
                std::make_shared<box_history_t>(det->get_bbox(), grid_cells, det, m_history_size, true);
            add_history_to_cache(m_bbox_pending_history, new_history);
            continue;
        }

        // Match found, update the history with the current bbox
        BoxHistory history = matched_history.value();
        history->update_current(det, grid_cells);
        history->current = smooth_bbox(history->current, history->previous);
        add_history_to_cache(m_bbox_pending_history, history);
        history->graces_recieved = 0; // Reset grace count since we matched
    }

    // move all non-matched histories to pending history, based on the grace period remaining for each and delete those
    // that are expired
    auto flattened_history = flatten_history_cache(m_bbox_history);
    for (auto history : flattened_history)
    {
        INCREMENT_GRACE_AND_CONTINUE(history, m_grace_period)
        add_history_to_cache(m_bbox_pending_history, history);
    }

    // create outputs from the pending history
    std::vector<BoxHistory> flattened_pending_history = flatten_history_cache(m_bbox_pending_history);
    for (auto history : flattened_pending_history)
    {
        outputs.emplace_back(create_detection_from_history(history, m_add_tracking_id, m_copy_nested_objects));
    }

    // clear history and move pending history to current
    m_bbox_history.clear();
    m_bbox_history.swap(m_bbox_pending_history);
    m_bbox_pending_history.clear();
    return outputs;
}
