#include <stddef.h>
#include <stdint.h>
#include <hailo_postprocess_tools/image_utils/hailomat.hpp>
#include <hailo_postprocess_tools/objects/hailo_common.hpp>
#include <hailo_postprocess_tools/objects/hailo_objects.hpp>
#include <media_library/buffer_pool.hpp>
#include <media_library/dma_memory_allocator.hpp>
#include <media_library/media_library_buffer.hpp>
#include <media_library/media_library_types.hpp>
#include <opencv2/core.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_postprocess_tools/image_utils/overlay_native.hpp"
#include "hailo_analytics/pipeline/overlay/overlay_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::overlay
{
/**
 * @brief Constructor for OverlayStage.
 * @param name The name of the stage.
 * @param queue_size Size of the queue for this stage.
 * @param leaky Indicates if the queue is leaky.
 * @param print_fps Flag to enable or disable printing FPS information.
 */
OverlayStage::OverlayStage(std::string name, bool skip, bool partial_landmarks,
                           std::unordered_set<size_t> landmark_indices_to_draw, size_t queue_size, bool leaky,
                           std::unordered_set<int> class_ids_to_draw,
                           std::function<cv::Scalar(const HailoDetectionPtr &)> color_selector,
                           bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations), m_skip(skip),
      m_partial_landmarks(partial_landmarks), m_landmark_indices_to_draw(std::move(landmark_indices_to_draw)),
      m_class_ids_to_draw(std::move(class_ids_to_draw)), m_color_selector(color_selector)
{
}

/**
 * @brief Initialize the overlay stage.
 * @return Status of the initialization.
 */
AppStatus OverlayStage::init()
{
    /* Set overlay default values */
    m_hailooverlay_info.line_thickness = 1;
    m_hailooverlay_info.font_thickness = 1;
    m_hailooverlay_info.face_blur = false;
    m_hailooverlay_info.show_confidence = true;
    m_hailooverlay_info.local_gallery = false;
    m_hailooverlay_info.landmark_point_radius = 3;
    m_hailooverlay_info.mask_overlay_n_threads = 0;
    return AppStatus::SUCCESS;
}

/**
 * @brief Deinitialize the overlay stage.
 * @return Status of the deinitialization.
 */
AppStatus OverlayStage::deinit()
{
    return AppStatus::SUCCESS;
}

/**
 * @brief Process the given data buffer and apply overlay.
 * @param data The data buffer to process.
 * @return Status of the processing.
 */
AppStatus OverlayStage::process(BufferPtr data)
{
    if (m_skip)
    {
        send_to_subscribers(data);
        return AppStatus::SUCCESS;
    }
    std::shared_ptr<HailoMat> hmat = std::make_shared<HailoNV12Mat>(
        (uint8_t *)data->get_buffer()->get_plane_ptr(0), data->get_buffer()->buffer_data->height,
        data->get_buffer()->buffer_data->width, data->get_buffer()->get_plane_stride(0),
        data->get_buffer()->get_plane_stride(1), m_hailooverlay_info.line_thickness, m_hailooverlay_info.font_thickness,
        (uint8_t *)data->get_buffer()->get_plane_ptr(0), (uint8_t *)data->get_buffer()->get_plane_ptr(1));

    if (hmat)
    {
        auto detections = hailo_common::get_hailo_detections(data->get_roi());
        if (!detections.empty())
        {
            std::unordered_map<std::string, int> label_count;
            for (const auto &detection : detections)
                ++label_count[detection->get_label()];

            std::ostringstream labels_stream;
            for (const auto &[label, count] : label_count)
                labels_stream << (labels_stream.tellp() ? ", " : "") << label << ": " << count;

            HAILO_ANALYTICS_LOG_TRACE("Overlay stage: Processing frame with {}", labels_stream.str());
        }

        if (DmaMemoryAllocator::get_instance().dmabuf_sync_start(data->get_buffer()->get_plane_ptr(0)) !=
            MEDIA_LIBRARY_SUCCESS)
            return AppStatus::DMA_ERROR;
        if (DmaMemoryAllocator::get_instance().dmabuf_sync_start(data->get_buffer()->get_plane_ptr(1)) !=
            MEDIA_LIBRARY_SUCCESS)
            return AppStatus::DMA_ERROR;
        // Blur faces if face-blur is activated.
        if (m_hailooverlay_info.face_blur)
        {
            face_blur(*hmat.get(), data->get_roi());
        }
        // Draw all results of the given roi on mat.
        overlay_status_t ret = draw_all(*hmat.get(), data->get_roi(), m_hailooverlay_info.landmark_point_radius,
                                        m_hailooverlay_info.show_confidence, m_hailooverlay_info.local_gallery,
                                        m_hailooverlay_info.mask_overlay_n_threads, m_partial_landmarks,
                                        m_landmark_indices_to_draw, m_class_ids_to_draw, m_color_selector);
        if (ret != OVERLAY_STATUS_OK)
        {
            HAILO_ANALYTICS_LOG_ERROR("Overlay failure draw_all failed, status = {}", ret);
        }
        if (DmaMemoryAllocator::get_instance().dmabuf_sync_end(data->get_buffer()->get_plane_ptr(0)) !=
            MEDIA_LIBRARY_SUCCESS)
            return AppStatus::DMA_ERROR;
        if (DmaMemoryAllocator::get_instance().dmabuf_sync_end(data->get_buffer()->get_plane_ptr(1)) !=
            MEDIA_LIBRARY_SUCCESS)
            return AppStatus::DMA_ERROR;
    }

    send_to_subscribers(data);

    return AppStatus::SUCCESS;
}

/**
 * @brief Set the overlay skip flag.
 * @param skip Flag to set the skip state.
 */
void OverlayStage::set_skip(bool skip)
{
    m_skip = skip;
}

/**
 * @brief Get the overlay skip flag.
 * @return Current skip state.
 */
bool OverlayStage::get_skip()
{
    return m_skip;
}

OverlayStageBuild::Builder &OverlayStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}
OverlayStageBuild::Builder &OverlayStageBuild::Builder::set_skip_opt(bool skip)
{
    m_skip = skip;
    return *this;
}
OverlayStageBuild::Builder &OverlayStageBuild::Builder::set_partial_landmarks(bool partial_landmarks)
{
    m_partial_landmarks = partial_landmarks;
    return *this;
}
OverlayStageBuild::Builder &OverlayStageBuild::Builder::set_landmark_indices_to_draw(std::unordered_set<size_t> indices)
{
    m_landmark_indices_to_draw = std::move(indices);
    return *this;
}
OverlayStageBuild::Builder &OverlayStageBuild::Builder::set_queue_size(size_t size)
{
    m_queue_size = size;
    return *this;
}
OverlayStageBuild::Builder &OverlayStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}
OverlayStageBuild::Builder &OverlayStageBuild::Builder::set_class_ids_to_draw(std::unordered_set<int> class_ids_to_draw)
{
    m_class_ids_to_draw = class_ids_to_draw;
    return *this;
}
OverlayStageBuild::Builder &OverlayStageBuild::Builder::set_color_selector(
    std::function<cv::Scalar(const HailoDetectionPtr &)> color_selector)
{
    m_color_selector = color_selector;
    return *this;
}
OverlayStageBuild::Builder &OverlayStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<OverlayStage> OverlayStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<OverlayStage>(m_stage_name.value(), m_skip, m_partial_landmarks, m_landmark_indices_to_draw,
                                          m_queue_size, m_leaky, m_class_ids_to_draw, m_color_selector, m_trace);
}

OverlayStageBuild::Builder OverlayStageBuild::create()
{
    return OverlayStageBuild::Builder();
}

} // namespace hailo_analytics::pipeline::overlay
