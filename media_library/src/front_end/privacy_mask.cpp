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
#include "media_library_logger.hpp"
#include "polygon_math.hpp"
#include <tl/expected.hpp>

using namespace privacy_mask_types;

PrivacyMaskBlender::PrivacyMaskBlender()
{
  m_privacy_masks.reserve(MAX_NUM_OF_PRIVACY_MASKS);

  // Black color for default
  m_color = {0, 0, 0};
  m_frame_width = 0;
  m_frame_height = 0;
  m_privacy_mask_mutex = std::make_shared<std::mutex>();

  m_buffer_pool = NULL;
  m_update_required = true;
  m_latest_privacy_mask_data = NULL;
}

PrivacyMaskBlender::PrivacyMaskBlender(uint frame_width, uint frame_height)
{

  m_privacy_masks.reserve(MAX_NUM_OF_PRIVACY_MASKS);

  // Black color for default
  m_color = {0, 0, 0};
  m_frame_width = frame_width;
  m_frame_height = frame_height;
  m_privacy_mask_mutex = std::make_shared<std::mutex>();

  set_frame_size(frame_width, frame_height);
  m_latest_privacy_mask_data = NULL;
}

PrivacyMaskBlender::~PrivacyMaskBlender()
{
    m_privacy_masks.clear();
    dsp_status status = dsp_utils::release_device();
    if (status != DSP_SUCCESS)
    {
        LOGGER__ERROR("Failed to release DSP device, status: {}", status);
    }
}

tl::expected<PrivacyMaskBlenderPtr, media_library_return> PrivacyMaskBlender::create()
{
  return create(nlohmann::json());
}

tl::expected<PrivacyMaskBlenderPtr, media_library_return> PrivacyMaskBlender::create(uint frame_width, uint frame_height)
{
  return create(frame_width, frame_height, nlohmann::json());
}

tl::expected<PrivacyMaskBlenderPtr, media_library_return> PrivacyMaskBlender::create(const nlohmann::json &config)
{
  PrivacyMaskBlenderPtr privacy_mask_blender_ptr = std::make_shared<PrivacyMaskBlender>();

  dsp_status dsp_ret = dsp_utils::acquire_device();
  if (dsp_ret != DSP_SUCCESS)
  {
    LOGGER__ERROR("Failed to acquire DSP device, status: {}", dsp_ret);
    return tl::make_unexpected(MEDIA_LIBRARY_OUT_OF_RESOURCES);
  }

  return privacy_mask_blender_ptr;
}

tl::expected<PrivacyMaskBlenderPtr, media_library_return> PrivacyMaskBlender::create(uint frame_width, uint frame_height, const nlohmann::json &config)
{
  PrivacyMaskBlenderPtr privacy_mask_blender_ptr = std::make_shared<PrivacyMaskBlender>(frame_width, frame_height);

  dsp_status dsp_ret = dsp_utils::acquire_device();
  if (dsp_ret != DSP_SUCCESS)
  {
    LOGGER__ERROR("Failed to acquire DSP device, status: {}", dsp_ret);
    return tl::make_unexpected(MEDIA_LIBRARY_OUT_OF_RESOURCES);
  }

  return privacy_mask_blender_ptr;
}

media_library_return PrivacyMaskBlender::init_buffer_pool()
{
    // Round up m_frame_width to be a multiple of byte_size / PRIVACY_MASK_QUANTIZATION (32)
    int line_division = 8 / PRIVACY_MASK_QUANTIZATION;
    uint frame_width = ((m_frame_width + (line_division-1)) & ~(line_division-1))/line_division;
    // Round bytes_per_line to be a multiple of byte_size (8)
    uint bytes_per_line = (frame_width + 7) & ~7;
    uint frame_height = m_frame_height/4;
    // TODO: set pool size
    m_buffer_pool = std::make_shared<MediaLibraryBufferPool>(frame_width, frame_height, DSP_IMAGE_FORMAT_GRAY8, 1, CMA, bytes_per_line);
    if (m_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
      LOGGER__ERROR("PrivacyMaskBlender::PrivacyMaskBlender: Failed to initialize buffer pool");
      return media_library_return::MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    LOGGER__INFO("PrivacyMaskBlender::PrivacyMaskBlender: Buffer pool initialized successfully with frame size {}x{} bytes_per_line {}", frame_width, frame_height, bytes_per_line);
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

void PrivacyMaskBlender::clean_latest_privacy_mask_data()
{
  if (m_update_required && m_latest_privacy_mask_data != NULL)
  {
    m_latest_privacy_mask_data->bitmask.decrease_ref_count();
    m_latest_privacy_mask_data = NULL;
  }
}

media_library_return PrivacyMaskBlender::add_privacy_mask(const polygon &privacy_mask)
{
  std::unique_lock<std::mutex> lock(*m_privacy_mask_mutex);
  if (m_privacy_masks.size() >= MAX_NUM_OF_PRIVACY_MASKS)
  {
    LOGGER__ERROR("PrivacyMaskBlender::add_privacy_mask: Max number of privacy masks reached {}", MAX_NUM_OF_PRIVACY_MASKS);
    return media_library_return::MEDIA_LIBRARY_ERROR;
  }

  if (privacy_mask.vertices.size() > 8)
  {
    LOGGER__ERROR("PrivacyMaskBlender::add_privacy_mask: Polygon cannot have more than 8 vertices");
    return media_library_return::MEDIA_LIBRARY_ERROR;
  }

  PolygonPtr polygon = std::make_shared<privacy_mask_types::polygon>(privacy_mask);

  // double rotation_angle = m_rotation;
  // rotate_polygon(polygon, rotation_angle, m_frame_width, m_frame_height);
  m_privacy_masks.emplace_back(polygon);

  m_update_required = true;

  return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskBlender::set_privacy_mask(const polygon &privacy_mask)
{
  if (privacy_mask.vertices.size() > 8)
  {
    LOGGER__ERROR("PrivacyMaskBlender::add_privacy_mask: Polygon cannot have more than 8 vertices");
    return media_library_return::MEDIA_LIBRARY_ERROR;
  }

  std::unique_lock<std::mutex> lock(*m_privacy_mask_mutex);
  //find the specific privacy mask
  auto it = std::find_if(m_privacy_masks.begin(), m_privacy_masks.end(), [&privacy_mask](const PolygonPtr &privacy_mask_ptr)
                         { return privacy_mask_ptr->id == privacy_mask.id; });
  if (it == m_privacy_masks.end())
  {
    LOGGER__ERROR("PrivacyMaskBlender::set_privacy_mask: Privacy mask with id {} not found", privacy_mask.id);
    return media_library_return::MEDIA_LIBRARY_ERROR;
  }

  PolygonPtr privacy_mask_to_update = *it;
  // Update polygon
  privacy_mask_to_update->vertices = privacy_mask.vertices;

  m_update_required = true;

  // double rotation_angle = m_rotation;
  // rotate_polygon(privacy_mask_to_update, rotation_angle, m_frame_width, m_frame_height);
  return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskBlender::remove_privacy_mask(const std::string &id)
{
  std::unique_lock<std::mutex> lock(*m_privacy_mask_mutex);
  auto it = std::find_if(m_privacy_masks.begin(), m_privacy_masks.end(), [&id](const PolygonPtr &privacy_mask)
                         { return privacy_mask->id == id; });
  if (it == m_privacy_masks.end())
  {
    LOGGER__ERROR("PrivacyMaskBlender::remove_privacy_mask: Privacy mask with id {} not found", id);
    return media_library_return::MEDIA_LIBRARY_ERROR;
  }
  m_privacy_masks.erase(it);

  m_update_required = true;

  return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskBlender::set_color(const rgb_color_t &color)
{
  m_color = color;
  return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskBlender::set_rotation(const rotation_angle_t &rotation)
{
  if(m_rotation == rotation)
  {
    LOGGER__WARNING("PrivacyMaskBlender::set_rotation: Rotation is already set to {}, skipping update", rotation);
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
  }

  std::unique_lock<std::mutex> lock(*m_privacy_mask_mutex);

  // Rotate polygons
  // double rotation_angle = rotation - m_rotation;
  // LOGGER__INFO("PrivacyMaskBlender::set_rotation: Rotating polygons by {} degrees", rotation_angle);
  // if (rotate_polygons(m_privacy_masks, rotation_angle, m_frame_width, m_frame_height) != media_library_return::MEDIA_LIBRARY_SUCCESS)
  // {
  //   LOGGER__ERROR("PrivacyMaskBlender::set_rotation: Failed to rotate polygons");
  //   return media_library_return::MEDIA_LIBRARY_ERROR;
  // }

  // Swap frame width and height if needed
  if ((m_rotation == ROTATION_ANGLE_0 || m_rotation == ROTATION_ANGLE_180) && (rotation == ROTATION_ANGLE_90 || rotation == ROTATION_ANGLE_270))
  {
    std::swap(m_frame_width, m_frame_height);
  }
  else if ((m_rotation == ROTATION_ANGLE_90 || m_rotation == ROTATION_ANGLE_270) && (rotation == ROTATION_ANGLE_0 || rotation == ROTATION_ANGLE_180))
  {
    std::swap(m_frame_width, m_frame_height);
  }

  m_update_required = true;

  // Initialize buffer pool with new dimensions
  if (init_buffer_pool() != media_library_return::MEDIA_LIBRARY_SUCCESS)
  {
    LOGGER__ERROR("PrivacyMaskBlender::set_rotation: Failed to initialize buffer pool");
    return media_library_return::MEDIA_LIBRARY_ERROR;
  }

  m_rotation = rotation;
  return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

tl::expected<rgb_color_t, media_library_return> PrivacyMaskBlender::get_color()
{
  return m_color;
}

tl::expected<polygon, media_library_return> PrivacyMaskBlender::get_privacy_mask(const std::string &id)
{
  std::unique_lock<std::mutex> lock(*m_privacy_mask_mutex);
  auto it = std::find_if(m_privacy_masks.begin(), m_privacy_masks.end(), [&id](const PolygonPtr &privacy_mask)
                         { return privacy_mask->id == id; });
  if (it == m_privacy_masks.end())
  {
    LOGGER__ERROR("PrivacyMaskBlender::get_privacy_mask: Privacy mask with id {} not found", id);
    return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
  }
  return **it;
}

tl::expected<std::pair<uint, uint>, media_library_return> PrivacyMaskBlender::get_frame_size()
{
  if (m_frame_width == 0 || m_frame_height == 0)
  {
    LOGGER__ERROR("PrivacyMaskBlender::get_frame_size: Frame size is not set yet");
    return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
  }
  return std::make_pair(m_frame_width, m_frame_height);
}

media_library_return PrivacyMaskBlender::set_frame_size(const uint &width, const uint &height)
{
  std::unique_lock<std::mutex> lock(*m_privacy_mask_mutex);
  m_frame_width = width;
  m_frame_height = height;

  m_update_required = true;

  // Initialize buffer pool
  if (init_buffer_pool() != media_library_return::MEDIA_LIBRARY_SUCCESS)
  {
    LOGGER__ERROR("PrivacyMaskBlender::set_rotation: Failed to initialize buffer pool at new frame size");
    return media_library_return::MEDIA_LIBRARY_ERROR;
  }

  return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

tl::expected<std::vector<polygon>, media_library_return> PrivacyMaskBlender::get_all_privacy_masks()
{
  std::vector<polygon> privacy_masks;
  std::unique_lock<std::mutex> lock(*m_privacy_mask_mutex);
  for (auto &privacy_mask : m_privacy_masks)
  {
    privacy_masks.emplace_back(*privacy_mask);
  }
  return privacy_masks;
}

tl::expected<PrivacyMaskDataPtr, media_library_return> PrivacyMaskBlender::blend()
{
  std::unique_lock<std::mutex> lock(*m_privacy_mask_mutex);

  if(!m_update_required && m_latest_privacy_mask_data != NULL)
  {
    return m_latest_privacy_mask_data;
  }

  clean_latest_privacy_mask_data();

  m_latest_privacy_mask_data = std::make_shared<privacy_mask_data_t>();
  if (m_privacy_masks.empty())
  {
    m_latest_privacy_mask_data->rois_count = 0;
    return m_latest_privacy_mask_data;
  }

  if (m_buffer_pool == NULL)
  {
      LOGGER__ERROR("PrivacyMaskBlender::set_rotation: buffer pool is uninitialized");
      return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
  }

  // allocate memory for bitmask
  if (m_buffer_pool->acquire_buffer(m_latest_privacy_mask_data->bitmask) != MEDIA_LIBRARY_SUCCESS)
  {
    LOGGER__ERROR("PrivacyMaskBlender::blend: Failed to acquire buffer");
    return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
  }
  
  if (write_polygons_to_privacy_mask_data(m_privacy_masks, m_frame_width, m_frame_height, m_color, m_latest_privacy_mask_data) != media_library_return::MEDIA_LIBRARY_SUCCESS)
  {
    LOGGER__ERROR("PrivacyMaskBlender::blend: Failed to write polygon");
    return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
  }

  m_latest_privacy_mask_data->bitmask.sync_start();
  m_update_required = false;
  return m_latest_privacy_mask_data;
}
