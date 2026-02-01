#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/cropping/tiling_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::cropping
{

TilingCropStage::TilingCropStage(std::string name, int output_pool_size, int input_width, int input_height,
                                 int output_width, int output_height, std::string main_sub_name,
                                 std::string sub_sub_name, const std::vector<HailoBBox> &bbox_tiles, size_t queue_size,
                                 bool leaky, bool trace_processing_operations, StagePoolMode pool_mode,
                                 size_t crop_every_x_frames)
    : DspBaseCropStage(name, output_pool_size, input_width, input_height, output_width, output_height, main_sub_name,
                       sub_sub_name, queue_size, leaky, trace_processing_operations, pool_mode, crop_every_x_frames),
      m_bbox_tiles(bbox_tiles)
{
}

AppStatus TilingCropStage::init()
{
    auto bytes_per_line = dsp_utils::get_dsp_desired_stride_from_width(m_output_width);
    m_buffer_pool =
        std::make_shared<MediaLibraryBufferPool>(m_output_width, m_output_height, HAILO_FORMAT_NV12, m_output_pool_size,
                                                 HAILO_MEMORY_TYPE_DMABUF, bytes_per_line, "tiling_buffer_pool");
    if (m_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        return AppStatus::DSP_OPERATION_ERROR;
    }

    /* Create the HailoTileROI objects and the buffer pools we will have pool per tile */
    for (std::size_t i = 0; i < m_bbox_tiles.size(); ++i)
    {
        const auto &tile_bbox = m_bbox_tiles[i];
        HailoTileROIPtr tile = std::make_shared<HailoTileROI>(tile_bbox, 0, 0, 0, 0, SINGLE_SCALE);
        m_fhd_tiles.push_back(tile);
    }

    return AppStatus::SUCCESS;
}

void TilingCropStage::prepare_crops(BufferPtr input_buffer, std::vector<dsp_crop_api_t> &crop_resize_dims)
{
    (void)input_buffer;
    for (auto &tile : m_fhd_tiles)
    {
        HailoBBox bbox = tile->get_bbox();
        prepare_single_crop_dim(bbox, crop_resize_dims);
    }
}

HailoBBox TilingCropStage::get_crop_bbox(int index)
{
    try
    {
        return m_fhd_tiles.at(index)->get_bbox();
    }
    catch (const std::out_of_range &e)
    {
        HAILO_ANALYTICS_LOG_ERROR("Tiling index {} is out of bounds: ", index, e.what());
        throw;
    }
}

void TilingCropStage::post_crop(BufferPtr input_buffer)
{
    (void)input_buffer;
    // No post-crop processing needed for tiling
}

void TilingCropStage::pre_crop(BufferPtr input_buffer)
{
    (void)input_buffer;
    // No pre-crop processing needed for tiling
}

HailoROIPtr TilingCropStage::get_crop_roi(int index)
{
    (void)index;
    return nullptr;
}

TilingCropStageBuild::Builder &TilingCropStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}
TilingCropStageBuild::Builder &TilingCropStageBuild::Builder::set_output_pool_size(int size)
{
    m_output_pool_size = size;
    return *this;
}
TilingCropStageBuild::Builder &TilingCropStageBuild::Builder::set_input_width(int size)
{
    m_input_width = size;
    return *this;
}
TilingCropStageBuild::Builder &TilingCropStageBuild::Builder::set_input_height(int size)
{
    m_input_height = size;
    return *this;
}
TilingCropStageBuild::Builder &TilingCropStageBuild::Builder::set_output_width(int size)
{
    m_output_width = size;
    return *this;
}
TilingCropStageBuild::Builder &TilingCropStageBuild::Builder::set_output_height(int size)
{
    m_output_height = size;
    return *this;
}
TilingCropStageBuild::Builder &TilingCropStageBuild::Builder::set_main_sub_name(std::string name)
{
    m_main_sub_name = name;
    return *this;
}
TilingCropStageBuild::Builder &TilingCropStageBuild::Builder::set_sub_sub_name(std::string name)
{
    m_sub_sub_name = name;
    return *this;
}
TilingCropStageBuild::Builder &TilingCropStageBuild::Builder::set_bbox_tiles(const std::vector<HailoBBox> &bbox_tiles)
{
    m_bbox_tiles = bbox_tiles;
    return *this;
}
TilingCropStageBuild::Builder &TilingCropStageBuild::Builder::set_queue_size(size_t size)
{
    m_queue_size = size;
    return *this;
}
TilingCropStageBuild::Builder &TilingCropStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}
TilingCropStageBuild::Builder &TilingCropStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}
TilingCropStageBuild::Builder &TilingCropStageBuild::Builder::set_pool_mode_opt(StagePoolMode mode)
{
    m_pool_mode = mode;
    return *this;
}
TilingCropStageBuild::Builder &TilingCropStageBuild::Builder::set_crop_every_x_frames(size_t crop_every_x_frames)
{
    m_crop_every_x_frames = crop_every_x_frames;
    return *this;
}

std::shared_ptr<TilingCropStage> TilingCropStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING((m_output_pool_size > 0), "set_output_pool_size");
    THROW_IF_MISSING((m_input_width > 0), "set_input_width");
    THROW_IF_MISSING((m_input_height > 0), "set_input_height");
    THROW_IF_MISSING((m_output_width > 0), "set_output_width");
    THROW_IF_MISSING((m_output_height > 0), "set_output_height");
    THROW_IF_MISSING(m_main_sub_name.has_value(), "set_main_sub_name");
    THROW_IF_MISSING(m_sub_sub_name.has_value(), "set_sub_sub_name");
    THROW_IF_MISSING(!m_bbox_tiles.empty(), "set_bbox_tiles");

    return std::make_shared<TilingCropStage>(m_stage_name.value(), m_output_pool_size, m_input_width, m_input_height,
                                             m_output_width, m_output_height, m_main_sub_name.value(),
                                             m_sub_sub_name.value(), m_bbox_tiles, m_queue_size, m_leaky, m_trace,
                                             m_pool_mode, m_crop_every_x_frames);
}

TilingCropStageBuild::Builder TilingCropStageBuild::create()
{
    return TilingCropStageBuild::Builder();
}

} // namespace hailo_analytics::pipeline::cropping
