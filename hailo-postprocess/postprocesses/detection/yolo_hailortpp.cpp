#include <rapidjson/encodings.h>
#include <regex>
#include <map>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "hailo_postprocess_tools/objects/json_config.hpp"
#include "hailo_postprocess_tools/labels/coco_eighty.hpp"
#include "hailo_postprocess_tools/labels/yolo_personface.hpp"
#include "hailo_postprocess_tools/labels/hailo_yolov8n.hpp"
#include "hailo_nms_decode.hpp"
#include "yolo_hailortpp.hpp"
#include "common/structures.hpp"
#include "common/file_reader.hpp"
#include "hailo_gst_tensor_metadata.hpp"
#include "hailo_postprocess_tools/objects/hailo_tensors.hpp"
#include "hailort.h"

static const std::string DEFAULT_YOLOV5S_OUTPUT_LAYER = "yolov5s_nv12/yolov5_nms_postprocess";
static const std::string DEFAULT_YOLOV5M_OUTPUT_LAYER = "yolov5m_wo_spp_60p/yolov5_nms_postprocess";
static const std::string DEFAULT_YOLOV5M_VEHICLES_OUTPUT_LAYER = "yolov5m_vehicles/yolov5_nms_postprocess";
static const std::string DEFAULT_YOLOV8S_OUTPUT_LAYER = "yolov8s_nv12/yolov8_nms_postprocess";
static const std::string DEFAULT_YOLOV8M_OUTPUT_LAYER = "yolov8m/yolov8_nms_postprocess";

#if __GNUC__ > 8
#include <filesystem>

namespace fs = std::filesystem;
#else
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;
#endif

YoloParamsNMS *init(const std::string config_path, const std::string /*function_name*/)
{
    YoloParamsNMS *params;
    if (!fs::exists(config_path))
    {
        params = new YoloParamsNMS(common::coco_eighty);
        return params;
    }
    else
    {
        params = new YoloParamsNMS();
        const char *json_schema = R""""({
        "$schema": "http://json-schema.org/draft-04/schema#",
        "type": "object",
        "properties": {
            "detection_threshold": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
            },
            "max_boxes": {
            "type": "integer"
            },
            "labels": {
            "type": "array",
            "items": {
                "type": "string"
                }
            }
        },
        "required": [
            "labels"
        ]
        })"""";

        std::string config_content = common::read_file(config_path);
        bool valid = common::validate_json_with_schema(config_content, json_schema);
        if (valid)
        {
            rapidjson::Document doc_config_json;
            doc_config_json.Parse(config_content.c_str());

            // parse labels
            auto labels = doc_config_json["labels"].GetArray();
            uint i = 0;
            for (auto &v : labels)
            {
                params->labels.insert(std::pair<std::uint8_t, std::string>(i, v.GetString()));
                i++;
            }

            // set the params
            if (doc_config_json.HasMember("detection_threshold"))
            {
                params->detection_threshold = doc_config_json["detection_threshold"].GetFloat();
            }
            if (doc_config_json.HasMember("max_boxes"))
            {
                params->max_boxes = doc_config_json["max_boxes"].GetInt();
                params->filter_by_score = true;
            }
        }
    }
    return params;
}
void free_resources(void *params_void_ptr)
{
    YoloParamsNMS *params = reinterpret_cast<YoloParamsNMS *>(params_void_ptr);
    delete params;
}

static std::map<uint8_t, std::string> yolo_vehicles_labels = {{0, "unlabeled"}, {1, "car"}};

void yolov5(HailoROIPtr roi)
{
    if (!roi->has_tensors())
    {
        return;
    }
    auto post = HailoNMSDecode(roi->get_tensor(DEFAULT_YOLOV5M_OUTPUT_LAYER), common::coco_eighty);
    auto detections = post.decode<float32_t, common::hailo_bbox_float32_t>();
    hailo_common::add_detections(roi, detections);
}

void yolov5s_nv12(HailoROIPtr roi)
{
    if (!roi->has_tensors())
    {
        return;
    }
    auto post = HailoNMSDecode(roi->get_tensor(DEFAULT_YOLOV5S_OUTPUT_LAYER), common::coco_eighty);
    auto detections = post.decode<float32_t, common::hailo_bbox_float32_t>();
    hailo_common::add_detections(roi, detections);
}

void yolov8s(HailoROIPtr roi)
{
    if (!roi->has_tensors())
    {
        return;
    }
    auto post = HailoNMSDecode(roi->get_tensor(DEFAULT_YOLOV8S_OUTPUT_LAYER), common::coco_eighty);
    auto detections = post.decode<float32_t, common::hailo_bbox_float32_t>();
    hailo_common::add_detections(roi, detections);
}

void yolov8m(HailoROIPtr roi)
{
    if (!roi->has_tensors())
    {
        return;
    }
    auto post = HailoNMSDecode(roi->get_tensor(DEFAULT_YOLOV8M_OUTPUT_LAYER), common::coco_eighty);
    auto detections = post.decode<float32_t, common::hailo_bbox_float32_t>();
    hailo_common::add_detections(roi, detections);
}

void yolov8n_personface(HailoROIPtr roi, YoloParamsNMS *params)
{
    if (!roi->has_tensors())
    {
        return;
    }
    auto post = HailoNMSDecode(roi->get_tensor("yolov8n_personface_384_640_nv12/yolov8_nms_postprocess"),
                               common::yolo_personface, params->detection_threshold, params->max_boxes, true);
    auto detections = post.decode<float32_t, common::hailo_bbox_float32_t>();
    hailo_common::add_detections(roi, detections);
}

void hailo_yolov8n(HailoROIPtr roi, YoloParamsNMS *params)
{
    if (!roi->has_tensors())
    {
        return;
    }
    auto post = HailoNMSDecode(roi->get_tensor("hailo_yolov8n_384_640/yolov8_nms_postprocess"), common::hailo_yolov8n,
                               params->detection_threshold, params->max_boxes, true);
    auto detections = post.decode<float32_t, common::hailo_bbox_float32_t>();
    hailo_common::add_detections(roi, detections);
}

void hailo_yolov8s(HailoROIPtr roi, YoloParamsNMS *params)
{
    if (!roi->has_tensors())
    {
        return;
    }
    auto post = HailoNMSDecode(roi->get_tensor("hailo_yolov8s_384_640/yolov8_nms_postprocess"), common::hailo_yolov8n,
                               params->detection_threshold, params->max_boxes, true);
    auto detections = post.decode<float32_t, common::hailo_bbox_float32_t>();
    hailo_common::add_detections(roi, detections);
}

void hailo_yolov8m(HailoROIPtr roi, YoloParamsNMS *params)
{
    if (!roi->has_tensors())
    {
        return;
    }
    auto post = HailoNMSDecode(roi->get_tensor("hailo_yolov8m_384_640/yolov8_nms_postprocess"), common::hailo_yolov8n,
                               params->detection_threshold, params->max_boxes, true);
    auto detections = post.decode<float32_t, common::hailo_bbox_float32_t>();
    hailo_common::add_detections(roi, detections);
}

void yolox(HailoROIPtr roi)
{
    if (!roi->has_tensors())
    {
        return;
    }
    auto post = HailoNMSDecode(roi->get_tensor("yolox_nms_postprocess"), common::coco_eighty);
    auto detections = post.decode<float32_t, common::hailo_bbox_float32_t>();
    hailo_common::add_detections(roi, detections);
}

void yolov5m_vehicles(HailoROIPtr roi)
{
    if (!roi->has_tensors())
    {
        return;
    }
    auto post = HailoNMSDecode(roi->get_tensor(DEFAULT_YOLOV5M_VEHICLES_OUTPUT_LAYER), yolo_vehicles_labels);
    auto detections = post.decode<float32_t, common::hailo_bbox_float32_t>();
    hailo_common::add_detections(roi, detections);
}

void yolov5m_vehicles_nv12(HailoROIPtr roi)
{
    if (!roi->has_tensors())
    {
        return;
    }
    auto post = HailoNMSDecode(roi->get_tensor("yolov5m_vehicles_nv12/yolov5_nms_postprocess"), yolo_vehicles_labels);
    auto detections = post.decode<float32_t, common::hailo_bbox_float32_t>();
    hailo_common::add_detections(roi, detections);
}

void yolov5s_personface(HailoROIPtr roi, YoloParamsNMS *params)
{
    if (!roi->has_tensors())
    {
        return;
    }

    auto post = HailoNMSDecode(roi->get_tensor("yolov5s_personface/yolov5_nms_postprocess"), common::yolo_personface,
                               params->detection_threshold, params->max_boxes, true);
    auto detections = post.decode<float32_t, common::hailo_bbox_float32_t>();
    hailo_common::add_detections(roi, detections);
}

void yolov5_no_persons(HailoROIPtr roi)
{
    if (!roi->has_tensors())
    {
        return;
    }
    auto post = HailoNMSDecode(roi->get_tensor(DEFAULT_YOLOV5M_OUTPUT_LAYER), common::coco_eighty);
    auto detections = post.decode<float32_t, common::hailo_bbox_float32_t>();
    for (auto it = detections.begin(); it != detections.end();)
    {
        if (it->get_label() == "person")
        {
            it = detections.erase(it);
        }
        else
        {
            ++it;
        }
    }
    hailo_common::add_detections(roi, detections);
}

void yolov5_seg(HailoROIPtr roi)
{
    if (!roi->has_tensors())
    {
        return;
    }

    // find the seg nms tensor
    std::vector<HailoTensorPtr> tensors = roi->get_tensors();
    for (auto tensor : tensors)
    {
        if (!std::regex_search(tensor->name(), std::regex("yolov5_seg_nms_postprocess")))
        {
            continue;
        }

        uint8_t *buffer = tensor->data();
        std::vector<HailoSegmentation> segmentations = {};
        uint16_t segmentations_count = *(uint16_t *)buffer;
        size_t buffer_offset = sizeof(uint16_t);

        for (size_t i = 0; i < segmentations_count; i++)
        {
            hailo_detection_with_byte_mask_t segmentation =
                *(hailo_detection_with_byte_mask_t *)(buffer + buffer_offset);
            auto width = static_cast<uint32_t>(segmentation.box.x_max - segmentation.box.x_min);
            auto height = static_cast<uint32_t>(segmentation.box.y_max - segmentation.box.y_min);
            segmentations.push_back(HailoSegmentation(
                HailoBBox(segmentation.box.x_min, segmentation.box.y_min, width, height), std::move(segmentation)));
            buffer_offset += sizeof(hailo_detection_with_byte_mask_t) + segmentation.mask_size;
        }

        hailo_common::add_segmentations(roi, segmentations);
    }
}

void filter(HailoROIPtr roi, void *params_void_ptr)
{
    if (!roi->has_tensors())
    {
        return;
    }
    YoloParamsNMS *params = reinterpret_cast<YoloParamsNMS *>(params_void_ptr);
    std::vector<HailoTensorPtr> tensors = roi->get_tensors();
    // find the nms tensor
    for (auto tensor : tensors)
    {
        if (std::regex_search(tensor->name(), std::regex("nms_postprocess")))
        {
            auto post = HailoNMSDecode(tensor, params->labels, params->detection_threshold, params->max_boxes,
                                       params->filter_by_score);
            auto detections = post.decode<float32_t, common::hailo_bbox_float32_t>();
            hailo_common::add_detections(roi, detections);
        }
    }
}

void filter_letterbox(HailoROIPtr roi, void *params_void_ptr)
{
    filter(roi, params_void_ptr);
    // Resize Letterbox
    HailoBBox roi_bbox = hailo_common::create_flattened_bbox(roi->get_bbox(), roi->get_scaling_bbox());
    auto detections = hailo_common::get_hailo_detections(roi);
    for (auto &detection : detections)
    {
        auto detection_bbox = detection->get_bbox();
        auto xmin = (detection_bbox.xmin() * roi_bbox.width()) + roi_bbox.xmin();
        auto ymin = (detection_bbox.ymin() * roi_bbox.height()) + roi_bbox.ymin();
        auto xmax = (detection_bbox.xmax() * roi_bbox.width()) + roi_bbox.xmin();
        auto ymax = (detection_bbox.ymax() * roi_bbox.height()) + roi_bbox.ymin();

        HailoBBox new_bbox(xmin, ymin, xmax - xmin, ymax - ymin);
        detection->set_bbox(new_bbox);
    }

    // Clear the scaling bbox of main roi because all detections are fixed.
    roi->clear_scaling_bbox();
}
