/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
/**
 * @file overlay.hpp
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2022-01-20
 *
 * @copyright Copyright (c) 2022
 *
 */
#pragma once

#include <map>
#include <vector>
#include <unordered_set>
#include <string>
#include <functional>
#include <cstddef>
#include <opencv2/opencv.hpp>
#include <algorithm>
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/image_utils/hailomat.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_postprocess_tools/image_utils/overlay_utils.hpp"
typedef enum
{
    OVERLAY_STATUS_UNINITIALIZED = -1,
    OVERLAY_STATUS_OK,
    OVERLAY_STATUS_INVALID_ARGUMENT,
    OVERLAY_STATUS_ERROR

} overlay_status_t;
overlay_status_t draw_all(HailoMat &hmat, HailoROIPtr roi, float landmark_point_radius, bool show_confidence = true,
                          bool local_gallery = false, uint mask_overlay_n_threads = 0, bool partial_landmarks = false,
                          std::unordered_set<size_t> landmark_indices_to_draw = {},
                          std::unordered_set<int> class_ids_to_draw = {},
                          std::function<cv::Scalar(const HailoDetectionPtr &)> color_selector = nullptr);
void face_blur(HailoMat &mat, HailoROIPtr roi);

#define SPACE " "
#define TEXT_CLS_FONT_SCALE_FACTOR (0.0025f)
#define MINIMUM_TEXT_CLS_FONT_SCALE (0.5f)
#define TEXT_DEFAULT_HEIGHT (0.1f)
#define TEXT_FONT_FACTOR (0.12f)
#define MINIMAL_BOX_WIDTH_FOR_TEXT (10)
#define LANDMARKS_COLOR (cv::Scalar(255, 0, 0))
#define NO_GLOBAL_ID_COLOR (cv::Scalar(255, 0, 0))
#define GLOBAL_ID_COLOR (cv::Scalar(0, 255, 0))
#define DEFAULT_DETECTION_COLOR (cv::Scalar(255, 255, 255))
#define DEFAULT_TILE_COLOR (2)
#define NULL_COLOR_ID ((size_t)NULL_CLASS_ID)
#define DEFAULT_COLOR (cv::Scalar(255, 255, 255))
// Transformations were taken from https://stackoverflow.com/questions/17892346/how-to-convert-rgb-yuv-rgb-both-ways.
#define RGB2Y(R, G, B) CLIP((0.257 * (R) + 0.504 * (G) + 0.098 * (B)) + 16)
#define RGB2U(R, G, B) CLIP((-0.148 * (R) - 0.291 * (G) + 0.439 * (B)) + 128)
#define RGB2V(R, G, B) CLIP((0.439 * (R) - 0.368 * (G) - 0.071 * (B)) + 128)

#define DEPTH_MIN_DISTANCE 0.5
#define DEPTH_MAX_DISTANCE 3

static const std::vector<cv::Scalar> tile_layer_color_table = {cv::Scalar(0, 0, 255), cv::Scalar(200, 100, 120),
                                                               cv::Scalar(255, 0, 0), cv::Scalar(120, 0, 0),
                                                               cv::Scalar(0, 0, 120)};

static cv::Scalar get_color(size_t color_id)
{
    cv::Scalar color;
    if (NULL_COLOR_ID == color_id)
        color = DEFAULT_COLOR;
    else
        color = indexToColor(color_id);

    return color;
}

inline std::string confidence_to_string(float confidence)
{
    int confidence_percentage = (confidence * 100);

    return std::to_string(confidence_percentage) + "%";
}

static overlay_status_t draw_classification(HailoMat &mat, HailoROIPtr roi, std::string text,
                                            uint number_of_classifications, size_t color_id = NULL_COLOR_ID)
{
    auto bbox = hailo_common::create_flattened_bbox(roi->get_bbox(), roi->get_scaling_bbox());
    int roi_xmin = bbox.xmin() * mat.native_width();
    int roi_ymin = bbox.ymin() * mat.native_height();
    int roi_width = mat.native_width() * bbox.width();
    int roi_height = mat.native_height() * bbox.height();
    auto text_position = cv::Point(roi_xmin, roi_ymin + (TEXT_DEFAULT_HEIGHT * number_of_classifications * roi_height) +
                                                 log(roi_height));
    double font_scale = TEXT_CLS_FONT_SCALE_FACTOR * roi_width;
    font_scale = (font_scale < MINIMUM_TEXT_CLS_FONT_SCALE) ? MINIMUM_TEXT_CLS_FONT_SCALE : font_scale;

    if (color_id == NULL_COLOR_ID)
    {
        mat.draw_text(text, text_position, font_scale, get_color(color_id));
    }

    else
    {
        cv::Scalar color = get_color(color_id);
        std::vector<cv::Mat> m_mat = mat.get_matrices();

        cv::putText(m_mat[0], text, text_position, cv::FONT_HERSHEY_SIMPLEX, font_scale, cv::Scalar(0, 0, 0), 7);
        mat.draw_text(text, text_position, font_scale, color);

        auto bbox_min = cv::Point(bbox.xmin() * mat.native_width(), bbox.ymin() * mat.native_height());
        auto bbox_max = cv::Point(bbox.xmax() * mat.native_width(), bbox.ymax() * mat.native_height());
        cv::Rect rect(bbox_min, bbox_max);

        // Draw the detection box
        mat.draw_rectangle(rect, color);
    }

    return OVERLAY_STATUS_OK;
}

static std::string get_classification_text(HailoClassificationPtr result, bool show_confidence = true)
{
    std::string text;
    std::string label = result->get_label();
    std::string confidence;
    if (show_confidence)
        confidence = SPACE + confidence_to_string(result->get_confidence());
    text = label + confidence;
    return text;
}

static overlay_status_t draw_landmarks(HailoMat &hmat, HailoLandmarksPtr landmarks, HailoROIPtr roi,
                                       float landmark_point_radius,
                                       const std::unordered_set<size_t> &landmark_indices_to_draw = {},
                                       bool partial_landmarks = false)
{
    HailoBBox bbox = roi->get_bbox();
    int thickness;
    std::vector<std::pair<int, int>> pairs = landmarks->get_pairs();
    int R = roi->get_bbox().height() * hmat.native_height() / 60;
    std::vector<HailoPoint> points = landmarks->get_points();

    for (auto &pair : pairs)
    {
        if ((points.at(pair.first).confidence() > 0) && (points.at(pair.second).confidence() > 0))
        {
            uint x1 = ((points.at(pair.first).x() * bbox.width()) + bbox.xmin()) * hmat.native_width();
            uint y1 = ((points.at(pair.first).y() * bbox.height()) + bbox.ymin()) * hmat.native_height();

            uint x2 = ((points.at(pair.second).x() * bbox.width()) + bbox.xmin()) * hmat.native_width();
            uint y2 = ((points.at(pair.second).y() * bbox.height()) + bbox.ymin()) * hmat.native_height();

            cv::Point joint1 = cv::Point(x1, y1);
            cv::Point joint2 = cv::Point(x2, y2);

            thickness = (bbox.width() < 0.05) ? 1 : 2;
            hmat.draw_line(joint1, joint2, get_color(4), thickness, cv::LINE_4);
        }
    }

    auto draw_point = [&](size_t idx) {
        if (idx >= points.size())
            return;
        const auto &point = points[idx];
        if (point.confidence() >= landmarks->get_threshold())
        {
            uint x = ((point.x() * bbox.width()) + bbox.xmin()) * hmat.native_width();
            uint y = ((point.y() * bbox.height()) + bbox.ymin()) * hmat.native_height();
            hmat.draw_ellipse({static_cast<int>(x), static_cast<int>(y)}, {R, R}, 0, 0, 360, get_color(7), 8);
        }
    };

    if (partial_landmarks && !landmark_indices_to_draw.empty())
    {
        for (size_t idx : landmark_indices_to_draw)
        {
            draw_point(idx);
        }
    }
    else
    {
        for (size_t i = 0; i < points.size(); ++i)
        {
            draw_point(i);
        }
    }

    return OVERLAY_STATUS_OK;
}

static cv::Rect get_rect(HailoMat &mat, HailoDetectionPtr detection, HailoROIPtr roi)
{
    HailoBBox roi_bbox = hailo_common::create_flattened_bbox(roi->get_bbox(), roi->get_scaling_bbox());
    auto detection_bbox = detection->get_bbox();

    auto bbox_min = cv::Point(((detection_bbox.xmin() * roi_bbox.width()) + roi_bbox.xmin()) * mat.native_width(),
                              ((detection_bbox.ymin() * roi_bbox.height()) + roi_bbox.ymin()) * mat.native_height());
    auto bbox_max = cv::Point(((detection_bbox.xmax() * roi_bbox.width()) + roi_bbox.xmin()) * mat.native_width(),
                              ((detection_bbox.ymax() * roi_bbox.height()) + roi_bbox.ymin()) * mat.native_height());
    return cv::Rect(bbox_min, bbox_max);
}

static std::string get_detection_text(HailoDetectionPtr detection, bool show_confidence = true)
{
    std::string text;
    std::string label = detection->get_label();
    std::string confidence = confidence_to_string(detection->get_confidence());
    if (!show_confidence)
        text = label;
    else if (!label.empty())
    {
        text = label + SPACE + confidence;
    }
    else
    {
        text = confidence;
    }
    return text;
}

static overlay_status_t draw_tile(HailoMat &mat, HailoTileROIPtr tile)
{
    auto bbox = tile->get_bbox();
    auto bbox_min = cv::Point(bbox.xmin() * mat.width(), bbox.ymin() * mat.height());
    auto bbox_max = cv::Point(bbox.xmax() * mat.width(), bbox.ymax() * mat.height());
    cv::Rect rect(bbox_min, bbox_max);
    cv::Scalar color;
    uint tile_layer = tile->get_layer();
    if (tile_layer < tile_layer_color_table.size())
        color = tile_layer_color_table[tile_layer];
    else
        color = get_color(DEFAULT_TILE_COLOR);

    // Draw the tile box
    mat.draw_rectangle(rect, color);

    return OVERLAY_STATUS_OK;
}

static overlay_status_t draw_id(HailoMat &mat, HailoUniqueIDPtr &hailo_id, HailoROIPtr roi)
{
    std::string id_text = std::to_string(hailo_id->get_id());

    auto bbox = roi->get_bbox();
    auto bbox_min = cv::Point(bbox.xmin() * mat.native_width(), bbox.ymin() * mat.native_height());
    auto bbox_max = cv::Point(bbox.xmax() * mat.native_width(), bbox.ymax() * mat.native_height());
    auto bbox_width = bbox_max.x - bbox_min.x;
    auto color = get_color(NULL_CLASS_ID);

    // Calculating the font size according to the box width.
    double font_scale = TEXT_FONT_FACTOR * log(bbox_width);
    auto text_position = cv::Point(bbox_min.x + log(bbox_width), bbox_max.y - log(bbox_width));
    // Draw the class and confidence text
    mat.draw_text(id_text, text_position, font_scale, color);
    return OVERLAY_STATUS_OK;
}

/**
 * @brief calculate the destionation region of interest and the resized mask
 *
 * @param destinationROI the region of interest to paint
 * @param image_planes the image data
 * @param roi the region of interest
 * @param mask a mask object inherited from from HailoMask
 * @param resized_mask_data an output of the fucntion, the mask resized
 * @param data_ptr mask data pointer
 * @param cv_type type of cv data, example: CV_32F
 */
template <typename T>
void calc_destination_roi_and_resize_mask(cv::Mat &destinationROI, cv::Mat &image_planes, HailoROIPtr roi,
                                          HailoMaskPtr mask, cv::Mat &resized_mask_data, T data_ptr, int cv_type)
{
    if (mask->get_height() == 0 || mask->get_width() == 0)
    {
        return;
    }

    HailoBBox bbox = roi->get_bbox();
    int roi_xmin = bbox.xmin() * image_planes.cols;
    int roi_ymin = bbox.ymin() * image_planes.rows;
    int roi_width = image_planes.cols * bbox.width();
    int roi_height = image_planes.rows * bbox.height();

    // clamp the region of interest so it is inside the image planes
    roi_xmin = std::clamp(roi_xmin, 0, image_planes.cols);
    roi_ymin = std::clamp(roi_ymin, 0, image_planes.rows);
    roi_width = std::clamp(roi_width, 0, image_planes.cols - roi_xmin);
    roi_height = std::clamp(roi_height, 0, image_planes.rows - roi_ymin);

    // Handle both pointer and container types for data_ptr
    uint8_t *data_ptr_raw;
    if constexpr (std::is_pointer_v<T>)
    {
        data_ptr_raw = (uint8_t *)data_ptr;
    }
    else
    {
        data_ptr_raw = (uint8_t *)data_ptr.data();
    }

    cv::Mat mat_data = cv::Mat(mask->get_height(), mask->get_width(), cv_type, data_ptr_raw);
    cv::resize(mat_data, resized_mask_data, cv::Size(roi_width, roi_height), 0, 0, cv::INTER_LINEAR);

    cv::Rect roi_rect(cv::Point(roi_xmin, roi_ymin), cv::Size(roi_width, roi_height));
    destinationROI = image_planes(roi_rect);
}

/**
 * @brief convert the estimated depths to colors and draw it (override the original image), a darker color means
 * that the depth is smaller.
 *
 * @param image_planes: matrix of the image
 * @param mask : HailoDepthMaskPtr that contains the data of the estimated depth of each pixel
 * @param roi region of interest
 * @return overlay_status_t
 */
static overlay_status_t draw_depth_mask(cv::Mat &image_planes, HailoDepthMaskPtr mask, HailoROIPtr roi,
                                        const uint mask_overlay_n_threads)
{
    cv::Mat resized_mask_data;
    cv::Mat destinationROI;
    calc_destination_roi_and_resize_mask(destinationROI, image_planes, roi, mask, resized_mask_data, mask->get_data(),
                                         CV_32F);

    float min = DEPTH_MIN_DISTANCE;
    float max = DEPTH_MAX_DISTANCE;

    double min_val;
    double max_val;
    cv::Point min_loc;
    cv::Point max_loc;

    cv::minMaxLoc(resized_mask_data, &min_val, &max_val, &min_loc, &max_loc);

    if (max < max_val)
        max = max_val;
    if (min > min_val)
        min = min_val;

    resized_mask_data = (resized_mask_data - min) / (max - min);

    if (mask_overlay_n_threads > 0)
        cv::setNumThreads(mask_overlay_n_threads);

    // perform efficient parallel matrix iteration and color every pixel its class color
    cv::parallel_for_(cv::Range(0, destinationROI.rows * destinationROI.cols),
                      ParallelPixelDepthMask(destinationROI.data, resized_mask_data.data, mask->get_transparency(),
                                             image_planes.cols, destinationROI.cols));

    return OVERLAY_STATUS_OK;
}

/**
 * @brief draw a mask whose values are ints represting class ids.
 * draw every pixel in the color in its class color.
 *
 * @param image_planes the image data
 * @param mask  HailoClassMask mask object pointer
 * @param roi the region of interest
 * @return overlay_status_t OVERLAY_STATUS_OK
 */
static overlay_status_t draw_class_mask(cv::Mat &image_planes, HailoClassMaskPtr mask, HailoROIPtr roi,
                                        const uint mask_overlay_n_threads)
{
    cv::Mat resized_mask_data;
    cv::Mat destinationROI;
    calc_destination_roi_and_resize_mask(destinationROI, image_planes, roi, mask, resized_mask_data, mask->get_data(),
                                         CV_8UC1);

    if (mask_overlay_n_threads > 0)
        cv::setNumThreads(mask_overlay_n_threads);

    // perform efficient parallel matrix iteration and color every pixel its class color
    cv::parallel_for_(cv::Range(0, destinationROI.rows * destinationROI.cols),
                      ParallelPixelClassMask(destinationROI.data, resized_mask_data.data, mask->get_transparency(),
                                             image_planes.cols, destinationROI.cols));

    return OVERLAY_STATUS_OK;
}

/**
 * @brief draw a mask that its values are floats representing confidence.
 * if the pixel value is above threshold, draw this pixel in the mask's class color.
 *
 * @param image_planes the image data
 * @param mask HailoConfClassMask mask object pointer
 * @param roi the region of interest
 * @return overlay_status_t OVERLAY_STATUS_OK
 */
static overlay_status_t draw_conf_class_mask(cv::Mat &image_planes, HailoConfClassMaskPtr mask, HailoROIPtr roi,
                                             const uint mask_overlay_n_threads)
{
    cv::Mat resized_mask_data;
    cv::Mat destinationROI;
    calc_destination_roi_and_resize_mask(destinationROI, image_planes, roi, mask, resized_mask_data, mask->get_data(),
                                         CV_32F);

    cv::Scalar mask_color = indexToColor(mask->get_class_id());

    if (mask_overlay_n_threads > 0)
        cv::setNumThreads(mask_overlay_n_threads);

    // perform efficient parallel matrix iteration and color every pixel its class color
    cv::parallel_for_(cv::Range(0, destinationROI.rows * destinationROI.cols),
                      ParallelPixelClassConfMask(destinationROI.data, resized_mask_data.data, mask->get_transparency(),
                                                 image_planes.cols, destinationROI.cols, mask_color));

    return OVERLAY_STATUS_OK;
}

inline overlay_status_t draw_all(HailoMat &hmat, HailoROIPtr roi, float landmark_point_radius, bool show_confidence,
                                 bool local_gallery, const uint mask_overlay_n_threads, bool partial_landmarks,
                                 std::unordered_set<size_t> landmark_indices_to_draw,
                                 std::unordered_set<int> class_ids_to_draw,
                                 std::function<cv::Scalar(const HailoDetectionPtr &)> color_selector)
{
    overlay_status_t ret = OVERLAY_STATUS_UNINITIALIZED;
    uint number_of_classifications = 0;
    cv::Mat &mat = hmat.get_matrices()[0];
    for (auto obj : roi->get_objects())
    {
        switch (obj->get_type())
        {
        case HAILO_DETECTION: {
            auto detection = std::dynamic_pointer_cast<HailoDetection>(obj);

            if (!class_ids_to_draw.empty() && class_ids_to_draw.count(detection->get_class_id()) == 0)
            {
                continue; // skip bbox not in whitelist
            }

            cv::Scalar color;
            std::string text;

            if (color_selector)
            {
                color = color_selector(detection);
                text = get_detection_text(detection, show_confidence);
            }
            else if (local_gallery)
            {
                auto gids = hailo_common::get_hailo_global_id(detection);
                color = (gids.size() == 1) ? GLOBAL_ID_COLOR : NO_GLOBAL_ID_COLOR;
            }
            else
            {
                color = get_color(static_cast<size_t>(detection->get_class_id()));
                text = get_detection_text(detection, show_confidence);
            }

            auto rect = get_rect(hmat, detection, roi);
            hmat.draw_rectangle(rect, color);

            auto txt_pt = cv::Point(rect.x - std::log(rect.width), rect.y - std::log(rect.width));
            float fscale = TEXT_FONT_FACTOR * std::log(rect.width);
            hmat.draw_text(text, txt_pt, fscale, color);

            ret =
                draw_all(hmat, detection, landmark_point_radius, show_confidence, local_gallery, mask_overlay_n_threads,
                         partial_landmarks, landmark_indices_to_draw, class_ids_to_draw, color_selector);
            break;
        }

        case HAILO_CLASSIFICATION: {
            number_of_classifications++;
            HailoClassificationPtr classification = std::dynamic_pointer_cast<HailoClassification>(obj);
            if (classification->get_classification_type() == "tracking")
            {
                std::string text = get_classification_text(classification, false);
                if (text == "lost")
                    ret = draw_classification(hmat, roi, text, number_of_classifications, 0);
                else if (text == "new")
                    ret = draw_classification(hmat, roi, text, number_of_classifications, 1);
                else if (text == "tracked")
                    ret = draw_classification(hmat, roi, text, number_of_classifications, 2);
            }
            else
            {
                std::string text = get_classification_text(classification, show_confidence);
                ret = draw_classification(hmat, roi, text, number_of_classifications, classification->get_class_id());
            }
            break;
        }
        case HAILO_LANDMARKS: {
            HailoLandmarksPtr landmarks = std::dynamic_pointer_cast<HailoLandmarks>(obj);
            draw_landmarks(hmat, landmarks, roi, landmark_point_radius, landmark_indices_to_draw, partial_landmarks);
            break;
        }
        case HAILO_TILE: {
            HailoTileROIPtr tile = std::dynamic_pointer_cast<HailoTileROI>(obj);
            draw_tile(hmat, tile);
            draw_all(hmat, tile, landmark_point_radius, show_confidence, local_gallery, mask_overlay_n_threads,
                     partial_landmarks, landmark_indices_to_draw);
            break;
        }
        case HAILO_UNIQUE_ID: {
            HailoUniqueIDPtr id = std::dynamic_pointer_cast<HailoUniqueID>(obj);
            if ((local_gallery && id->get_mode() == GLOBAL_ID) || (!local_gallery && id->get_mode() == TRACKING_ID))
                draw_id(hmat, id, roi);
            break;
        }
        case HAILO_DEPTH_MASK: {
            HailoDepthMaskPtr mask = std::dynamic_pointer_cast<HailoDepthMask>(obj);
            draw_depth_mask(mat, mask, roi, mask_overlay_n_threads);
            break;
        }
        case HAILO_CLASS_MASK: {
            HailoClassMaskPtr mask = std::dynamic_pointer_cast<HailoClassMask>(obj);
            draw_class_mask(mat, mask, roi, mask_overlay_n_threads);
            break;
        }
        case HAILO_CONF_CLASS_MASK: {
            HailoConfClassMaskPtr mask = std::dynamic_pointer_cast<HailoConfClassMask>(obj);
            draw_conf_class_mask(mat, mask, roi, mask_overlay_n_threads);
            break;
        }
        default:
            // continue
            break;
        }
    }
    ret = OVERLAY_STATUS_OK;
    return ret;
}

inline void face_blur(HailoMat &hmat, HailoROIPtr roi)
{
    for (auto detection : hailo_common::get_hailo_detections(roi))
    {
        if (detection->get_label() == "face")
        {
            HailoBBox roi_bbox = hailo_common::create_flattened_bbox(roi->get_bbox(), roi->get_scaling_bbox());
            auto detection_bbox = detection->get_bbox();
            auto xmin =
                std::clamp<int>(((detection_bbox.xmin() * roi_bbox.width()) + roi_bbox.xmin()) * hmat.native_width(), 0,
                                hmat.native_width());
            auto ymin =
                std::clamp<int>(((detection_bbox.ymin() * roi_bbox.height()) + roi_bbox.ymin()) * hmat.native_height(),
                                0, hmat.native_height());
            auto xmax =
                std::clamp<int>(((detection_bbox.xmax() * roi_bbox.width()) + roi_bbox.xmin()) * hmat.native_width(), 0,
                                hmat.native_width());
            auto ymax =
                std::clamp<int>(((detection_bbox.ymax() * roi_bbox.height()) + roi_bbox.ymin()) * hmat.native_height(),
                                0, hmat.native_height());
            auto rect = cv::Rect(cv::Point(xmin, ymin), cv::Point(xmax, ymax));
            hmat.blur(rect, cv::Size(13, 13));

            // Remove landmarks from the ROI before overlaying the blurred face
            roi->remove_objects_typed(HAILO_LANDMARKS);
        }
        else
        {
            face_blur(hmat, detection);
        }
    }
}
