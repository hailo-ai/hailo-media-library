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

#include "overlay_impl.hpp"
#include "buffer_utils/buffer_utils.hpp"
#include "media_library/media_library_logger.hpp"

OverlayImpl::OverlayImpl(std::string id, float x, float y, float width, float height, unsigned int z_index, unsigned int angle, osd::rotation_alignment_policy_t rotation_policy, bool enabled = true) : m_id(id), m_x(x), m_y(y), m_width(width), m_height(height), m_z_index(z_index), m_angle(angle), m_rotation_policy(rotation_policy), m_enabled(enabled)
{
}

cv::Mat OverlayImpl::resize_mat(cv::Mat mat, int width, int height)
{
    // ensure even dimensions, round up not to clip image
    width += (width % 2);
    height += (height % 2);

    // resize the mat image to target size
    cv::Mat resized_image;
    cv::resize(mat, resized_image, cv::Size(width, height), 0, 0, cv::INTER_AREA);
    return resized_image;
}

cv::Mat OverlayImpl::rotate_mat(cv::Mat mat, uint angle, osd::rotation_alignment_policy_t alignment_policy, cv::Point *center_drift)
{
    *center_drift = cv::Point(0, 0);
    if (angle == 0)
    {
        return mat;
    }

    // Get the image's center
    cv::Point2f center(mat.cols / 2.0, mat.rows / 2.0);

    // Get the rotation matrix
    cv::Mat rot = cv::getRotationMatrix2D(center, angle, 1.0);

    // Define the destination image
    cv::Rect bbox = cv::RotatedRect(center, mat.size(), angle).boundingRect();

    // Ensure the bounding box dimensions are even
    bbox.width += bbox.width % 2;
    bbox.height += bbox.height % 2;

    // Adjust transformation matrix to rotate around center and not top-left corner
    rot.at<double>(0, 2) += bbox.width / 2.0 - center.x;
    rot.at<double>(1, 2) += bbox.height / 2.0 - center.y;

    cv::Mat result;
    cv::warpAffine(mat, result, rot, bbox.size());

    if (alignment_policy == osd::rotation_alignment_policy_t::CENTER)
    {
        // adjust top left corner because center of image may move during rotation
        cv::Point2f new_center(result.cols / 2.0, result.rows / 2.0);
        *center_drift = center - new_center;
    }
    return result;
}

GstVideoFrame OverlayImpl::gst_video_frame_from_mat_bgra(cv::Mat mat)
{
    // Create caps at BGRA format and required size
    GstCaps *caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGRA",
                                        "width", G_TYPE_INT, mat.cols,
                                        "height", G_TYPE_INT, mat.rows,
                                        NULL);
    // Create GstVideoInfo meta from those caps
    GstVideoInfo *image_info = gst_video_info_new();
    gst_video_info_from_caps(image_info, caps);
    // Create a GstBuffer from the cv::mat, allowing for contiguous memory
    GstBuffer *buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY,
                                                    mat.data,
                                                    mat.total() * mat.elemSize(),
                                                    0, mat.total() * mat.elemSize(),
                                                    NULL, NULL);
    // Create and map a GstVideoFrame from the GstVideoInfo and GstBuffer
    GstVideoFrame frame;
    gst_video_frame_map(&frame, image_info, buffer, GST_MAP_READ);
    gst_video_info_free(image_info);
    gst_caps_unref(caps);
    return frame;
}

media_library_return OverlayImpl::create_gst_video_frame(uint width, uint height, std::string format, GstVideoFrame *frame)
{
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    // Create caps at format and required size
    GstCaps *caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format.c_str(),
                                        "width", G_TYPE_INT, width,
                                        "height", G_TYPE_INT, height,
                                        NULL);

    void *buffer_ptr = NULL;

    // Create GstVideoInfo meta from those caps
    GstVideoInfo *image_info = gst_video_info_new();
    gst_video_info_from_caps(image_info, caps);
    uint buffer_size = image_info->size;
    dsp_status buffer_status = dsp_utils::create_hailo_dsp_buffer(buffer_size, &buffer_ptr);

    if (buffer_status != DSP_SUCCESS)
    {
        gst_caps_unref(caps);
        LOGGER__ERROR("Error: create_hailo_dsp_buffer - failed to create buffer");
        return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
    }

    // Create a GstBuffer from the cv::mat, allowing for contiguous memory
    GstBuffer *buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY,
                                                    buffer_ptr,
                                                    buffer_size,
                                                    0, buffer_size,
                                                    buffer_ptr, GDestroyNotify(dsp_utils::release_hailo_dsp_buffer));

    // Create and map a GstVideoFrame from the GstVideoInfo and GstBuffer
    gst_video_frame_map(frame, image_info, buffer, GST_MAP_READ);
    gst_buffer_unref(buffer);
    gst_video_info_free(image_info);
    gst_caps_unref(caps);
    return ret;
}

media_library_return OverlayImpl::end_sync_buffer(GstVideoFrame *video_frame)
{
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    for (int i = 0; i < (int)GST_VIDEO_FRAME_N_PLANES(video_frame); i++)
    {
        void *buffer_ptr = (void *)GST_VIDEO_FRAME_PLANE_DATA(video_frame, i);
        media_library_return status = DmaMemoryAllocator::get_instance().dmabuf_sync_end(buffer_ptr);
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Error: dmabuf_sync_end - failed to sync buffer for plane ", i);
            return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
        }
    }
    return ret;
}

media_library_return OverlayImpl::create_dma_a420_video_frame(uint width, uint height, GstVideoFrame *frame)
{
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    // Create caps at format and required size
    GstCaps *caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "A420",
                                        "width", G_TYPE_INT, width,
                                        "height", G_TYPE_INT, height,
                                        NULL);

    // Create GstVideoInfo meta from those caps
    GstVideoInfo *image_info = gst_video_info_new();
    gst_video_info_from_caps(image_info, caps);

    // Create a GstBuffer from the dma buffers, allowing for contiguous memory
    GstBuffer *buffer = gst_buffer_new();
    // Create 4 dma buffers for each of the 4 planes of an A420 image
    for (int i = 0; i < 4; i++)
    {
        void *buffer_ptr = NULL;
        // Calculate plane sizes
        size_t channel_stride = image_info->stride[i];
        size_t channel_size = channel_stride * height;
        if (i == 1 || i == 2)
            channel_size /= 2;

        // Create the dma buffer
        media_library_return status = DmaMemoryAllocator::get_instance().allocate_dma_buffer(channel_size, &buffer_ptr);
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            gst_caps_unref(caps);
            LOGGER__ERROR("Error: create_hailo_dsp_buffer - failed to create buffer for plane ", i);
            return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
        }
        DmaMemoryAllocator::get_instance().dmabuf_sync_start(buffer_ptr); // start sync so that we can write to it

        // Wrap the dma buffer as continuous GstMemory, add the plane to the GstBuffer
        GstMemory *mem = gst_memory_new_wrapped(GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS,
                                                buffer_ptr,
                                                channel_size,
                                                0, channel_size,
                                                buffer_ptr, GDestroyNotify(destroy_dma_buffer));
        gst_buffer_insert_memory(buffer, -1, mem);
    }

    // Add GstVideoMeta so that mapping the buffer (ie: gst_video_frame_map) does not change the buffer layout (GstMemory per plane)
    (void)gst_buffer_add_video_meta_full(buffer,
                                         GST_VIDEO_FRAME_FLAG_NONE,
                                         GST_VIDEO_INFO_FORMAT(image_info),
                                         GST_VIDEO_INFO_WIDTH(image_info),
                                         GST_VIDEO_INFO_HEIGHT(image_info),
                                         GST_VIDEO_INFO_N_PLANES(image_info),
                                         image_info->offset,
                                         image_info->stride);

    // Create and map a GstVideoFrame from the GstVideoInfo and GstBuffer
    gst_video_frame_map(frame, image_info, buffer, GST_MAP_WRITE);
    gst_buffer_unref(buffer);
    gst_video_info_free(image_info);
    gst_caps_unref(caps);
    return ret;
}

media_library_return OverlayImpl::convert_2_dma_video_frame(GstVideoFrame *src_frame, GstVideoFrame *dest_frame, GstVideoFormat dest_format)
{
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;

    // Prepare the video info and set the new format
    GstVideoInfo *dest_info = gst_video_info_new();
    gst_video_info_set_format(dest_info, dest_format, src_frame->info.width, src_frame->info.height);
    // Prepare the GstVideoConverter that will facilitate the conversion
    GstVideoConverter *converter = gst_video_converter_new(&src_frame->info, dest_info, NULL);

    // Create a DMA capable buffer for the A420 format
    media_library_return a420_dma_status = create_dma_a420_video_frame(src_frame->info.width, src_frame->info.height, dest_frame);
    if (a420_dma_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Error: create_dma_a420_video_frame - failed to create buffer");
        ret = MEDIA_LIBRARY_DSP_OPERATION_ERROR;
    }
    else
    {
        // Make the conversion
        gst_video_converter_frame(converter, src_frame, dest_frame);
        end_sync_buffer(dest_frame); // end sync so that DMA is written
    }

    gst_video_converter_free(converter);
    gst_video_info_free(dest_info);

    return ret;
}

tl::expected<std::tuple<int, int>, media_library_return> OverlayImpl::calc_xy_offsets(std::string id, float x_norm, float y_norm, size_t &overlay_width, size_t &overlay_height, int image_width, int image_height, int x_drift, int y_drift)
{
    if (x_norm < 0 || x_norm > 1 || y_norm < 0 || y_norm > 1)
    {
        LOGGER__ERROR("overlay {} x and y offsets must be normalized between 0 and 1", id);
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }

    int x_offset = x_norm * image_width;
    int y_offset = y_norm * image_height;

    x_offset += x_drift;
    y_offset += y_drift;

    if (x_offset + overlay_width > static_cast<size_t>(image_width))
    {
        if (x_offset >= image_width)
        {
            LOGGER__ERROR("overlay {} can't fit in frame! Adjust x offset. ({})", id, x_offset);
            return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
        }

        overlay_width = image_width - x_offset;
        overlay_width -= (overlay_width % 2);
    }

    if (y_offset + overlay_height > static_cast<size_t>(image_height))
    {
        if (y_offset >= image_height)
        {
            LOGGER__ERROR("overlay {} can't fit in frame! Adjust y offset. ({})", id, y_offset);
            return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
        }

        overlay_height = image_height - y_offset;
        overlay_height -= (overlay_height % 2);
    }

    if (x_offset < 0)
    {
        LOGGER__ERROR("overlay {} can't fit in frame! Adjust x offset. ({})", id, x_offset);
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }

    if (y_offset < 0)
    {
        LOGGER__ERROR("overlay {} can't fit in frame! Adjust y offset. ({})", id, y_offset);
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }

    std::tuple ret = {x_offset, y_offset};
    return ret;
}

bool OverlayImpl::get_enabled()
{
    std::shared_lock lock(m_overlay_mutex);
    return m_enabled;
}
void OverlayImpl::set_enabled(bool enabled)
{
    std::unique_lock lock(m_overlay_mutex);
    m_enabled = enabled;
}

OverlayImpl::~OverlayImpl()
{
    // free all resources
    free_resources();
}

void OverlayImpl::free_resources()
{
    for (auto &video_frame : m_video_frames)
    {
        gst_video_frame_unmap(&video_frame);
    }
    for (auto &dsp_overlay : m_dsp_overlays)
    {
        dsp_utils::free_overlay_property_planes(&dsp_overlay);
    }

    m_video_frames.clear();
    m_dsp_overlays.clear();
}

bool OverlayImpl::operator<(const OverlayImpl &other)
{
    return m_z_index < other.m_z_index;
}

std::set<OverlayImplPtr>::iterator OverlayImpl::get_priority_iterator()
{
    return m_priority_iterator;
}

void OverlayImpl::set_priority_iterator(std::set<OverlayImplPtr>::iterator priority_iterator)
{
    m_priority_iterator = priority_iterator;
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> OverlayImpl::create_dsp_overlays(int frame_width, int frame_height)
{
    if (frame_width == 0 || frame_height == 0)
    {
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    // Free all existing resources before creating new ones
    free_resources();

    media_library_return status;

    GstVideoFrame dest_frame;
    if (m_image_mat.empty())
    {
        LOGGER__ERROR("m_image_mat is empty");
        status = MEDIA_LIBRARY_ERROR;
        return tl::make_unexpected(status);
    }

    cv::Mat mat = m_image_mat;
    cv::Point center_drift{0, 0};
    if (m_angle != 0)
    {
        mat = OverlayImpl::rotate_mat(m_image_mat, m_angle, m_rotation_policy, &center_drift);
        LOGGER__DEBUG("Rotated OSD by {} degrees, center drifted by {} pixels, around {}", m_angle, center_drift, m_rotation_policy);
    }

    GstVideoFrame gst_bgra_image = gst_video_frame_from_mat_bgra(mat);
    status = convert_2_dma_video_frame(&gst_bgra_image, &dest_frame, GST_VIDEO_FORMAT_A420);

    gst_buffer_unref(gst_bgra_image.buffer);
    gst_video_frame_unmap(&gst_bgra_image);

    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }

    dsp_image_properties_t dsp_image;
    create_dsp_buffer_from_video_frame(&dest_frame, dsp_image);
    m_video_frames.push_back(dest_frame);
    auto offsets_expected = calc_xy_offsets(m_id, m_x, m_y, dsp_image.width, dsp_image.height, frame_width, frame_height, center_drift.x, center_drift.y);
    if (!offsets_expected.has_value())
    {
        return tl::make_unexpected(offsets_expected.error());
    }

    auto [x_offset, y_offset] = offsets_expected.value();
    dsp_overlay_properties_t dsp_overlay =
        {
            .overlay = dsp_image,
            .x_offset = (size_t)x_offset,
            .y_offset = (size_t)y_offset,
        };

    m_dsp_overlays.push_back(dsp_overlay);

    set_enabled(true);
    return m_dsp_overlays;
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> OverlayImpl::get_dsp_overlays()
{
    if (!get_enabled())
    {
        LOGGER__ERROR("overlay not ready to blend");
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    return m_dsp_overlays;
}
