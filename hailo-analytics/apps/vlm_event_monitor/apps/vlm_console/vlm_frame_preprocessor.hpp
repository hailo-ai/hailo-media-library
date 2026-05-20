#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <tl/expected.hpp>

/**
 * @brief Converts JPEG-encoded frames into model-ready RGB pixel buffers.
 *
 * Each preprocessed output is a flat byte buffer of exactly
 * `target_height * target_width * target_channels` bytes, ready to be wrapped
 * in a hailort::MemoryView and passed to the VLM.
 *
 * Preprocessing steps:
 *   1. JPEG decode  (cv::imdecode)
 *   2. Letterbox resize to (height, width), preserving aspect ratio with
 *      black padding on the short axis.
 *   3. BGR → RGB channel order conversion.
 */
class VlmFramePreprocessor
{
  public:
    VlmFramePreprocessor(uint32_t target_height, uint32_t target_width, uint32_t target_channels);

    VlmFramePreprocessor(const VlmFramePreprocessor &) = delete;
    VlmFramePreprocessor &operator=(const VlmFramePreprocessor &) = delete;

    tl::expected<std::vector<uint8_t>, std::string> preprocess_jpeg(const std::vector<uint8_t> &jpeg_data) const;

    uint32_t target_height() const
    {
        return m_height;
    }
    uint32_t target_width() const
    {
        return m_width;
    }
    uint32_t target_channels() const
    {
        return m_channels;
    }
    size_t output_size() const
    {
        return static_cast<size_t>(m_height) * m_width * m_channels;
    }

  private:
    uint32_t m_height;
    uint32_t m_width;
    uint32_t m_channels;
};
