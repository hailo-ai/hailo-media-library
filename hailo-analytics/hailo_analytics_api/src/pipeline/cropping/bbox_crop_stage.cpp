#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/cropping/bbox_crop_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::cropping
{

static dsp_scaling_mode_t get_scaling_mode_from_letterbox(bool use_letterbox, dsp_letterbox_alignment_t alignment)
{
    if (!use_letterbox)
    {
        return DSP_SCALING_MODE_STRETCH;
    }
    return (alignment == DSP_LETTERBOX_MIDDLE) ? DSP_SCALING_MODE_LETTERBOX_MIDDLE : DSP_SCALING_MODE_LETTERBOX_UP_LEFT;
}

BBoxCropStage::BBoxCropStage(std::string name, int output_pool_size, int input_width, int input_height,
                             int output_width, int output_height, std::string main_sub_name, std::string sub_sub_name,
                             std::vector<std::string> labels, size_t queue_size, bool leaky,
                             bool trace_processing_operations, StagePoolMode pool_mode, size_t crop_every_x_frames,
                             bool use_letterbox, dsp_letterbox_alignment_t letterbox_alignment,
                             dsp_color_t letterbox_color)
    : DspBaseCropStage(name, output_pool_size, input_width, input_height, output_width, output_height, main_sub_name,
                       sub_sub_name, queue_size, leaky, trace_processing_operations, pool_mode, crop_every_x_frames,
                       get_scaling_mode_from_letterbox(use_letterbox, letterbox_alignment), letterbox_color),
      m_target_labels(labels), m_use_letterbox(use_letterbox), m_letterbox_alignment(letterbox_alignment),
      m_letterbox_color(letterbox_color)
{
    HAILO_ANALYTICS_LOG_INFO(
        "{} Constructor: use_letterbox={}, scaling_mode={}, color=(y={}, u={}, v={})", name, use_letterbox,
        use_letterbox ? (letterbox_alignment == DSP_LETTERBOX_MIDDLE ? "LETTERBOX_MIDDLE" : "LETTERBOX_UP_LEFT")
                      : "STRETCH",
        letterbox_color.y, letterbox_color.u, letterbox_color.v);
}

AppStatus BBoxCropStage::init()
{
    auto bytes_per_line = dsp_utils::get_dsp_desired_stride_from_width(m_output_width);
    m_buffer_pool =
        std::make_shared<MediaLibraryBufferPool>(m_output_width, m_output_height, HAILO_FORMAT_NV12, m_output_pool_size,
                                                 HAILO_MEMORY_TYPE_DMABUF, bytes_per_line, "detection_buffer_pool");
    if (m_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        return AppStatus::DSP_OPERATION_ERROR;
    }

    setup_pool_notification();
    return AppStatus::SUCCESS;
}

void BBoxCropStage::prepare_crops(BufferPtr input_buffer, std::vector<dsp_crop_api_t> &crop_resize_dims)
{
    HailoMediaLibraryBufferPtr buffer = input_buffer->get_buffer();
    int input_width = buffer->buffer_data->width;
    int input_height = buffer->buffer_data->height;
    HailoROIPtr roi = input_buffer->get_roi();

    for (auto detection : hailo_common::get_hailo_detections(roi))
    {
        std::string detection_label = detection->get_label();
        if (std::find(m_target_labels.begin(), m_target_labels.end(), detection_label) != m_target_labels.end())
        {
            auto detection_bbox = detection->get_bbox();

            m_detection_crops_bbox.push_back(detection_bbox);
            m_detection_rois.push_back(detection);

            prepare_single_crop_dim(detection_bbox, crop_resize_dims, input_width, input_height);
        }
    }
}

HailoBBox BBoxCropStage::get_crop_bbox(int index)
{
    try
    {
        return m_detection_crops_bbox.at(index);
    }
    catch (const std::out_of_range &e)
    {
        HAILO_ANALYTICS_LOG_ERROR("Cropped index {} is out of bounds:{} ", index, e.what());
        throw;
    }
}

HailoROIPtr BBoxCropStage::get_crop_roi(int index)
{
    try
    {
        return m_detection_rois.at(index);
    }
    catch (const std::out_of_range &e)
    {
        HAILO_ANALYTICS_LOG_ERROR("ROI index {} is out of bounds: {}", index, e.what());
        throw;
    }
}

void BBoxCropStage::pre_crop(BufferPtr input_buffer)
{
    (void)input_buffer;
    // No pre-crop processing needed for tiling
}

void BBoxCropStage::post_crop(BufferPtr input_buffer)
{
    (void)input_buffer;
    m_detection_crops_bbox.clear();
    m_detection_rois.clear();
}

AppStatus BBoxCropStage::process(BufferPtr data)
{
    return DspBaseCropStage::process(data);
}

BBoxCropStageBuild::Builder &BBoxCropStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}
BBoxCropStageBuild::Builder &BBoxCropStageBuild::Builder::set_output_pool_size(int size)
{
    m_output_pool_size = size;
    return *this;
}
BBoxCropStageBuild::Builder &BBoxCropStageBuild::Builder::set_input_width(int size)
{
    m_input_width = size;
    return *this;
}
BBoxCropStageBuild::Builder &BBoxCropStageBuild::Builder::set_input_height(int size)
{
    m_input_height = size;
    return *this;
}
BBoxCropStageBuild::Builder &BBoxCropStageBuild::Builder::set_output_width(int size)
{
    m_output_width = size;
    return *this;
}
BBoxCropStageBuild::Builder &BBoxCropStageBuild::Builder::set_output_height(int size)
{
    m_output_height = size;
    return *this;
}
BBoxCropStageBuild::Builder &BBoxCropStageBuild::Builder::set_main_sub_name(std::string name)
{
    m_main_sub_name = name;
    return *this;
}
BBoxCropStageBuild::Builder &BBoxCropStageBuild::Builder::set_sub_sub_name(std::string name)
{
    m_sub_sub_name = name;
    return *this;
}
BBoxCropStageBuild::Builder &BBoxCropStageBuild::Builder::set_labels(std::vector<std::string> labels)
{
    m_labels = labels;
    return *this;
}
BBoxCropStageBuild::Builder &BBoxCropStageBuild::Builder::set_queue_size(size_t size)
{
    m_queue_size = size;
    return *this;
}
BBoxCropStageBuild::Builder &BBoxCropStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}
BBoxCropStageBuild::Builder &BBoxCropStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}
BBoxCropStageBuild::Builder &BBoxCropStageBuild::Builder::set_pool_mode_opt(StagePoolMode mode)
{
    m_pool_mode = mode;
    return *this;
}
BBoxCropStageBuild::Builder &BBoxCropStageBuild::Builder::set_crop_every_x_frames(size_t crop_every_x_frames)
{
    m_crop_every_x_frames = crop_every_x_frames;
    return *this;
}

BBoxCropStageBuild::Builder &BBoxCropStageBuild::Builder::set_letterbox_opt(dsp_letterbox_alignment_t alignment,
                                                                            dsp_color_t color)
{
    m_use_letterbox = true;
    m_letterbox_alignment = alignment;
    m_letterbox_color = color;
    return *this;
}

std::shared_ptr<BBoxCropStage> BBoxCropStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING((m_output_pool_size > 0), "set_output_pool_size");
    THROW_IF_MISSING((m_input_width > 0), "set_input_width");
    THROW_IF_MISSING((m_input_height > 0), "set_input_height");
    THROW_IF_MISSING((m_output_width > 0), "set_output_width");
    THROW_IF_MISSING((m_output_height > 0), "set_output_height");
    THROW_IF_MISSING(m_main_sub_name.has_value(), "set_main_sub_name");
    THROW_IF_MISSING(m_sub_sub_name.has_value(), "set_sub_sub_name");
    THROW_IF_MISSING(m_labels.has_value(), "set_labels");

    return std::make_shared<BBoxCropStage>(
        m_stage_name.value(), m_output_pool_size, m_input_width, m_input_height, m_output_width, m_output_height,
        m_main_sub_name.value(), m_sub_sub_name.value(), m_labels.value(), m_queue_size, m_leaky, m_trace, m_pool_mode,
        m_crop_every_x_frames, m_use_letterbox, m_letterbox_alignment, m_letterbox_color);
}

BBoxCropStageBuild::Builder BBoxCropStageBuild::create()
{
    return BBoxCropStageBuild::Builder();
}

} // namespace hailo_analytics::pipeline::cropping
