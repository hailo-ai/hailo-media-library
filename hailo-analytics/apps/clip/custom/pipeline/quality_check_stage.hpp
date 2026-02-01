#pragma once

// General includes
#include <cstdint>
#include <memory>
#include <string>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <sstream>

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"

#include "hailo_postprocess_tools/image_utils/hailomat.hpp"

// Using declarations for pipeline types
using hailo_analytics::pipeline::BufferPtr;

#define QUALITY_QUEUE_SIZE_DEFAULT 15

#define QUALITY_CHECK_CROP_WIDTH_LIMIT 10
#define QUALITY_CHECK_CROP_HEIGHT_LIMIT 10
#define QUALITY_CHECK_THRESHOLD 100.0

class QualityCheckStage : public hailo_analytics::pipeline::ThreadedStage
{

  public:
    bool m_enabled = true; // Enable or disable the stage

    inline QualityCheckStage(std::string name, bool enable = true, size_t queue_size = QUALITY_QUEUE_SIZE_DEFAULT,
                             bool leaky = false, bool trace_processing_operations = true)
        : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
          m_enabled(enable)
    {
    }

    inline hailo_analytics::pipeline::AppStatus init() override
    {
        return hailo_analytics::pipeline::AppStatus::SUCCESS;
    }

    inline hailo_analytics::pipeline::AppStatus deinit() override
    {
        return hailo_analytics::pipeline::AppStatus::SUCCESS;
    }

    inline hailo_analytics::pipeline::AppStatus process(BufferPtr data)
    {
        if (m_enabled)
        {
            HailoBBox bbox = {0.0, 0.0, 1.0, 1.0}; // Default bbox

            std::shared_ptr<HailoNV12Mat> nv12hmat = std::make_shared<HailoNV12Mat>(
                (uint8_t *)data->get_buffer()->get_plane_ptr(0), data->get_buffer()->buffer_data->height,
                data->get_buffer()->buffer_data->width, data->get_buffer()->get_plane_stride(0),
                data->get_buffer()->get_plane_stride(1), 1, 1, (uint8_t *)data->get_buffer()->get_plane_ptr(0),
                (uint8_t *)data->get_buffer()->get_plane_ptr(1));

            float quality = quality_estimation(nv12hmat, bbox, 0.1);
            std::cout << "Best Shot Quality: " << quality << ", NOTE: currently still in bypass mode" << std::endl;
        }

#if 0 // Aaron Test                        
        nv12hmat->save_image(generate_png_filename());
#endif

        send_to_subscribers(data);

        return hailo_analytics::pipeline::AppStatus::SUCCESS;
    }

  private:
    /**
     * @brief Returns the calculate the variance of edges.
     *
     * @param image  -  cv::Mat
     *        The original image.
     *
     * @param roi  -  HailoBBox
     *        The ROI to read from the image
     *
     * @param crop_ratio  -  float
     *        The percent of the image to crop in from the edges (default 10%).
     *
     * @return float
     *         The variance of edges in the image.
     */
    float quality_estimation(std::shared_ptr<HailoMat> hailo_mat, const HailoBBox &roi, const float crop_ratio = 0.1)
    {
        // Crop the center of the roi from the image, avoid cropping out of bounds
        float roi_width = roi.width();
        float roi_height = roi.height();
        float roi_xmin = roi.xmin();
        float roi_ymin = roi.ymin();
        float roi_xmax = roi.xmax();
        float roi_ymax = roi.ymax();
        float x_offset = roi_width * crop_ratio;
        float y_offset = roi_height * crop_ratio;
        float cropped_xmin = CLAMP(roi_xmin + x_offset, 0, 1);
        float cropped_ymin = CLAMP(roi_ymin + y_offset, 0, 1);
        float cropped_xmax = CLAMP(roi_xmax - x_offset, cropped_xmin, 1);
        float cropped_ymax = CLAMP(roi_ymax - y_offset, cropped_ymin, 1);
        float cropped_width_n = cropped_xmax - cropped_xmin;
        float cropped_height_n = cropped_ymax - cropped_ymin;
        int cropped_width = int(cropped_width_n * hailo_mat->native_width());
        int cropped_height = int(cropped_height_n * hailo_mat->native_height());

        // If the cropepd image is too small then quality is zero
        if (cropped_width <= QUALITY_CHECK_CROP_WIDTH_LIMIT || cropped_height <= QUALITY_CHECK_CROP_HEIGHT_LIMIT)
            return -1.0;

        // If it is not too small then we can make the crop
        HailoROIPtr crop_roi =
            std::make_shared<HailoROI>(HailoBBox(cropped_xmin, cropped_ymin, cropped_width_n, cropped_height_n));
        std::vector<cv::Mat> cropped_image_vec = hailo_mat->crop(crop_roi);

        // Convert image to BGR
        cv::Mat bgr_image;
        switch (hailo_mat->get_type())
        {
        case HAILO_MAT_YUY2: {
            cv::Mat cropped_image = cropped_image_vec[0];
            cv::Mat yuy2_image = cv::Mat(cropped_image.rows, cropped_image.cols * 2, CV_8UC2,
                                         (char *)cropped_image.data, cropped_image.step);
            cv::cvtColor(yuy2_image, bgr_image, cv::COLOR_YUV2BGR_YUY2);
            break;
        }
        case HAILO_MAT_NV12: {
            cv::Mat full_mat =
                cv::Mat(cropped_image_vec[0].rows + cropped_image_vec[1].rows, cropped_image_vec[0].cols, CV_8UC1);
            memcpy(full_mat.data, cropped_image_vec[0].data, cropped_image_vec[0].rows * cropped_image_vec[0].cols);
            memcpy(full_mat.data + cropped_image_vec[0].rows * cropped_image_vec[0].cols, cropped_image_vec[1].data,
                   cropped_image_vec[1].rows * cropped_image_vec[1].cols);
            cv::cvtColor(full_mat, bgr_image, cv::COLOR_YUV2BGR_NV12);

            break;
        }
        default:
            bgr_image = cropped_image_vec[0];
            break;
        }

#if 1 // With Gaussian blur

        // Gaussian Blur
        cv::Mat gaussian_image;
        cv::GaussianBlur(bgr_image, gaussian_image, cv::Size(3, 3), 0);

        // Convert to grayscale
        cv::Mat gray_image;
        cv::Mat gray_image_normalized;
        cv::cvtColor(gaussian_image, gray_image, cv::COLOR_BGR2GRAY);
        cv::normalize(gray_image, gray_image_normalized, 255, 0, cv::NORM_INF);

#else

        // Convert to grayscale
        cv::Mat gray_image;
        cv::Mat gray_image_normalized;
        cv::cvtColor(bgr_image, gray_image, cv::COLOR_BGR2GRAY);
        cv::normalize(gray_image, gray_image_normalized, 255, 0, cv::NORM_INF);

#endif
        // Compute the Laplacian of the gray image
        cv::Mat laplacian_image;
        cv::Laplacian(gray_image_normalized, laplacian_image, CV_64F);

        // Calculate the variance of edges
        cv::Scalar mean, stddev;
        cv::meanStdDev(laplacian_image, mean, stddev, cv::Mat());
        float variance = stddev.val[0] * stddev.val[0];
        return variance;
    }

    std::string generate_png_filename()
    {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count();

        std::ostringstream oss;
        oss << ms << ".png";
        return oss.str();
    }
};

class QualityCheckStageBuild : public QualityCheckStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        bool m_enable = true; // Enable or disable the stage
        size_t m_queue_size = QUALITY_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name)
        {
            m_stage_name = name;
            return *this;
        }
        Builder &set_enable(bool activate)
        {
            m_enable = activate;
            return *this;
        }
        Builder &set_queue_size(size_t size)
        {
            m_queue_size = size;
            return *this;
        }
        Builder &set_leaky_opt(bool activate)
        {
            m_leaky = activate;
            return *this;
        }
        Builder &set_trace_opt(bool activate)
        {
            m_trace = activate;
            return *this;
        }

        std::shared_ptr<QualityCheckStage> buildptr() const
        {
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

            return std::make_shared<QualityCheckStage>(m_stage_name.value(), m_enable, m_queue_size, m_leaky, m_trace);
        }
    };

    static Builder create()
    {
        return Builder();
    }
};
