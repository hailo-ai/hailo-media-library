/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "privacy_mask.hpp"
#include "logger_macros.hpp"
#include "media_library_logger.hpp"
#include "media_library_utils.hpp"
#include "analytics_db.hpp"
#include "polygon_math.hpp"
#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#include <tl/expected.hpp>
#include <regex>
#include <fstream>
#include <sys/mman.h>

#define MODULE_NAME LoggerType::PrivacyMask

using namespace privacy_mask_types;

PrivacyMaskBlender::PrivacyMaskBlender()
{
    m_static_privacy_masks.reserve(MAX_NUM_OF_STATIC_PRIVACY_MASKS);

    // Black color for default
    m_color = {0, 0, 0};
    m_privacy_mask_type = PrivacyMaskType::COLOR;
    m_frame_width = 0;
    m_frame_height = 0;

    m_buffer_pool = NULL;
    m_info_update_required = true;
    m_static_mask_update_required = true;
    m_latest_privacy_masks = NULL;
    m_static_mask_enabled = true;
    m_dynamic_mask_enabled = false;
    m_config_parser = std::make_shared<ConfigParser>(ConfigSchema::CONFIG_SCHEMA_PRIVACY_MASK);

    m_dynamic_masks_rois.clear();
}

PrivacyMaskBlender::PrivacyMaskBlender(uint frame_width, uint frame_height)
{
    m_static_privacy_masks.reserve(MAX_NUM_OF_STATIC_PRIVACY_MASKS);

    // Black color for default
    m_color = {0, 0, 0};
    m_privacy_mask_type = PrivacyMaskType::COLOR;
    m_frame_width = frame_width;
    m_frame_height = frame_height;

    set_frame_size(frame_width, frame_height);
    m_latest_privacy_masks = NULL;
    m_static_mask_enabled = true;
    m_dynamic_mask_enabled = false;
    m_config_parser = std::make_shared<ConfigParser>(ConfigSchema::CONFIG_SCHEMA_PRIVACY_MASK);

    m_dynamic_masks_rois.clear();
}

PrivacyMaskBlender::~PrivacyMaskBlender()
{
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    if (m_latest_privacy_masks != NULL)
    {
        m_latest_privacy_masks = NULL;
    }
    m_static_privacy_masks.clear();
    m_dynamic_masks_rois.clear();
    dsp_status status = dsp_utils::release_device();
    if (status != DSP_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to release DSP device, status: {}", status);
    }
}

tl::expected<PrivacyMaskBlenderPtr, media_library_return> PrivacyMaskBlender::create()
{
    PrivacyMaskBlenderPtr privacy_mask_blender_ptr = std::make_shared<PrivacyMaskBlender>();

    dsp_status dsp_ret = dsp_utils::acquire_device();
    if (dsp_ret != DSP_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to acquire DSP device, status: {}", dsp_ret);
        return tl::make_unexpected(MEDIA_LIBRARY_OUT_OF_RESOURCES);
    }

    return privacy_mask_blender_ptr;
}

tl::expected<PrivacyMaskBlenderPtr, media_library_return> PrivacyMaskBlender::create(const std::string &config)
{
    PrivacyMaskBlenderPtr privacy_mask_blender_ptr = std::make_shared<PrivacyMaskBlender>();

    dsp_status dsp_ret = dsp_utils::acquire_device();
    if (dsp_ret != DSP_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to acquire DSP device, status: {}", dsp_ret);
        return tl::make_unexpected(MEDIA_LIBRARY_OUT_OF_RESOURCES);
    }

    if (privacy_mask_blender_ptr->configure(config) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure PrivacyMaskBlender");
        return tl::make_unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
    }

    return privacy_mask_blender_ptr;
}

tl::expected<PrivacyMaskBlenderPtr, media_library_return> PrivacyMaskBlender::create(uint frame_width,
                                                                                     uint frame_height)

{
    PrivacyMaskBlenderPtr privacy_mask_blender_ptr = std::make_shared<PrivacyMaskBlender>(frame_width, frame_height);

    dsp_status dsp_ret = dsp_utils::acquire_device();
    if (dsp_ret != DSP_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to acquire DSP device, status: {}", dsp_ret);
        return tl::make_unexpected(MEDIA_LIBRARY_OUT_OF_RESOURCES);
    }

    return privacy_mask_blender_ptr;
}

media_library_return PrivacyMaskBlender::init_buffer_pool()
{
    // Round up m_frame_width to be a multiple of byte_size / PRIVACY_MASK_QUANTIZATION (32)
    int line_division = 8 / PRIVACY_MASK_QUANTIZATION;
    uint frame_width = ((m_frame_width + (line_division - 1)) & ~(line_division - 1)) / line_division;
    // Round bytes_per_line to be a multiple of byte_size (8)
    uint bytes_per_line = (frame_width + 7) & ~7;
    uint frame_height = m_frame_height / 4;
    std::string name = "privacy_mask";
    // TODO: set pool size
    m_buffer_pool = std::make_shared<MediaLibraryBufferPool>(frame_width, frame_height, HAILO_FORMAT_GRAY8, 1,
                                                             HAILO_MEMORY_TYPE_DMABUF, bytes_per_line, name);
    if (m_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to initialize buffer pool");
        return media_library_return::MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    LOGGER__MODULE__INFO(MODULE_NAME,
                         "Buffer pool initialized successfully with frame size {}x{} "
                         "bytes_per_line {}",
                         frame_width, frame_height, bytes_per_line);
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskBlender::add_static_privacy_mask(const polygon &privacy_mask)
{
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    if (m_static_privacy_masks.size() >= MAX_NUM_OF_STATIC_PRIVACY_MASKS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Max number of privacy masks reached {}", MAX_NUM_OF_STATIC_PRIVACY_MASKS);
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    if (privacy_mask.vertices.size() > MAX_NUM_OF_VERTICES_IN_POLYGON)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Polygon cannot have more than {} vertices", MAX_NUM_OF_VERTICES_IN_POLYGON);
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    PolygonPtr polygon_ptr = std::make_shared<polygon>(privacy_mask);

    // double rotation_angle = m_rotation;
    // rotate_polygon(polygon_ptr, rotation_angle, m_frame_width, m_frame_height);
    m_static_privacy_masks.emplace_back(polygon_ptr);

    m_static_mask_update_required = true;

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskBlender::set_static_privacy_mask(const polygon &privacy_mask)
{
    if (privacy_mask.vertices.size() > MAX_NUM_OF_VERTICES_IN_POLYGON)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Polygon cannot have more than {} vertices", MAX_NUM_OF_VERTICES_IN_POLYGON);
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    // find the specific privacy mask
    auto it = std::find_if(
        m_static_privacy_masks.begin(), m_static_privacy_masks.end(),
        [&privacy_mask](const PolygonPtr &privacy_mask_ptr) { return privacy_mask_ptr->id == privacy_mask.id; });
    if (it == m_static_privacy_masks.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Privacy mask with id {} not found", privacy_mask.id);
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    PolygonPtr privacy_mask_to_update = *it;
    // Update polygon
    privacy_mask_to_update->vertices = privacy_mask.vertices;

    m_static_mask_update_required = true;

    // double rotation_angle = m_rotation;
    // rotate_polygon(privacy_mask_to_update, rotation_angle, m_frame_width, m_frame_height);
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskBlender::remove_static_privacy_mask(const std::string &id)
{
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    auto it = std::find_if(m_static_privacy_masks.begin(), m_static_privacy_masks.end(),
                           [&id](const PolygonPtr &privacy_mask) { return privacy_mask->id == id; });
    if (it == m_static_privacy_masks.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Privacy mask with id {} not found", id);
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    m_static_privacy_masks.erase(it);

    m_static_mask_update_required = true;

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskBlender::set_color(const rgb_color_t &color)
{
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    m_privacy_mask_type = PrivacyMaskType::COLOR;
    m_color = color;
    m_pixelization_size = std::nullopt;
    m_info_update_required = true;
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskBlender::set_pixelization_size(PixelizationSize size)
{
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    if (size < 2 || size > 64)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Pixelization size must be a number between 2 and 64");
        return media_library_return::MEDIA_LIBRARY_INVALID_ARGUMENT;
    }
    m_privacy_mask_type = PrivacyMaskType::PIXELIZATION;
    m_pixelization_size = size;
    m_color = std::nullopt;
    m_info_update_required = true;
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskBlender::set_rotation(const rotation_angle_t &rotation)
{
    if (m_rotation == rotation)
    {
        LOGGER__MODULE__WARNING(MODULE_NAME, "Rotation is already set to {}, skipping update", rotation);
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);

    // Rotate polygons
    // double rotation_angle = rotation - m_rotation;
    // LOGGER__MODULE__INFO(MODULE_NAME, "Rotating polygons by {}
    // degrees", rotation_angle); if (rotate_polygons(m_static_privacy_masks, rotation_angle, m_frame_width,
    // m_frame_height) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    // {
    //   LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to rotate polygons");
    //   return media_library_return::MEDIA_LIBRARY_ERROR;
    // }

    // Swap frame width and height if needed
    if ((m_rotation == ROTATION_ANGLE_0 || m_rotation == ROTATION_ANGLE_180) &&
        (rotation == ROTATION_ANGLE_90 || rotation == ROTATION_ANGLE_270))
    {
        std::swap(m_frame_width, m_frame_height);
    }
    else if ((m_rotation == ROTATION_ANGLE_90 || m_rotation == ROTATION_ANGLE_270) &&
             (rotation == ROTATION_ANGLE_0 || rotation == ROTATION_ANGLE_180))
    {
        std::swap(m_frame_width, m_frame_height);
    }

    m_static_mask_update_required = true;

    // Initialize buffer pool with new dimensions
    if (init_buffer_pool() != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to initialize buffer pool");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    m_rotation = rotation;
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

tl::expected<rgb_color_t, media_library_return> PrivacyMaskBlender::get_color()
{
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    if (m_privacy_mask_type != PrivacyMaskType::COLOR)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Privacy mask type is not set to COLOR");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    if (!m_color.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Incosistent state, color is not set in COLOR mode");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    return m_color.value();
}

tl::expected<PixelizationSize, media_library_return> PrivacyMaskBlender::get_pixelization_size()
{
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    if (m_privacy_mask_type != PrivacyMaskType::PIXELIZATION)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Privacy mask type is not set to PIXELIZATION");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    if (!m_pixelization_size.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Incosistent state, pixelization size is not set in PIXELIZATION mode");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    return m_pixelization_size.value();
}

tl::expected<polygon, media_library_return> PrivacyMaskBlender::get_static_privacy_mask(const std::string &id)
{
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    auto it = std::find_if(m_static_privacy_masks.begin(), m_static_privacy_masks.end(),
                           [&id](const PolygonPtr &privacy_mask) { return privacy_mask->id == id; });
    if (it == m_static_privacy_masks.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Privacy mask with id {} not found", id);
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    return **it;
}

tl::expected<std::pair<uint, uint>, media_library_return> PrivacyMaskBlender::get_frame_size()
{
    if (m_frame_width == 0 || m_frame_height == 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Frame size is not set yet");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    return std::make_pair(m_frame_width, m_frame_height);
}

media_library_return PrivacyMaskBlender::set_frame_size(const uint &width, const uint &height)
{
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    m_frame_width = width;
    m_frame_height = height;

    m_static_mask_update_required = true;

    // Initialize buffer pool
    if (init_buffer_pool() != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to initialize buffer pool at new frame size");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskBlender::clear_all_static_privacy_masks()
{
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    m_static_privacy_masks.clear();
    m_static_mask_update_required = true;
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

tl::expected<std::vector<polygon>, media_library_return> PrivacyMaskBlender::get_all_static_privacy_masks()
{
    std::vector<polygon> privacy_masks;
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    for (auto &privacy_mask : m_static_privacy_masks)
    {
        privacy_masks.emplace_back(*privacy_mask);
    }
    return privacy_masks;
}

static privacy_mask_types::yuv_color_t rgb_to_yuv(const rgb_color_t &rgb_color)
{
    privacy_mask_types::yuv_color_t yuv_color;
    yuv_color.y = 0.257 * rgb_color.r + 0.504 * rgb_color.g + 0.098 * rgb_color.b + 16;
    yuv_color.u = -0.148 * rgb_color.r - 0.291 * rgb_color.g + 0.439 * rgb_color.b + 128;
    yuv_color.v = 0.439 * rgb_color.r - 0.368 * rgb_color.g - 0.071 * rgb_color.b + 128;
    return yuv_color;
}

static dsp_letterbox_alignment_t scaling_mode_to_dsp_letterbox(ScalingMode scaling_mode)
{
    switch (scaling_mode)
    {
    case ScalingMode::STRETCH:
        return DSP_NO_LETTERBOX;
    case ScalingMode::LETTERBOX_MIDDLE:
        return DSP_LETTERBOX_MIDDLE;
    case ScalingMode::LETTERBOX_UP_LEFT:
        return DSP_LETTERBOX_UP_LEFT;
    default:
        return DSP_NO_LETTERBOX;
    }
}

media_library_return PrivacyMaskBlender::update_info()
{
    if (!m_info_update_required)
    {
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    switch (m_privacy_mask_type)
    {
    case PrivacyMaskType::COLOR:
        if (!m_color.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Color is not set in COLOR mode");
            return media_library_return::MEDIA_LIBRARY_ERROR;
        }
        m_latest_privacy_masks->info.color = rgb_to_yuv(m_color.value());
        m_latest_privacy_masks->info.type = PrivacyMaskType::COLOR;
        break;
    case PrivacyMaskType::PIXELIZATION:
        if (!m_pixelization_size.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Pixelization size is not set in PIXELIZATION mode");
            return media_library_return::MEDIA_LIBRARY_ERROR;
        }
        m_latest_privacy_masks->info.pixelization_size = m_pixelization_size.value();
        m_latest_privacy_masks->info.type = PrivacyMaskType::PIXELIZATION;
        break;
    default:
        LOGGER__MODULE__ERROR(MODULE_NAME, "Invalid privacy mask type");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    m_info_update_required = false;
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskBlender::update_static_mask()
{
    if (!m_static_mask_update_required && m_latest_privacy_masks->static_data != NULL)
    {
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    m_latest_privacy_masks->static_data = std::make_shared<static_privacy_mask_data_t>();

    if (!m_static_mask_enabled || m_static_privacy_masks.empty())
    {
        m_latest_privacy_masks->static_data->rois_count = 0;
        m_static_mask_update_required = false;
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    if (m_buffer_pool == NULL)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Buffer pool is uninitialized");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    if (m_buffer_pool->acquire_buffer(m_latest_privacy_masks->static_data->bitmask) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to acquire buffer");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    m_latest_privacy_masks->static_data->bitmask->sync_start();
    if (write_polygons_to_privacy_mask_data(m_static_privacy_masks, m_frame_width, m_frame_height,
                                            m_latest_privacy_masks->static_data) !=
        media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to write polygon");
        m_latest_privacy_masks->static_data->bitmask->sync_end();
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    m_latest_privacy_masks->static_data->bitmask->sync_end();
    m_static_mask_update_required = false;
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskBlender::update_dynamic_mask(uint64_t isp_timestamp_ns)
{
    if (!m_dynamic_mask_enabled || m_latest_privacy_masks->dynamic_data == NULL)
    {
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }
    LOGGER__MODULE__TRACE(MODULE_NAME, "Updating dynamic mask");
    m_dynamic_masks_rois.clear();

    m_latest_privacy_masks->dynamic_data->dynamic_mask_group.masks = NULL;
    m_latest_privacy_masks->dynamic_data->dynamic_mask_group.masks_count = 0;
    m_dynamic_masks_rois.clear();
    auto &db = AnalyticsDB::instance();
    std::chrono::nanoseconds isp_timestamp(isp_timestamp_ns);
    std::chrono::time_point<std::chrono::steady_clock> isp_timestamp_tp(isp_timestamp);

    AnalyticsQueryOptions opts{.m_type = AnalyticsQueryType::WithinDelta,
                               .m_ts = isp_timestamp_tp,
                               .m_delta = std::chrono::milliseconds(40),
                               .m_timeout = std::chrono::milliseconds(10000)};
    auto closet_instance_segmentation_entry_expected = db.query_instance_segmentation_entry(m_analytics_data_id, opts);
    if (!closet_instance_segmentation_entry_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get closest instance segmentation entry from DB");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    auto closet_instance_segmentation_entry = closet_instance_segmentation_entry_expected.value();

    auto analytics_config = db.get_application_analytics_config();
    for (const auto &segmentation_data : closet_instance_segmentation_entry.analytics_buffer)
    {
        if (m_dynamic_masks_rois.size() >= MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS)
        {
            LOGGER__MODULE__WARNING(MODULE_NAME,
                                    "Reached MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS ({}), skipping remaining ROIs.",
                                    MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS);
            break;
        }

        // Verify that the dynamic analytics data ID exists in the config
        if (analytics_config.instance_segmentation_analytics_config.find(m_analytics_data_id) ==
            analytics_config.instance_segmentation_analytics_config.end())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Analytics config for ID {} not found", m_analytics_data_id);
            return media_library_return::MEDIA_LIBRARY_ERROR;
        }

        if (std::find_if(analytics_config.instance_segmentation_analytics_config[m_analytics_data_id].labels.begin(),
                         analytics_config.instance_segmentation_analytics_config[m_analytics_data_id].labels.end(),
                         [&](const label_t &label) { return label.id == segmentation_data.class_id; }) ==
            analytics_config.instance_segmentation_analytics_config[m_analytics_data_id].labels.end())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Skipping segmentation data for unknown class_id {}",
                                  segmentation_data.class_id);
            continue;
        }

        // Find the label name for this class_id
        auto &labels = analytics_config.instance_segmentation_analytics_config[m_analytics_data_id].labels;
        auto label_it = std::find_if(labels.begin(), labels.end(),
                                     [&](const label_t &label) { return label.id == segmentation_data.class_id; });

        if (label_it == labels.end())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Skipping segmentation data for unknown class_id {}",
                                  segmentation_data.class_id);
            continue;
        }

        // Check if the label name is in the masked labels
        if (std::find(m_masked_labels.begin(), m_masked_labels.end(), label_it->label) == m_masked_labels.end())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME,
                                  "Skipping segmentation data for label '{}' (class_id {}) not in masked labels",
                                  label_it->label, segmentation_data.class_id);
            continue;
        }

        // Execute the dynamic mask
        auto input_frame_net_width = analytics_config.instance_segmentation_analytics_config[m_analytics_data_id].width;
        auto input_frame_net_height =
            analytics_config.instance_segmentation_analytics_config[m_analytics_data_id].height;
        auto scaling_mode = analytics_config.instance_segmentation_analytics_config[m_analytics_data_id].scaling_mode;
        LOGGER__MODULE__TRACE(
            MODULE_NAME,
            "Processing segmentation data for class_id {}, box: ({}, {}), ({}, {}), "
            "input_frame_net_width: {}, input_frame_net_height: {}, scaling_mode: {}, mask_size: {}, ",
            segmentation_data.class_id, segmentation_data.box.x_min, segmentation_data.box.y_min,
            segmentation_data.box.x_max, segmentation_data.box.y_max, input_frame_net_width, input_frame_net_height,
            static_cast<int>(scaling_mode), segmentation_data.mask_size);

        m_dynamic_masks_rois.push_back(dsp_dynamic_privacy_mask_roi_t{
            .bytemask = segmentation_data.mask,
            .input_frame_net_width = input_frame_net_width,
            .input_frame_net_height = input_frame_net_height,
            .letterbox = scaling_mode_to_dsp_letterbox(scaling_mode),
            .roi =
                {
                    .start_x = static_cast<size_t>(segmentation_data.box.x_min),
                    .start_y = static_cast<size_t>(segmentation_data.box.y_min),
                    .end_x = static_cast<size_t>(segmentation_data.box.x_max),
                    .end_y = static_cast<size_t>(segmentation_data.box.y_max),
                },
            .dilation_size = m_dilation_size,
        });
    }

    m_latest_privacy_masks->dynamic_data->dynamic_mask_group.masks = m_dynamic_masks_rois.data();
    m_latest_privacy_masks->dynamic_data->dynamic_mask_group.masks_count = m_dynamic_masks_rois.size();

    auto original_width_ratio =
        analytics_config.instance_segmentation_analytics_config[m_analytics_data_id].original_width_ratio;
    auto original_height_ratio =
        analytics_config.instance_segmentation_analytics_config[m_analytics_data_id].original_height_ratio;
    m_latest_privacy_masks->dynamic_data->dynamic_mask_group.original_aspect_ratio =
        static_cast<float>(original_width_ratio) / original_height_ratio;

    // TODO : add support for aspect ratio preservation
    m_latest_privacy_masks->dynamic_data->dynamic_mask_group.scaling_mode = DSP_SCALING_MODE_STRETCH;

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

tl::expected<PrivacyMasksPtr, media_library_return> PrivacyMaskBlender::get_updated_privacy_masks(
    uint64_t isp_timestamp_ns)
{
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);

    if (m_latest_privacy_masks == NULL)
    {
        m_latest_privacy_masks = std::make_shared<privacy_masks_t>();
    }

    if (update_info() != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }

    if (update_static_mask() != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }

    if (update_dynamic_mask(isp_timestamp_ns) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }

    return m_latest_privacy_masks;
}

media_library_return PrivacyMaskBlender::blend(HailoMediaLibraryBufferPtr &input_buffer)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Blending privacy mask");
    struct timespec start_blend, end_blend;
    clock_gettime(CLOCK_MONOTONIC, &start_blend);

    auto updated_masks_expected = get_updated_privacy_masks(input_buffer->isp_timestamp_ns);
    if (!updated_masks_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to blend privacy mask");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    PrivacyMasksPtr privacy_mask_data = updated_masks_expected.value();

    // Prepare the static privacy mask parameters
    std::optional<dsp_static_privacy_mask_t> static_privacy_mask = std::nullopt;
    std::vector<dsp_roi_t> dsp_rois;
    if (privacy_mask_data->static_data->rois_count != 0)
    {
        static_privacy_mask.emplace();
        dsp_rois.resize(privacy_mask_data->static_data->rois_count);

        static_privacy_mask->bitmask = (uint8_t *)privacy_mask_data->static_data->bitmask->get_plane_ptr(0);
        if (privacy_mask_data->info.type == PrivacyMaskType::COLOR)
        {
            static_privacy_mask->color.y = privacy_mask_data->info.color.y;
            static_privacy_mask->color.u = privacy_mask_data->info.color.u;
            static_privacy_mask->color.v = privacy_mask_data->info.color.v;
            static_privacy_mask->type = DSP_PRIVACY_MASK_COLOR;
        }
        else
        {
            static_privacy_mask->type = DSP_PRIVACY_MASK_BLUR;
            static_privacy_mask->blur_radius = privacy_mask_data->info.pixelization_size;
        }
        static_privacy_mask->rois = dsp_rois.data();
        static_privacy_mask->rois_count = privacy_mask_data->static_data->rois_count;

        for (uint i = 0; i < privacy_mask_data->static_data->rois_count; i++)
        {
            dsp_rois[i] = {
                .start_x = privacy_mask_data->static_data->rois[i].x,
                .start_y = privacy_mask_data->static_data->rois[i].y,
                .end_x = privacy_mask_data->static_data->rois[i].x + privacy_mask_data->static_data->rois[i].width,
                .end_y = privacy_mask_data->static_data->rois[i].y + privacy_mask_data->static_data->rois[i].height};
        }
    }

    // Assemble the unified privacy mask parameters
    unified_dsp_privacy_mask_t privacy_mask_params;

    if (privacy_mask_data->info.type == PrivacyMaskType::COLOR)
    {
        privacy_mask_params.color.y = privacy_mask_data->info.color.y;
        privacy_mask_params.color.u = privacy_mask_data->info.color.u;
        privacy_mask_params.color.v = privacy_mask_data->info.color.v;
        privacy_mask_params.type = DSP_PRIVACY_MASK_COLOR;
    }
    else
    {
        privacy_mask_params.type = DSP_PRIVACY_MASK_BLUR;
        privacy_mask_params.pixelization_size = privacy_mask_data->info.pixelization_size;
    }
    privacy_mask_params.static_privacy_mask_params = static_privacy_mask ? &static_privacy_mask.value() : nullptr;
    privacy_mask_params.dynamic_privacy_mask_params =
        (privacy_mask_data->dynamic_data && privacy_mask_data->dynamic_data->dynamic_mask_group.masks_count > 0)
            ? &privacy_mask_data->dynamic_data->dynamic_mask_group
            : nullptr;
    LOGGER__MODULE__TRACE(
        MODULE_NAME, "Blending {} static masks and {} dynamic masks", privacy_mask_data->static_data->rois_count,
        privacy_mask_data->dynamic_data ? privacy_mask_data->dynamic_data->dynamic_mask_group.masks_count : 0);
    dsp_status status = dsp_utils::perform_dsp_privacy_mask(input_buffer->buffer_data.get(), &privacy_mask_params);
    if (status != DSP_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "DSP privacy mask blend failed with {}", status);
        return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_blend);
    [[maybe_unused]] long ms = (long)media_library_difftimespec_ms(end_blend, start_blend);
    LOGGER__MODULE__TRACE(MODULE_NAME, "Blending privacy masks took {} milliseconds ({} fps)", ms, (1000 / ms));

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

void PrivacyMaskBlender::set_static_mask_enabled(bool enable)
{
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    if (m_static_mask_enabled != enable)
    {
        m_static_mask_enabled = enable;
        m_static_mask_update_required = true;
    }
}

bool PrivacyMaskBlender::is_static_mask_enabled()
{
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    return m_static_mask_enabled;
}

void PrivacyMaskBlender::set_dynamic_mask_enabled(bool enable)
{
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    if (m_dynamic_mask_enabled != enable)
    {
        m_dynamic_mask_enabled = enable;
    }
}

bool PrivacyMaskBlender::is_dynamic_mask_enabled()
{
    std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
    return m_dynamic_mask_enabled;
}

media_library_return PrivacyMaskBlender::configure(const std::string &config)
{
    // check if the config has ' at the beginning and end of the string. if so, remove them
    std::string clean_config = config; // config is const, so we need to copy it
    if (clean_config[0] == '\'' && clean_config[clean_config.size() - 1] == '\'')
    {
        clean_config = clean_config.substr(1, config.size() - 2);
    }

    if (m_config_parser->validate_configuration(clean_config) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to validate configuration");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    auto config_json = nlohmann::json::parse(clean_config)["privacy_mask"];
    std::string privacy_mask_config_string = config_json.dump();

    auto privacy_mask_config = std::make_unique<privacy_mask_config_t>();
    if (m_config_parser->config_string_to_struct<privacy_mask_config_t>(privacy_mask_config_string,
                                                                        *privacy_mask_config) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to convert config string to struct");
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    return configure(privacy_mask_config);
}

media_library_return PrivacyMaskBlender::configure(const std::unique_ptr<privacy_mask_config_t> &config)
{
    // Update info config
    switch (config->mask_type)
    {
    case PrivacyMaskType::COLOR:
        set_color(config->color_value);
        break;
    case PrivacyMaskType::PIXELIZATION:
        set_pixelization_size(config->pixelization_size);
        break;
    }

    // Update dynamic mask config
    if (config->dynamic_privacy_mask_config.has_value())
    {
        std::unique_lock<std::mutex> lock(m_privacy_mask_mutex);
        m_dynamic_mask_enabled = config->dynamic_privacy_mask_config->enabled;
        m_analytics_data_id = config->dynamic_privacy_mask_config->analytics_data_id;
        m_masked_labels = config->dynamic_privacy_mask_config->masked_labels;
        m_dilation_size = config->dynamic_privacy_mask_config->dilation_size;
    }

    // Update static mask config
    if (config->static_privacy_mask_config.has_value())
    {
        const auto &static_config = config->static_privacy_mask_config.value();
        set_static_mask_enabled(
            static_config.enabled); // TODO make tappas use this enable disable in globale enable disable

        if (static_config.enabled)
        {
            if (clear_all_static_privacy_masks() != MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to clear all static privacy masks");
                return MEDIA_LIBRARY_ERROR;
            }

            for (const auto &mask : static_config.masks)
            {
                if (add_static_privacy_mask(mask) != MEDIA_LIBRARY_SUCCESS)
                {
                    LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to add static privacy mask {}", mask.id);
                    return MEDIA_LIBRARY_ERROR;
                }
            }
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}
