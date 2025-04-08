#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/core/core.hpp"

#include "buffer_pool.hpp"
#include "config_manager.hpp"
#include "dsp_utils.hpp"
#include "media_library_logger.hpp"
#include "media_library_utils.hpp"
#include "motion_detection.hpp"
#include <iostream>
#include <stdint.h>
#include <string>
#include <time.h>
#include <tl/expected.hpp>
#include <vector>
#include <shared_mutex>

#define MODULE_NAME LoggerType::MotionDetection

MotionDetection::MotionDetection()
{
    m_motion_detection_config = {};
    m_motion_detection_buffer_pool = nullptr;
    m_motion_detection_previous_buffer_ptr = nullptr;
    m_motion_detection_roi = cv::Rect(0, 0, 0, 0);
}

MotionDetection::MotionDetection(motion_detection_config_t &motion_detection_config)
{
    m_motion_detection_config = motion_detection_config;
    m_motion_detection_buffer_pool = nullptr;
    m_motion_detection_previous_buffer_ptr = nullptr;
}

media_library_return MotionDetection::allocate_motion_detection(uint32_t max_buffer_pool_size)
{
    m_motion_detection_roi = cv::Rect(m_motion_detection_config.roi.x, m_motion_detection_config.roi.y,
                                      m_motion_detection_config.roi.width, m_motion_detection_config.roi.height);

    if (m_motion_detection_buffer_pool != nullptr &&
        m_motion_detection_buffer_pool->get_width() ==
            m_motion_detection_config.resolution.dimensions.destination_width &&
        m_motion_detection_buffer_pool->get_height() ==
            m_motion_detection_config.resolution.dimensions.destination_height &&
        max_buffer_pool_size == m_motion_detection_buffer_pool->get_size())
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Buffer pool already exists, skipping creation");
        return MEDIA_LIBRARY_SUCCESS;
    }
    std::string name = "motion_detection_bitmask";
    auto bytes_per_line =
        dsp_utils::get_dsp_desired_stride_from_width(m_motion_detection_config.resolution.dimensions.destination_width);
    LOGGER__MODULE__INFO(
        MODULE_NAME,
        "Creating buffer pool named {} for output resolution: width {} height {} in buffers size of {} and "
        "bytes per line {}",
        name, m_motion_detection_config.resolution.dimensions.destination_width,
        m_motion_detection_config.resolution.dimensions.destination_height, max_buffer_pool_size, bytes_per_line);
    MediaLibraryBufferPoolPtr buffer_pool = std::make_shared<MediaLibraryBufferPool>(
        m_motion_detection_config.resolution.dimensions.destination_width,
        m_motion_detection_config.resolution.dimensions.destination_height, HAILO_FORMAT_GRAY8, max_buffer_pool_size,
        HAILO_MEMORY_TYPE_DMABUF, bytes_per_line, name);

    if (buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init buffer pool");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }
    m_motion_detection_buffer_pool = buffer_pool;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MotionDetection::perform_motion_detection(std::vector<HailoMediaLibraryBufferPtr> &output_frames)
{
    struct timespec start_resize, end_resize;
    clock_gettime(CLOCK_MONOTONIC, &start_resize);

    HailoMediaLibraryBufferPtr motion_detection_current_buffer_ptr = output_frames.back();
    if (!is_frame_valid(motion_detection_current_buffer_ptr))
    {
        return MEDIA_LIBRARY_SUCCESS;
    }

    if (!initialize_previous_frame(motion_detection_current_buffer_ptr))
    {
        return MEDIA_LIBRARY_SUCCESS;
    }

    create_current_frame(motion_detection_current_buffer_ptr);

    HailoMediaLibraryBufferPtr bitmask_buffer = allocate_bitmask_buffer();
    if (bitmask_buffer == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to acquire buffer for motion detection");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    create_motion_mask();

    bool motion_detected = detect_motion();

    update_output_frames(output_frames, bitmask_buffer, motion_detected);

    update_previous_frame(motion_detection_current_buffer_ptr);

    clock_gettime(CLOCK_MONOTONIC, &end_resize);
    log_execution_time(start_resize, end_resize);

    return MEDIA_LIBRARY_SUCCESS;
}

// Helper Functions

bool MotionDetection::is_frame_valid(const HailoMediaLibraryBufferPtr &buffer_ptr) const
{
    return buffer_ptr && buffer_ptr->buffer_data != nullptr;
}

bool MotionDetection::initialize_previous_frame(const HailoMediaLibraryBufferPtr &buffer_ptr)
{
    if (m_motion_detection_previous_buffer_ptr == nullptr)
    {
        m_motion_detection_previous_frame = cv::Mat(buffer_ptr->buffer_data->height, buffer_ptr->buffer_data->width,
                                                    CV_8UC1, buffer_ptr->get_plane_ptr(0));
        m_motion_detection_previous_buffer_ptr = buffer_ptr;
        return false; // First frame, just initialized previous frame
    }
    return true;
}

void MotionDetection::create_current_frame(const HailoMediaLibraryBufferPtr &buffer_ptr)
{
    m_motion_detection_current_frame =
        cv::Mat(buffer_ptr->buffer_data->height, buffer_ptr->buffer_data->width, CV_8UC1, buffer_ptr->get_plane_ptr(0));
}

HailoMediaLibraryBufferPtr MotionDetection::allocate_bitmask_buffer()
{
    HailoMediaLibraryBufferPtr bitmask_buffer = std::make_shared<hailo_media_library_buffer>();
    if (m_motion_detection_buffer_pool->acquire_buffer(bitmask_buffer) != MEDIA_LIBRARY_SUCCESS)
    {
        return nullptr;
    }
    m_motion_detection_mask = cv::Mat(bitmask_buffer->buffer_data->height, bitmask_buffer->buffer_data->width, CV_8UC1,
                                      bitmask_buffer->get_plane_ptr(0));
    return bitmask_buffer;
}

void MotionDetection::create_motion_mask()
{
    cv::absdiff(m_motion_detection_current_frame, m_motion_detection_previous_frame, m_motion_detection_mask);

    auto kernel = getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(m_motion_detection_mask, m_motion_detection_mask, cv::MORPH_OPEN, kernel);

    cv::threshold(m_motion_detection_mask, m_motion_detection_mask, m_motion_detection_config.sensitivity_level, 255,
                  cv::THRESH_BINARY);
}

bool MotionDetection::detect_motion() const
{
    int threshold = m_motion_detection_mask.total() * m_motion_detection_config.threshold;
    cv::Scalar sum = cv::sum(m_motion_detection_mask(m_motion_detection_roi));
    bool motion_detected = sum[0] > threshold;

    if (motion_detected)
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "Motion detected");
    }

    return motion_detected;
}

void MotionDetection::update_output_frames(std::vector<HailoMediaLibraryBufferPtr> &output_frames,
                                           HailoMediaLibraryBufferPtr &bitmask_buffer, bool motion_detected)
{
    for (auto &frame : output_frames)
    {
        frame->motion_detection_buffer = bitmask_buffer;
        frame->motion_detected = motion_detected;
    }
}

void MotionDetection::update_previous_frame(const HailoMediaLibraryBufferPtr &current_buffer_ptr)
{
    m_motion_detection_previous_frame = m_motion_detection_current_frame;
    m_motion_detection_previous_buffer_ptr = current_buffer_ptr;
}

void MotionDetection::log_execution_time(const timespec &start, const timespec &end) const
{
    [[maybe_unused]] long ms = static_cast<long>(media_library_difftimespec_ms(end, start));
    LOGGER__MODULE__TRACE(MODULE_NAME, "perform_motion_detection took {} milliseconds ({} fps)", ms, 1000 / ms);
}
