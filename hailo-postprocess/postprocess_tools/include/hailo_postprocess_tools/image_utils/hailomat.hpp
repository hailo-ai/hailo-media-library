/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
/**
 * @file overlay/common.hpp
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2022-01-20
 *
 * @copyright Copyright (c) 2022
 *
 */

#pragma once

#include <opencv2/opencv.hpp>
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"

// Transformations were taken from https://stackoverflow.com/questions/17892346/how-to-convert-rgb-yuv-rgb-both-ways.
#define RGB2Y(R, G, B) CLIP((0.257 * (R) + 0.504 * (G) + 0.098 * (B)) + 16)
#define RGB2U(R, G, B) CLIP((-0.148 * (R) - 0.291 * (G) + 0.439 * (B)) + 128)
#define RGB2V(R, G, B) CLIP((0.439 * (R) - 0.368 * (G) - 0.071 * (B)) + 128)

typedef enum
{
    HAILO_MAT_NONE = -1,
    HAILO_MAT_RGB,
    HAILO_MAT_RGBA,
    HAILO_MAT_YUY2,
    HAILO_MAT_NV12
} hailo_mat_t;

typedef enum
{
    NONE = -1,
    VERTICAL,
    HORIZONTAL,
    DIAGONAL,
    ANTI_DIAGONAL,
} LineOrientation;

inline LineOrientation line_orientation(cv::Point point1, cv::Point point2)
{
    if (point1.x == point2.x)
        return VERTICAL;
    else if (point1.y == point2.y)
        return HORIZONTAL;
    else if (point1.x < point2.x && point1.y < point2.y)
        return DIAGONAL;
    else if (point1.x < point2.x && point1.y > point2.y)
        return ANTI_DIAGONAL;
    else
        return NONE;
}

inline int floor_to_even_number(int x)
{
    /*
    The expression x &~1 in C++ performs a bitwise AND operation between the number x and the number ~1(bitwise negation
    of 1). In binary representation, the number 1 is represented as 0000 0001, and its negation, ~1, is equal to 1111
    1110. The bitwise AND operation between x and ~1 zeros out the least significant bit of x, effectively rounding it
    down to the nearest even number. This is because any odd number in binary representation will have its least
    significant bit set to 1, and ANDing it with ~1 will zero out that bit.
    */
    return x & ~1;
}

class HailoMat
{
  protected:
    uint m_height;
    uint m_width;
    uint m_native_height;
    uint m_native_width;
    uint m_stride;
    int m_line_thickness;
    int m_font_thickness;
    std::vector<cv::Mat> m_matrices;
    cv::Rect get_bounding_rect(HailoBBox bbox, uint channel_width, uint channel_height)
    {
        cv::Rect rect;
        uint width = channel_width;
        uint height = channel_height;
        rect.x = CLAMP(bbox.xmin() * width, 0, width);
        rect.y = CLAMP(bbox.ymin() * height, 0, height);
        rect.width = CLAMP(bbox.width() * width, 0, width - rect.x);
        rect.height = CLAMP(bbox.height() * height, 0, height - rect.y);
        return rect;
    }

  public:
    HailoMat(uint height, uint width, uint stride, int line_thickness = 1, int font_thickness = 1)
        : m_height(height), m_width(width), m_native_height(height), m_native_width(width), m_stride(stride),
          m_line_thickness(line_thickness), m_font_thickness(font_thickness) {};
    HailoMat()
        : m_height(0), m_width(0), m_native_height(0), m_native_width(0), m_stride(0), m_line_thickness(0),
          m_font_thickness(0) {};
    virtual ~HailoMat() = default;
    uint width()
    {
        return m_width;
    };
    uint height()
    {
        return m_height;
    };
    uint native_width()
    {
        return m_native_width;
    };
    uint native_height()
    {
        return m_native_height;
    };
    std::vector<cv::Mat> &get_matrices()
    {
        return m_matrices;
    }
    virtual void draw_rectangle(cv::Rect rect, const cv::Scalar color) = 0;
    virtual void draw_text(std::string text, cv::Point position, double font_scale, const cv::Scalar color) = 0;
    virtual void draw_line(cv::Point point1, cv::Point point2, const cv::Scalar color, int thickness,
                           int line_type) = 0;
    virtual void draw_ellipse(cv::Point center, cv::Size axes, double angle, double start_angle, double end_angle,
                              const cv::Scalar color, int thickness) = 0;
    virtual void blur(cv::Rect rect, cv::Size ksize) = 0;
    /*
     * @brief Crop ROIs from the mat, note the present implementation is valid
     *        for interlaced formats. Planar formats such as NV12 should override.
     *
     * @param crop_roi
     *        The roi to crop from this mat.
     * @return cv::Mat
     *         The cropped mat.
     */
    virtual std::vector<cv::Mat> crop(HailoROIPtr crop_roi)
    {
        cv::Rect rect = get_crop_rect(crop_roi);
        cv::Mat cropped_cv_mat = get_matrices()[0](
            rect); // assuming only one channel (NV12 that has 2 matrices is handled in the derived class)
        // create a vector of 1 mat
        std::vector<cv::Mat> cropped_mats;
        cropped_mats.emplace_back(std::move(cropped_cv_mat));

        return cropped_mats;
    }

    virtual cv::Rect get_crop_rect(HailoROIPtr crop_roi)
    {
        auto bbox = hailo_common::create_flattened_bbox(crop_roi->get_bbox(), crop_roi->get_scaling_bbox());
        cv::Rect rect = get_bounding_rect(bbox, m_width, m_height);
        return rect;
    }

    /**
     * @brief Get the type of mat
     *
     * @return hailo_mat_t - The type of the mat.
     */
    virtual hailo_mat_t get_type() = 0;
};

class HailoRGBMat : public HailoMat
{
  protected:
    std::string m_name;

  public:
    HailoRGBMat(uint8_t *buffer, uint height, uint width, uint stride, int line_thickness = 1, int font_thickness = 1,
                std::string name = "HailoRGBMat")
        : HailoMat(height, width, stride, line_thickness, font_thickness)
    {
        m_name = name;
        cv::Mat mat = cv::Mat(m_height, m_width, CV_8UC3, buffer, m_stride);
        m_matrices.push_back(mat);
    };
    HailoRGBMat(cv::Mat mat, std::string name, int line_thickness = 1, int font_thickness = 1)
    {
        m_matrices.push_back(mat);
        m_name = name;
        m_height = mat.rows;
        m_width = mat.cols;
        m_stride = mat.step;
        m_native_height = m_height;
        m_native_width = m_width;
        m_line_thickness = line_thickness;
        m_font_thickness = font_thickness;
    }
    virtual hailo_mat_t get_type()
    {
        return HAILO_MAT_RGB;
    }
    virtual std::string get_name() const
    {
        return m_name;
    }
    virtual void draw_rectangle(cv::Rect rect, const cv::Scalar color)
    {
        cv::rectangle(m_matrices[0], rect, color, m_line_thickness);
    }
    virtual void draw_text(std::string text, cv::Point position, double font_scale, const cv::Scalar color)
    {
        cv::putText(m_matrices[0], text, position, cv::FONT_HERSHEY_SIMPLEX, font_scale, color, m_font_thickness);
    }
    virtual void draw_line(cv::Point point1, cv::Point point2, const cv::Scalar color, int thickness, int line_type)
    {
        cv::line(m_matrices[0], point1, point2, color, thickness, line_type);
    }
    virtual void draw_ellipse(cv::Point center, cv::Size axes, double angle, double start_angle, double end_angle,
                              const cv::Scalar color, int thickness)
    {
        cv::ellipse(m_matrices[0], center, axes, angle, start_angle, end_angle, color, thickness);
    }
    virtual void blur(cv::Rect rect, cv::Size ksize)
    {
        cv::Mat target_roi = this->m_matrices[0](rect);
        cv::blur(target_roi, target_roi, ksize);
    }
    virtual ~HailoRGBMat()
    {
        for (auto &mat : m_matrices)
        {
            mat.release();
        }
        m_matrices.clear();
    }
};

class HailoRGBAMat : public HailoMat
{
  protected:
    cv::Scalar get_rgba_color(cv::Scalar rgb_color, int alpha = 1.0)
    {
        // setting default alpha as 1.0 as shown in an example: https://www.w3schools.com/css/css_colors_rgb.asp
        rgb_color[3] = alpha;
        return rgb_color;
    }

  public:
    HailoRGBAMat(uint8_t *buffer, uint height, uint width, uint stride, int line_thickness = 1, int font_thickness = 1)
        : HailoMat(height, width, stride, line_thickness, font_thickness)
    {
        cv::Mat mat = cv::Mat(m_height, m_width, CV_8UC4, buffer, m_stride);
        m_matrices.push_back(mat);
    };
    virtual hailo_mat_t get_type()
    {
        return HAILO_MAT_RGBA;
    }
    virtual void draw_rectangle(cv::Rect rect, const cv::Scalar color)
    {
        cv::rectangle(m_matrices[0], rect, get_rgba_color(color), m_line_thickness);
    }
    virtual void draw_text(std::string text, cv::Point position, double font_scale, const cv::Scalar color)
    {
        cv::putText(m_matrices[0], text, position, cv::FONT_HERSHEY_SIMPLEX, font_scale, get_rgba_color(color),
                    m_font_thickness);
    }
    virtual void draw_line(cv::Point point1, cv::Point point2, const cv::Scalar color, int thickness, int line_type)
    {
        cv::line(m_matrices[0], point1, point2, get_rgba_color(color), thickness, line_type);
    }
    virtual void draw_ellipse(cv::Point center, cv::Size axes, double angle, double start_angle, double end_angle,
                              const cv::Scalar color, int thickness)
    {
        cv::ellipse(m_matrices[0], center, axes, angle, start_angle, end_angle, get_rgba_color(color), thickness);
    }
    virtual void blur(cv::Rect rect, cv::Size ksize)
    {
        cv::Mat target_roi = this->m_matrices[0](rect);
        cv::blur(target_roi, target_roi, ksize);
    }
    virtual ~HailoRGBAMat()
    {
        m_matrices.clear();
    }
};

class HailoYUY2Mat : public HailoMat
{
  protected:
    cv::Scalar get_yuy2_color(cv::Scalar rgb_color)
    {
        uint r = rgb_color[0];
        uint g = rgb_color[1];
        uint b = rgb_color[2];
        uint y = RGB2Y(r, g, b);
        uint u = RGB2U(r, g, b);
        uint v = RGB2V(r, g, b);
        return cv::Scalar(y, u, y, v);
    }

  public:
    HailoYUY2Mat(uint8_t *buffer, uint height, uint width, uint stride, int line_thickness = 1, int font_thickness = 1)
        : HailoMat(height, width, stride, line_thickness, font_thickness)
    {
        m_width = m_width / 2;
        cv::Mat mat = cv::Mat(m_height, m_width, CV_8UC4, buffer, m_stride);
        m_matrices.push_back(mat);
    };
    virtual hailo_mat_t get_type()
    {
        return HAILO_MAT_YUY2;
    }
    virtual void draw_rectangle(cv::Rect rect, const cv::Scalar color)
    {
        cv::Rect fixed_rect = cv::Rect(rect.x / 2, rect.y, rect.width / 2, rect.height);
        cv::rectangle(m_matrices[0], fixed_rect, get_yuy2_color(color), m_line_thickness);
    }
    virtual void draw_text(std::string text, cv::Point position, double font_scale, const cv::Scalar color) {};
    virtual void draw_line(cv::Point point1, cv::Point point2, const cv::Scalar color, int thickness, int line_type) {};
    virtual void draw_ellipse(cv::Point center, cv::Size axes, double angle, double start_angle, double end_angle,
                              const cv::Scalar color, int thickness) {};
    virtual void blur(cv::Rect rect, cv::Size ksize) {};
    virtual ~HailoYUY2Mat()
    {
        m_matrices.clear();
    }
};

class HailoNV12Mat : public HailoMat
{
    /**
        NV12 Layout in memory (planar YUV 4:2:0):

        +-----+-----+-----+-----+-----+-----+
        | Y0  | Y1  | Y2  | Y3  | Y4  | Y5  |
        +-----+-----+-----+-----+-----+-----+
        | Y6  | ... | ... | ... | ... | ... |
        +-----+-----+-----+-----+-----+-----+
        | Y12 | ... | ... | ... | ... | ... |
        +-----+-----+-----+-----+-----+-----+
        | Y18 | ... | ... | ... | ... | ... |
        +-----+-----+-----+-----+-----+-----+
        | Y24 | ... | ... | ... | ... | ... |
        +-----+-----+-----+-----+-----+-----+
        | Y30 | Y31 | Y32 | Y33 | Y34 | Y35 |
        +-----+-----+-----+-----+-----+-----+
        | U0  | V0  | U1  | V1  | U2  | V2  |
        +-----+-----+-----+-----+-----+-----+
        | U3  | V3  | ... | ... | ... | ... |
        +-----+-----+-----+-----+-----+-----+
        | U6  | V6  | U7  | V7  | U8  | V8  |
        +-----+-----+-----+-----+-----+-----+
    */
  protected:
    uint m_y_stride;
    uint m_uv_stride;
    cv::Scalar get_nv12_color(cv::Scalar rgb_color)
    {
        uint r = rgb_color[0];
        uint g = rgb_color[1];
        uint b = rgb_color[2];
        uint y = RGB2Y(r, g, b);
        uint u = RGB2U(r, g, b);
        uint v = RGB2V(r, g, b);
        return cv::Scalar(y, u, v);
    }

  public:
    HailoNV12Mat(uint8_t *buffer, uint height, uint width, uint y_plane_stride, uint uv_plane_stride,
                 int line_thickness = 1, int font_thickness = 1, void *plane0 = nullptr, void *plane1 = nullptr)
        : HailoMat(height, width, y_plane_stride, line_thickness, font_thickness)
    {
        m_height = (m_height * 3 / 2);
        m_y_stride = y_plane_stride;
        m_uv_stride = uv_plane_stride;

        if (plane0 == nullptr)
            plane0 = (char *)buffer;
        if (plane1 == nullptr)
        {
            plane1 = (char *)buffer + ((m_native_height)*m_uv_stride);
        }
        cv::Mat y_plane_mat = cv::Mat(m_native_height, m_width, CV_8UC1, plane0, y_plane_stride);
        cv::Mat uv_plane_mat = cv::Mat(m_native_height / 2, m_native_width / 2, CV_8UC2, plane1, uv_plane_stride);
        m_matrices.push_back(y_plane_mat);
        m_matrices.push_back(uv_plane_mat);
    };
    virtual hailo_mat_t get_type()
    {
        return HAILO_MAT_NV12;
    }

    /**
     * @brief Draws a NEON-accelerated 1-pixel border rectangle directly into NV12 image planes.
     *
     * Revised to include:
     *  - bounds-clamping & early exit
     *  - clearer snake_case names
     *  - DRY helpers for horizontal edges
     *  - explicit UV row calculation
     *  - ptrdiff_t for strides
     */
    void draw_rectangle_neon(const cv::Rect &rect, const cv::Scalar &bgr_color)
    {
        // 1. Convert BGR → NV12 YUV bytes
        cv::Scalar yuv_color = get_nv12_color(bgr_color);
        uint8_t y_byte = static_cast<uint8_t>(yuv_color[0]);
        uint8_t u_byte = static_cast<uint8_t>(yuv_color[1]);
        uint8_t v_byte = static_cast<uint8_t>(yuv_color[2]);

        // 2. Floor all coords/sizes to even for NV12 chroma alignment
        int aligned_x = floor_to_even_number(rect.x);
        int aligned_y = floor_to_even_number(rect.y);
        int aligned_width = floor_to_even_number(rect.width);
        int aligned_height = floor_to_even_number(rect.height);

        // 3. Clamp to image bounds and re-floor to even
        int img_width = m_matrices[0].cols;
        int img_height = m_matrices[0].rows;

        aligned_x = std::clamp(aligned_x, 0, img_width - 2);
        aligned_y = std::clamp(aligned_y, 0, img_height - 2);
        aligned_width = std::min(aligned_width, img_width - aligned_x);
        aligned_height = std::min(aligned_height, img_height - aligned_y);

        aligned_width = floor_to_even_number(aligned_width);
        aligned_height = floor_to_even_number(aligned_height);
        if (aligned_width < 2 || aligned_height < 2)
            return; // nothing to draw

        // 4. Compute half-res UV coords & rows
        int uv_x = aligned_x / 2;
        int uv_width = aligned_width / 2;
        int uv_row_top = aligned_y / 2;
        int uv_height = aligned_height / 2;
        int uv_row_bottom = uv_row_top + uv_height - 1;

        // 5. Grab Y and UV plane pointers + strides
        const cv::Mat &y_mat = m_matrices[0];
        const cv::Mat &uv_mat = m_matrices[1];
        uint8_t *y_plane = y_mat.data;
        uint8_t *uv_plane = uv_mat.data;
        ptrdiff_t y_stride = static_cast<ptrdiff_t>(y_mat.step[0]);
        ptrdiff_t uv_stride = static_cast<ptrdiff_t>(uv_mat.step[0]);

        // 6. Broadcast color into NEON registers
        uint8x16_t neon_y = vdupq_n_u8(y_byte);
        uint8x16_t neon_u = vdupq_n_u8(u_byte);
        uint8x16_t neon_v = vdupq_n_u8(v_byte);
        uint8x16x2_t neon_uv = {neon_u, neon_v};

        auto draw_horiz_y = [&](int row) {
            uint8_t *p = y_plane + ptrdiff_t(row) * y_stride + aligned_x;
            int rem = aligned_width;
            while (rem >= 16)
            {
                vst1q_u8(p, neon_y);
                p += 16;
                rem -= 16;
            }
            while (rem--)
            {
                *p++ = y_byte;
            }
        };

        auto draw_horiz_uv = [&](int uv_row) {
            uint8_t *p = uv_plane + ptrdiff_t(uv_row) * uv_stride + uv_x * 2;
            int rem = uv_width;
            while (rem >= 16)
            {
                vst2q_u8(p, neon_uv);
                p += 32;
                rem -= 16;
            }
            while (rem--)
            {
                *p++ = u_byte;
                *p++ = v_byte;
            }
        };

        // 7. Draw horizontal edges
        draw_horiz_y(aligned_y);
        draw_horiz_y(aligned_y + aligned_height - 1);
        draw_horiz_uv(uv_row_top);
        draw_horiz_uv(uv_row_bottom);

        // 8. Draw vertical edges on Y plane
        for (int row = aligned_y; row < aligned_y + aligned_height; ++row)
        {
            uint8_t *row_ptr = y_plane + ptrdiff_t(row) * y_stride;
            row_ptr[aligned_x] = y_byte;
            row_ptr[aligned_x + aligned_width - 1] = y_byte;
        }

        // 9. Draw vertical edges on UV plane
        for (int uv_row = uv_row_top; uv_row < uv_row_top + uv_height; ++uv_row)
        {
            uint8_t *row_ptr = uv_plane + ptrdiff_t(uv_row) * uv_stride + uv_x * 2;
            // left
            row_ptr[0] = u_byte;
            row_ptr[1] = v_byte;
            // right
            row_ptr[(uv_width - 1) * 2 + 0] = u_byte;
            row_ptr[(uv_width - 1) * 2 + 1] = v_byte;
        }
    }

    void draw_rectangle_opencv(cv::Rect rect, const cv::Scalar color)
    {
        cv::Scalar yuv_color = get_nv12_color(color);
        uint thickness = m_line_thickness > 1 ? m_line_thickness / 2 : 1;
        // always floor the rect coordinates to even numbers to avoid drawing on the wrong pixel
        int y_plane_rect_x = floor_to_even_number(rect.x);
        int y_plane_rect_y = floor_to_even_number(rect.y);
        int y_plane_rect_width = floor_to_even_number(rect.width);
        int y_plane_rect_height = floor_to_even_number(rect.height);

        cv::Rect y1_rect = cv::Rect(y_plane_rect_x, y_plane_rect_y, y_plane_rect_width, y_plane_rect_height);
        cv::Rect y2_rect =
            cv::Rect(y_plane_rect_x + 1, y_plane_rect_y + 1, y_plane_rect_width - 2, y_plane_rect_height - 2);
        cv::rectangle(m_matrices[0], y1_rect, cv::Scalar(yuv_color[0]), thickness);
        cv::rectangle(m_matrices[0], y2_rect, cv::Scalar(yuv_color[0]), thickness);

        cv::Rect uv_rect =
            cv::Rect(y_plane_rect_x / 2, y_plane_rect_y / 2, y_plane_rect_width / 2, y_plane_rect_height / 2);
        cv::rectangle(m_matrices[1], uv_rect, cv::Scalar(yuv_color[1], yuv_color[2]), thickness);
    }

    virtual void draw_rectangle(cv::Rect rect, const cv::Scalar color)
    {
#ifdef __ARM_NEON
        draw_rectangle_neon(rect, color);
#else
        draw_rectangle_opencv(rect, color);
#endif
    }

    virtual void draw_text(std::string text, cv::Point position, double font_scale, const cv::Scalar color)
    {
        cv::Scalar yuv_color = get_nv12_color(color);
        cv::Point y_position = cv::Point(position.x, position.y);
        cv::Point uv_position = cv::Point(position.x / 2, position.y / 2);
        cv::putText(m_matrices[0], text, y_position, cv::FONT_HERSHEY_SIMPLEX, font_scale, cv::Scalar(yuv_color[0]),
                    m_font_thickness);
        cv::putText(m_matrices[1], text, uv_position, cv::FONT_HERSHEY_SIMPLEX, font_scale / 2,
                    cv::Scalar(yuv_color[1], yuv_color[2]), m_font_thickness / 2);
    };

    virtual void draw_line(cv::Point point1, cv::Point point2, const cv::Scalar color, int thickness, int line_type)
    {
        cv::Scalar yuv_color = get_nv12_color(color);

        int y_plane_x1_value = floor_to_even_number(point1.x);
        int y_plane_y1_value = floor_to_even_number(point1.y);
        int y_plane_x2_value = floor_to_even_number(point2.x);
        int y_plane_y2_value = floor_to_even_number(point2.y);

        cv::line(m_matrices[1], cv::Point(y_plane_x1_value / 2, y_plane_y1_value / 2),
                 cv::Point(y_plane_x2_value / 2, y_plane_y2_value / 2), cv::Scalar(yuv_color[1], yuv_color[2]),
                 thickness, line_type);

        switch (line_orientation(point1, point2))
        {
        case HORIZONTAL:
            cv::line(m_matrices[0], cv::Point(y_plane_x1_value, y_plane_y1_value),
                     cv::Point(y_plane_x2_value, y_plane_y2_value), cv::Scalar(yuv_color[0]), thickness, line_type);
            cv::line(m_matrices[0], cv::Point(y_plane_x1_value, y_plane_y1_value + 1),
                     cv::Point(y_plane_x2_value, y_plane_y2_value + 1), cv::Scalar(yuv_color[0]), thickness, line_type);
            break;
        case VERTICAL:
            cv::line(m_matrices[0], cv::Point(y_plane_x1_value, y_plane_y1_value),
                     cv::Point(y_plane_x2_value, y_plane_y2_value), cv::Scalar(yuv_color[0]), thickness, line_type);
            cv::line(m_matrices[0], cv::Point(y_plane_x1_value + 1, y_plane_y1_value),
                     cv::Point(y_plane_x2_value + 1, y_plane_y2_value), cv::Scalar(yuv_color[0]), thickness, line_type);
            break;
        case DIAGONAL:
            cv::line(m_matrices[0], cv::Point(y_plane_x1_value, y_plane_y1_value),
                     cv::Point(y_plane_x2_value, y_plane_y2_value), cv::Scalar(yuv_color[0]), thickness, line_type);
            cv::line(m_matrices[0], cv::Point(y_plane_x1_value + 1, y_plane_y1_value + 1),
                     cv::Point(y_plane_x2_value + 1, y_plane_y2_value + 1), cv::Scalar(yuv_color[0]), thickness,
                     line_type);
            break;
        case ANTI_DIAGONAL:
            cv::line(m_matrices[0], cv::Point(y_plane_x1_value, y_plane_y1_value),
                     cv::Point(y_plane_x2_value, y_plane_y2_value), cv::Scalar(yuv_color[0]), thickness, line_type);
            cv::line(m_matrices[0], cv::Point(y_plane_x1_value + 1, y_plane_y1_value),
                     cv::Point(y_plane_x2_value + 1, y_plane_y2_value), cv::Scalar(yuv_color[0]), thickness, line_type);
            break;
        default:
            break;
        }
    }

    virtual void draw_ellipse(cv::Point center, cv::Size axes, double angle, double start_angle, double end_angle,
                              const cv::Scalar color, int thickness)
    {
        thickness = 2;
        // Wrap the mat with Y and UV channel windows
        cv::Scalar yuv_color = get_nv12_color(color);

        cv::Point y_position = cv::Point(floor_to_even_number(center.x), floor_to_even_number(center.y));
        cv::Point uv_position = cv::Point(floor_to_even_number(center.x) / 2, floor_to_even_number(center.y) / 2);

        cv::ellipse(m_matrices[0], y_position, {floor_to_even_number(axes.width), floor_to_even_number(axes.height)},
                    angle, start_angle, end_angle, cv::Scalar(yuv_color[0]), thickness / 2);
        cv::ellipse(m_matrices[0], y_position,
                    {floor_to_even_number(axes.width) + 1, floor_to_even_number(axes.height) + 1}, angle, start_angle,
                    end_angle, cv::Scalar(yuv_color[0]), thickness / 2);
        cv::ellipse(m_matrices[1], uv_position, axes / 2, angle, start_angle, end_angle,
                    cv::Scalar(yuv_color[1], yuv_color[2]), thickness);
    }

    virtual void blur(cv::Rect rect, cv::Size ksize)
    {
        cv::Rect y_rect = cv::Rect(rect.x, rect.y, rect.width, rect.height);
        cv::Mat target_roi_y = this->m_matrices[0](y_rect);
        cv::blur(target_roi_y, target_roi_y, ksize);
    }

    virtual cv::Rect get_crop_rect(HailoROIPtr crop_roi)
    {
        auto bbox = hailo_common::create_flattened_bbox(crop_roi->get_bbox(), crop_roi->get_scaling_bbox());

        // The Y channel is packed before the U & V on it's own
        cv::Rect y_rect = get_bounding_rect(bbox, m_native_width, m_native_height);
        // y_rect values should be even (round if necessary)
        y_rect.width = floor_to_even_number(y_rect.width);
        y_rect.height = floor_to_even_number(y_rect.height);
        y_rect.x = floor_to_even_number(y_rect.x);
        y_rect.y = floor_to_even_number(y_rect.y);

        return y_rect;
    }

    virtual std::vector<cv::Mat> crop(HailoROIPtr crop_roi)
    {
        // Wrap the mat with Y and UV channel windows
        cv::Rect y_rect = get_crop_rect(crop_roi);

        // The U and V channels are interlaced together after the Y channel,
        // so they need to be cropped separately
        cv::Rect uv_rect;
        // uv_rect values should be exactly half the size of y_rect
        uv_rect.width = y_rect.width / 2;
        uv_rect.height = y_rect.height / 2;
        uv_rect.x = y_rect.x / 2;
        uv_rect.y = y_rect.y / 2;

        cv::Mat cropped_y_mat = cv::Mat(y_rect.height, y_rect.width, CV_8UC1, m_y_stride);
        cv::Mat cropped_uv_mat = cv::Mat(uv_rect.height, uv_rect.width, CV_8UC2, m_uv_stride);

        // Fill the cropped mat with the cropped channels
        m_matrices[0](y_rect).copyTo(cropped_y_mat);
        m_matrices[1](uv_rect).copyTo(cropped_uv_mat);

        // create vector that will hold the cropped mat
        std::vector<cv::Mat> cropped_mat_vec;
        cropped_mat_vec.emplace_back(std::move(cropped_y_mat));
        cropped_mat_vec.emplace_back(std::move(cropped_uv_mat));

        return cropped_mat_vec;
    }
    virtual ~HailoNV12Mat()
    {
        m_matrices.clear(); // this will call the destructor of each mat
    }
};
