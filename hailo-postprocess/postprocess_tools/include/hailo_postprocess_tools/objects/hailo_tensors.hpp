/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
/**
 * @file hailo_tensors.hpp
 * @authors Hailo
 **/

#pragma once
#include "hailo/hailort.h"
#include "hailo/hailo_gst_tensor_metadata.hpp"
#include <memory>
#include <string>
#include <vector>

class HailoTensor
{
  private:
    uint8_t *m_data;    // Pointer to the data of the tensor.
    std::string m_name; // Name of output tensor.
    // The following are used for the tensor shape.
    // Note: The shape is represented as width x height x features.
    uint32_t m_width;    // Width of the tensor.
    uint32_t m_height;   // Height of the tensor.
    uint32_t m_features; // Features of the tensor.
    // NMS Shape parameters.
    uint32_t m_max_bboxes_per_class; // Maximum number of bounding boxes per class in the NMS tensor.
    uint32_t m_number_of_classes;    // Number of classes in the NMS tensor.

    // Quantization parameters
    float32_t m_qp_zp;    // Quantization zero point.
    float32_t m_qp_scale; // Quantization scale.

    // Other tensor params.
    bool m_is_uint16; // Flag to indicate if the tensor is uint16 or not.
    bool m_is_nms;    // Flag to indicate if the tensor is NMS or not.

  public:
    /**
     * @brief Construct a new Hailo Tensor object
     *
     * @param data - Pointer to the tensor output.
     * @param tensor_meta_info - info about the output, represented as hailo_tensor_metadata_t.
     */
    HailoTensor(uint8_t *data, const hailo_tensor_metadata_t &tensor_meta_info)
        : m_data(data), m_name(tensor_meta_info.name), m_width(tensor_meta_info.shape.width),
          m_height(tensor_meta_info.shape.height), m_features(tensor_meta_info.shape.features),
          m_qp_zp(tensor_meta_info.quant_info.qp_zp), m_qp_scale(tensor_meta_info.quant_info.qp_scale),
          m_is_uint16(tensor_meta_info.format.type ==
                      HailoTensorFormatType::HAILO_FORMAT_TYPE_UINT16), // Check if the format is uint16
          m_is_nms(tensor_meta_info.format.is_nms)                      // Check if the tensor is NMS
    {
        // If the tensor is NMS, we set the width and height to 0.
        if (m_is_nms)
        {
            // If the tensor is NMS, we set the width and height to 0.
            m_width = 0;
            m_height = 0;
            m_features = 0;
            m_max_bboxes_per_class = tensor_meta_info.nms_shape.max_bboxes_per_class;
            m_number_of_classes = tensor_meta_info.nms_shape.number_of_classes;
        }
        else
        {
            m_max_bboxes_per_class = 0; // Not applicable for non-NMS tensors.
            m_number_of_classes = 0;    // Not applicable for non-NMS tensors.
        }
    };

    HailoTensor(uint8_t *data, const hailo_vstream_info_t &vstream_info)
        : m_data(data), m_name(vstream_info.name), m_width(vstream_info.shape.width),
          m_height(vstream_info.shape.height), m_features(vstream_info.shape.features),
          m_qp_zp(vstream_info.quant_info.qp_zp), m_qp_scale(vstream_info.quant_info.qp_scale)
    {
        m_is_uint16 = vstream_info.format.type == HAILO_FORMAT_TYPE_UINT16; // Check if the format is uint16
        m_is_nms = (HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS == vstream_info.format.order) ||
                   (HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK == vstream_info.format.order) ||
                   (HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE == vstream_info.format.order);
        if (m_is_nms)
        {
            // If the tensor is NMS, we set the width and height to 0.
            m_width = 0;
            m_height = 0;
            m_features = 0;
            m_max_bboxes_per_class = vstream_info.nms_shape.max_bboxes_per_class;
            m_number_of_classes = vstream_info.nms_shape.number_of_classes;
        }
        else
        {
            m_max_bboxes_per_class = 0; // Not applicable for non-NMS tensors.
            m_number_of_classes = 0;    // Not applicable for non-NMS tensors.
        }
    };
    // Destructor
    ~HailoTensor() = default;
    // Copy constructor
    HailoTensor(const HailoTensor &other) = default;
    // Move constructor
    HailoTensor(HailoTensor &&other) = default;

    // Getters:
    std::string name()
    {
        return m_name;
    }
    uint8_t *data()
    {
        return m_data;
    }
    const uint32_t width()
    {
        return m_width;
    }
    const uint32_t height()
    {
        return m_height;
    }
    const uint32_t features()
    {
        return m_features;
    }
    const float32_t qp_zp()
    {
        return m_qp_zp;
    }
    const float32_t qp_scale()
    {
        return m_qp_scale;
    }
    const uint32_t size() const
    {
        return m_width * m_height * m_features; // Total number of elements in the tensor.
    }
    std::vector<std::size_t> shape()
    {
        std::vector<std::size_t> shape = {height(), width(), features()};
        return shape;
    }

    bool is_uint16() const
    {
        return m_is_uint16;
    }
    bool is_nms() const
    {
        return m_is_nms;
    }

    uint32_t max_bboxes_per_class() const
    {
        return m_max_bboxes_per_class;
    }

    uint32_t number_of_classes() const
    {
        return m_number_of_classes;
    }

    // Methods:
    /**
     * @brief Gets a quantized number and returns its dequantized value (float).
     *
     * @param num number to dequantize.
     * @return float dequantized number.
     */
    template <typename T> float fix_scale(T num)
    {
        return (float(num) - m_qp_zp) * m_qp_scale;
    }

    /**
     * @brief Gets a dequantized number and returns its quantized value (template).
     *
     * @param num number to quantize.
     * @return T quantized number.
     */
    template <typename T> T quantize(T num)
    {
        return T((float(num) / m_qp_scale) + m_qp_zp);
    }

    /**
     * @brief Gets a specific cell of this tensor.
     *
     * @param row The row of the cell
     * @param col The column of the cell
     * @param channel The channel of the cell
     * @return uint8_t value of this tensor at the specified place.
     * @note number is quantized.
     */
    uint8_t get(uint row, uint col, uint channel)
    {
        int pos = (width() * features()) * row + features() * col + channel;
        return m_data[pos];
    }
    uint16_t get_uint16(uint row, uint col, uint channel)
    {
        int pos = (width() * features()) * row + features() * col + channel;
        uint16_t *data_uint16 = (uint16_t *)m_data;
        return data_uint16[pos];
    }

    /**
     * @brief Gets a specific cell of this tensor in full percision (dequantized).
     *
     * @param row The row of the cell
     * @param col The column of the cell
     * @param channel The channel of the cell
     * @return float value of this tensor at the specified place (dequantized).
     */
    float get_full_percision(uint row, uint col, uint channel, bool is_uint16)
    {
        if (is_uint16)
            return fix_scale(get_uint16(row, col, channel));
        else
            return fix_scale(get(row, col, channel));
    }
};

using HailoTensorPtr = std::shared_ptr<HailoTensor>;
