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
using hailo_analytics::pipeline::Buffer;
using hailo_analytics::pipeline::BufferPtr;

#define CLIP_IMG_PREPROCESS_QUEUE_SIZE_DEFAULT 15

#define CLIP_IMG_PREPROCESS_CROP_WIDTH_LIMIT 10
#define CLIP_IMG_PREPROCESS_CROP_HEIGHT_LIMIT 10
#define CLIP_IMG_PREPROCESS_THRESHOLD 100.0

constexpr std::string_view CLASSIFICATION_TYPE_CLIP = "clip";

class ClipImagePreprocess : public hailo_analytics::pipeline::ThreadedStage
{

  public:
    bool m_enabled = true; // Enable or disable the stage

    ClipImagePreprocess(std::string name, bool enable = true,
                        size_t queue_size = CLIP_IMG_PREPROCESS_QUEUE_SIZE_DEFAULT, bool leaky = false,
                        bool trace_processing_operations = true);

    hailo_analytics::pipeline::AppStatus init() override;

    hailo_analytics::pipeline::AppStatus deinit() override;

    hailo_analytics::pipeline::AppStatus process(BufferPtr data);

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
    float quality_estimation(std::shared_ptr<HailoMat> hailo_mat, const HailoBBox &roi, const float crop_ratio = 0.1);

    std::string generate_png_filename();
};

class ClipImagePreprocessBuild : public ClipImagePreprocess
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        bool m_enable = true; // Enable or disable the stage
        size_t m_queue_size = CLIP_IMG_PREPROCESS_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_enable(bool activate);
        Builder &set_queue_size(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_trace_opt(bool activate);

        std::shared_ptr<ClipImagePreprocess> buildptr() const;
    };

    static Builder create();
};
