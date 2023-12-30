/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
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

#include "buffer_utils.hpp"
#include "gsthailobuffermeta.hpp"
#include <gst/gst.h>
#include <stdio.h>
#include <string.h>

GstVideoMeta *add_video_meta_to_buffer(GstBuffer *buffer,
                                       GstVideoInfo *video_info,
                                       HailoMediaLibraryBufferPtr hailo_buffer)
{
  GstVideoAlignment alignment;
  gst_video_alignment_reset(&alignment);
  alignment.padding_right = hailo_buffer->hailo_pix_buffer->planes[0].bytesperline - GST_VIDEO_INFO_PLANE_STRIDE(video_info, 0);
  if (!gst_video_info_align(video_info, &alignment))
  {
    return NULL;
  }
  GstVideoMeta *meta = gst_buffer_add_video_meta_full(buffer,
                                                      GST_VIDEO_FRAME_FLAG_NONE,
                                                      GST_VIDEO_INFO_FORMAT(video_info),
                                                      GST_VIDEO_INFO_WIDTH(video_info),
                                                      GST_VIDEO_INFO_HEIGHT(video_info),
                                                      GST_VIDEO_INFO_N_PLANES(video_info),
                                                      video_info->offset,
                                                      video_info->stride);
  return meta;
}

/**
 * Creates a GstBuffer from a HailoMediaLibraryBufferPtr
 * Create GstMemory for each plane and set the destroy notify to hailo_media_library_plane_unref
 *
 * @param[in] hailo_buffer HailoMediaLibraryBufferPtr
 * @return GstBuffer
 */
GstBuffer *create_gst_buffer_from_hailo_buffer(HailoMediaLibraryBufferPtr hailo_buffer)
{
    GstBuffer *gst_outbuf = gst_buffer_new();

    for (uint i = 0; i < hailo_buffer->get_num_of_planes(); i++)
    {
        dsp_data_plane_t plane = hailo_buffer->hailo_pix_buffer->planes[i];
        std::pair<HailoMediaLibraryBufferPtr, guint> *hailo_plane;
        hailo_plane = new std::pair<HailoMediaLibraryBufferPtr, guint>();
        hailo_plane->first = hailo_buffer;
        hailo_plane->second = i;

        // log DSP buffer plane ptr: " << plane.userptr
        gst_buffer_append_memory(gst_outbuf,
                                 gst_memory_new_wrapped(GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, plane.userptr, plane.bytesused, 0, plane.bytesused,
                                                        hailo_plane, GDestroyNotify(hailo_media_library_plane_unref)));
    }
    gst_buffer_add_hailo_buffer_meta(gst_outbuf, hailo_buffer, gst_buffer_get_size(gst_outbuf));
    return gst_outbuf;
}

bool create_hailo_buffer_from_video_frame(GstVideoFrame *video_frame, hailo_media_library_buffer &hailo_buffer, GstHailoV4l2Meta *hailo_v4l2_meta)
{
    DspImagePropertiesPtr input_dsp_image_props_ptr = std::make_shared<dsp_image_properties_t>();
    if (!create_dsp_buffer_from_video_frame(video_frame, *input_dsp_image_props_ptr))
        return false;

    hailo_buffer.create(nullptr, input_dsp_image_props_ptr);
    if (hailo_v4l2_meta)
    {
        hailo_buffer.vsm = hailo_v4l2_meta->vsm;
        hailo_buffer.isp_ae_fps = hailo_v4l2_meta->isp_ae_fps;
        hailo_buffer.video_fd = hailo_v4l2_meta->video_fd;
    }
    hailo_media_library_buffer_ref(&hailo_buffer);
    return true;
}

void update_video_info_from_meta(GstBuffer *buffer, GstVideoInfo *video_info)
{
    GstVideoMeta *meta = gst_buffer_get_video_meta(buffer);
    if (meta)
    {
        video_info->finfo = gst_video_format_get_info (meta->format);
        video_info->width = meta->width;
        video_info->height = meta->height;
        for (guint i = 0; i < meta->n_planes; i++)
        {
            video_info->offset[i] = meta->offset[i];
            video_info->stride[i] = meta->stride[i];
        }
    }
}

/**
 * Creates and populates a dsp_image_properties_t
 * struct with data of a given GstVideoFrame
 *
 * @param[in] video_frame Gst video frame with plane data
 * @return populated dsp_image_properties_t of the video frame
 */
bool create_dsp_buffer_from_video_info(GstBuffer *buffer, GstVideoInfo *video_info, dsp_image_properties_t &dsp_image_props)
{
    bool ret = false;
    update_video_info_from_meta(buffer, video_info);
    GstVideoFormat format = GST_VIDEO_INFO_FORMAT(video_info);
    size_t image_width = GST_VIDEO_INFO_WIDTH(video_info);
    size_t image_height = GST_VIDEO_INFO_HEIGHT(video_info);
    size_t n_planes = GST_VIDEO_INFO_N_PLANES(video_info);

    switch (format)
    {
    case GST_VIDEO_FORMAT_RGB:
    {
        // RGB is non-planar, since all channels are interleaved, we treat the whole image as 1 plane
        GstMapInfo map_info0;
        GstMemory *mem0 = gst_buffer_peek_memory(buffer, 0);
        gst_memory_map(mem0, &map_info0, GST_MAP_READ);
        void *data = (void *)map_info0.data;
        size_t input_line_stride = GST_VIDEO_INFO_PLANE_STRIDE(video_info, 0);
        size_t input_size = GST_VIDEO_INFO_SIZE(video_info);

        // Allocate memory for the plane
        dsp_data_plane_t *plane = new dsp_data_plane_t[1];
        plane->userptr = data;
        plane->bytesperline = input_line_stride;
        plane->bytesused = input_size;

        gst_memory_unmap(mem0, &map_info0);

        // Fill in dsp_image_properties_t values
        dsp_image_props = (dsp_image_properties_t){
            .width = image_width,
            .height = image_height,
            .planes = plane,
            .planes_count = n_planes,
            .format = DSP_IMAGE_FORMAT_RGB};
        ret = true;
        break;
    }
    case GST_VIDEO_FORMAT_NV12:
    {
        // NV12 is semi-planar, where the Y channel is a seprate plane from the UV channels

        // Gather y channel info
        GstMapInfo map_info0;

        GstMemory *mem0 = gst_buffer_peek_memory(buffer, 0);
        gst_memory_map(mem0, &map_info0, GST_MAP_READ);
        void *y_channel_data = (void *)map_info0.data;
        size_t y_channel_stride = GST_VIDEO_INFO_PLANE_STRIDE(video_info, 0);
        size_t y_channel_size = y_channel_stride * image_height;
        dsp_data_plane_t y_plane_data = {
            .userptr = y_channel_data,
            .bytesperline = y_channel_stride,
            .bytesused = y_channel_size,
        };

        gst_memory_unmap(mem0, &map_info0);

        // Gather uv channel info
        GstMapInfo map_info1;
        GstMemory *mem1 = gst_buffer_peek_memory(buffer, 1);
        gst_memory_map(mem1, &map_info1, GST_MAP_READ);

        void *uv_channel_data = (void *)map_info1.data;
        size_t uv_channel_stride = GST_VIDEO_INFO_PLANE_STRIDE(video_info, 1);
        size_t uv_channel_size = uv_channel_stride * image_height / 2;
        dsp_data_plane_t uv_plane_data = {
            .userptr = uv_channel_data,
            .bytesperline = uv_channel_stride,
            .bytesused = uv_channel_size,
        };

        gst_memory_unmap(mem1, &map_info1);

        dsp_data_plane_t *yuv_planes = new dsp_data_plane_t[2];
        yuv_planes[0] = y_plane_data;
        yuv_planes[1] = uv_plane_data;

        GST_CAT_DEBUG(GST_CAT_DEFAULT, "DSP image properties from GstVideoFrame: buffer offset = %zu", GST_BUFFER_OFFSET(buffer));
        GST_CAT_DEBUG(GST_CAT_DEFAULT, "DSP image properties from GstVideoFrame: NV12, y ptr %p, y stride %zu, y size %zu, uv ptr %p, uv stride %zu, uv size %zu",
                      y_channel_data, y_channel_stride, y_channel_size, uv_channel_data, uv_channel_stride, uv_channel_size);
        // Fill in dsp_image_properties_t values
        dsp_image_props = (dsp_image_properties_t){
            .width = image_width,
            .height = image_height,
            .planes = yuv_planes,
            .planes_count = n_planes,
            .format = DSP_IMAGE_FORMAT_NV12};

        ret = true;
        break;
    }
    default:
    {
        ret = false;
        break;
    }
    }
    return ret;
}

/**
 * Creates and populates a dsp_image_properties_t
 * struct with data of a given GstVideoFrame
 *
 * @param[in] video_frame Gst video frame with plane data
 * @return populated dsp_image_properties_t of the video frame
 */
bool create_dsp_buffer_from_video_frame(GstVideoFrame *video_frame, dsp_image_properties_t &dsp_image_props)
{
    GstVideoFormat format = GST_VIDEO_FRAME_FORMAT(video_frame);
    size_t image_width = GST_VIDEO_FRAME_WIDTH(video_frame);
    size_t image_height = GST_VIDEO_FRAME_HEIGHT(video_frame);
    size_t n_planes = GST_VIDEO_FRAME_N_PLANES(video_frame);

    switch (format)
    {
    case GST_VIDEO_FORMAT_RGB:
    {
        // RGB is non-planar, since all channels are interleaved, we treat the whole image as 1 plane
        void *data = (void *)GST_VIDEO_FRAME_PLANE_DATA(video_frame, 0);
        size_t input_line_stride = GST_VIDEO_FRAME_PLANE_STRIDE(video_frame, 0);
        size_t input_size = GST_VIDEO_FRAME_SIZE(video_frame);

        // Allocate memory for the plane
        dsp_data_plane_t *plane = new dsp_data_plane_t[1];
        plane->userptr = data;
        plane->bytesperline = input_line_stride;
        plane->bytesused = input_size;

        // Fill in dsp_image_properties_t values
        dsp_image_props = (dsp_image_properties_t){
            .width = image_width,
            .height = image_height,
            .planes = plane,
            .planes_count = n_planes,
            .format = DSP_IMAGE_FORMAT_RGB};
        break;
    }
    case GST_VIDEO_FORMAT_YUY2:
    {
        GST_CAT_ERROR(GST_CAT_DEFAULT, "DSP image properties from GstVideoFrame failed: YUY2 not yet supported.");
        return false;
    }
    case GST_VIDEO_FORMAT_NV12:
    {
        // NV12 is semi-planar, where the Y channel is a seprate plane from the UV channels
        // Gather y channel info
        void *y_channel_data = (void *)GST_VIDEO_FRAME_PLANE_DATA(video_frame, 0);
        size_t y_channel_stride = GST_VIDEO_FRAME_PLANE_STRIDE(video_frame, 0);
        size_t y_channel_size = y_channel_stride * image_height;
        dsp_data_plane_t y_plane_data = {
            .userptr = y_channel_data,
            .bytesperline = y_channel_stride,
            .bytesused = y_channel_size,
        };
        // Gather uv channel info
        void *uv_channel_data = (void *)GST_VIDEO_FRAME_PLANE_DATA(video_frame, 1);
        size_t uv_channel_stride = GST_VIDEO_FRAME_PLANE_STRIDE(video_frame, 1);
        size_t uv_channel_size = uv_channel_stride * image_height / 2;
        dsp_data_plane_t uv_plane_data = {
            .userptr = uv_channel_data,
            .bytesperline = uv_channel_stride,
            .bytesused = uv_channel_size,
        };
        dsp_data_plane_t *yuv_planes = new dsp_data_plane_t[2];
        yuv_planes[0] = y_plane_data;
        yuv_planes[1] = uv_plane_data;

        GST_CAT_DEBUG(GST_CAT_DEFAULT, "DSP image properties from GstVideoFrame: buffer offset = %zu", GST_BUFFER_OFFSET(video_frame->buffer));
        GST_CAT_DEBUG(GST_CAT_DEFAULT, "DSP image properties from GstVideoFrame: NV12, y ptr %p, y stride %zu, y size %zu, uv ptr %p, uv stride %zu, uv size %zu",
                      y_channel_data, y_channel_stride, y_channel_size, uv_channel_data, uv_channel_stride, uv_channel_size);
        // Fill in dsp_image_properties_t values
        dsp_image_props = (dsp_image_properties_t){
            .width = image_width,
            .height = image_height,
            .planes = yuv_planes,
            .planes_count = n_planes,
            .format = DSP_IMAGE_FORMAT_NV12};
        break;
    }
    case GST_VIDEO_FORMAT_GRAY8:
    {
        void *data = (void *)GST_VIDEO_FRAME_PLANE_DATA(video_frame, 0);
        size_t image_stride = GST_VIDEO_FRAME_PLANE_STRIDE(video_frame, 0);
        size_t image_size = image_stride * image_height;

        dsp_data_plane_t plane_data = {
            .userptr = data,
            .bytesperline = image_stride,
            .bytesused = image_size,
        };

        dsp_data_plane_t *planes = new dsp_data_plane_t[1];
        planes[0] = plane_data;

        // Fill in dsp_image_properties_t values
        dsp_image_props = (dsp_image_properties_t){
            .width = image_width,
            .height = image_height,
            .planes = planes,
            .planes_count = n_planes,
            .format = DSP_IMAGE_FORMAT_GRAY8};
        break;
    }
    case GST_VIDEO_FORMAT_A420:
    {
        // A420 is fully planar (4:4:2:0), essentially I420 YUV with an extra alpha channel at full size
        // Gather channel info
        dsp_data_plane_t *a420_planes = new dsp_data_plane_t[n_planes];
        for (guint i = 0; i < n_planes; ++i)
        {
            void *channel_data = (void *)GST_VIDEO_FRAME_PLANE_DATA(video_frame, i);
            size_t channel_stride = GST_VIDEO_FRAME_PLANE_STRIDE(video_frame, i);
            size_t channel_size = channel_stride * image_height;
            if (i == 1 || i == 2)
                channel_size /= 2;
            dsp_data_plane_t plane_data = {
                .userptr = channel_data,
                .bytesperline = channel_stride,
                .bytesused = channel_size,
            };
            a420_planes[i] = plane_data;
        }

        // Fill in dsp_image_properties_t values
        dsp_image_props = (dsp_image_properties_t){
            .width = image_width,
            .height = image_height,
            .planes = a420_planes,
            .planes_count = n_planes,
            .format = DSP_IMAGE_FORMAT_A420};
        break;
    }
    default:
    {
        return false;
    }
    }

    return true;
}
