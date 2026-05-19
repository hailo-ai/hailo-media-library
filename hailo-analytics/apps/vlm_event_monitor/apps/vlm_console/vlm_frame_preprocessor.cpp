#include "vlm_frame_preprocessor.hpp"

#include <algorithm>

#include <opencv2/opencv.hpp>

VlmFramePreprocessor::VlmFramePreprocessor(uint32_t target_height, uint32_t target_width, uint32_t target_channels)
    : m_height(target_height), m_width(target_width), m_channels(target_channels)
{
}

tl::expected<std::vector<uint8_t>, std::string> VlmFramePreprocessor::preprocess_jpeg(
    const std::vector<uint8_t> &jpeg_data) const
{
    // Decode JPEG
    cv::Mat decoded =
        cv::imdecode(cv::Mat(1, static_cast<int>(jpeg_data.size()), CV_8UC1, const_cast<uint8_t *>(jpeg_data.data())),
                     cv::IMREAD_COLOR);
    if (decoded.empty())
    {
        return tl::make_unexpected("Failed to decode JPEG frame");
    }

    const int target_w = static_cast<int>(m_width);
    const int target_h = static_cast<int>(m_height);

    // Letterbox resize: maintain aspect ratio with black padding
    const double scale =
        std::min(static_cast<double>(target_w) / decoded.cols, static_cast<double>(target_h) / decoded.rows);
    const int new_w = static_cast<int>(decoded.cols * scale);
    const int new_h = static_cast<int>(decoded.rows * scale);

    cv::Mat scaled;
    cv::resize(decoded, scaled, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);

    cv::Mat resized(target_h, target_w, decoded.type(), cv::Scalar(0, 0, 0));
    const int x_offset = (target_w - new_w) / 2;
    const int y_offset = (target_h - new_h) / 2;
    scaled.copyTo(resized(cv::Rect(x_offset, y_offset, new_w, new_h)));

    // BGR → RGB
    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

    const size_t expected = output_size();
    std::vector<uint8_t> result(rgb.data, rgb.data + expected);
    return result;
}
