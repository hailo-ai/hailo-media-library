#include "clip_image_preprocess.hpp"

#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

#include <chrono>
#include <sstream>

ClipImagePreprocess::ClipImagePreprocess(std::string name, bool enable, size_t queue_size, bool leaky,
                                         bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations), m_enabled(enable)
{
}

hailo_analytics::pipeline::AppStatus ClipImagePreprocess::init()
{
    return hailo_analytics::pipeline::AppStatus::SUCCESS;
}

hailo_analytics::pipeline::AppStatus ClipImagePreprocess::deinit()
{
    return hailo_analytics::pipeline::AppStatus::SUCCESS;
}

hailo_analytics::pipeline::AppStatus ClipImagePreprocess::process(BufferPtr data)
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
        HAILO_ANALYTICS_LOG_INFO(
            "Clip Image Preprocess Stage {} calculated quality: {}, NOTE: currently still in bypass mode", m_stage_name,
            quality);
    }

#if 0 // DEBUG - Save the cropped image for inspection                        
        nv12hmat->save_image(generate_png_filename());
#endif

    // Create a deep copy of the ROI with all its detection objects cloned
    // as we will be adding classification results to it and we don't want
    // to modify the original detection objects in case other stages are using them
    // (eg, possibly propagated to overlay stage for visualization)
    auto original_roi = data->get_roi();
    HailoDetectionPtr detection = std::dynamic_pointer_cast<HailoDetection>(original_roi);

    if (!detection)
    {
        HAILO_ANALYTICS_LOG_ERROR("Clip Image Preprocess Stage {} failed to get detection from ROI", m_stage_name);
        return hailo_analytics::pipeline::AppStatus::PIPELINE_ERROR;
    }

    // Clone the detection to create an independent copy
    HailoDetectionPtr detection_copy = std::dynamic_pointer_cast<HailoDetection>(detection->clone());

    HailoClassificationPtr classification =
        std::make_shared<HailoClassification>(std::string(CLASSIFICATION_TYPE_CLIP), detection_copy->get_class_id(),
                                              detection_copy->get_label(), detection_copy->get_confidence());

    detection_copy->add_object(classification);

    // Create a new buffer with the cloned detection
    BufferPtr data_new = std::make_shared<Buffer>(data->get_buffer(), detection_copy);
    send_to_subscribers(data_new);

    return hailo_analytics::pipeline::AppStatus::SUCCESS;
}

float ClipImagePreprocess::quality_estimation(std::shared_ptr<HailoMat> hailo_mat, const HailoBBox &roi,
                                              const float crop_ratio)
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
    if (cropped_width <= CLIP_IMG_PREPROCESS_CROP_WIDTH_LIMIT ||
        cropped_height <= CLIP_IMG_PREPROCESS_CROP_HEIGHT_LIMIT)
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
        cv::Mat yuy2_image = cv::Mat(cropped_image.rows, cropped_image.cols * 2, CV_8UC2, (char *)cropped_image.data,
                                     cropped_image.step);
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

std::string ClipImagePreprocess::generate_png_filename()
{
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count();

    std::ostringstream oss;
    oss << ms << ".png";
    return oss.str();
}

ClipImagePreprocessBuild::Builder &ClipImagePreprocessBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

ClipImagePreprocessBuild::Builder &ClipImagePreprocessBuild::Builder::set_enable(bool activate)
{
    m_enable = activate;
    return *this;
}

ClipImagePreprocessBuild::Builder &ClipImagePreprocessBuild::Builder::set_queue_size(size_t size)
{
    m_queue_size = size;
    return *this;
}

ClipImagePreprocessBuild::Builder &ClipImagePreprocessBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

ClipImagePreprocessBuild::Builder &ClipImagePreprocessBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<ClipImagePreprocess> ClipImagePreprocessBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<ClipImagePreprocess>(m_stage_name.value(), m_enable, m_queue_size, m_leaky, m_trace);
}

ClipImagePreprocessBuild::Builder ClipImagePreprocessBuild::create()
{
    return Builder();
}
