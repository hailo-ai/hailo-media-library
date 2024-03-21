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

#include "buffer_utils.hpp"
#include "gsthailobuffermeta.hpp"
#include "hailo_v4l2/hailo_v4l2.h"
#include "hailo_v4l2/hailo_v4l2_meta.h"
#include <gst/gst.h>
#include <gst/allocators/gstdmabuf.h>
#include <stdio.h>
#include <string.h>

static bool create_hailo_buffer_from_video_frame(GstVideoFrame *video_frame, hailo_media_library_buffer &hailo_buffer);

/**
 * Creates a HailoMediaLibraryBufferPtr from a GstBuffer.
 * The HailoMediaLibraryBufferPtr is created from the GstVideoFrame or from GstHailoBufferMeta
 * if it exists.
 * If the GstVideoFrame is used, the GstVideoInfo is created from the GstCaps.
 *
 * @param[in] buffer GstBuffer to create HailoMediaLibraryBufferPtr from.
 * @param[in] caps GstCaps to create GstVideoInfo from.
 * @return HailoMediaLibraryBufferPtr created from GstBuffer.
 */
HailoMediaLibraryBufferPtr hailo_buffer_from_gst_buffer(GstBuffer *buffer, GstCaps *caps)
{
    GstHailoBufferMeta *hailo_buffer_meta = gst_buffer_get_hailo_buffer_meta(buffer);
    GstVideoInfo *video_info;
    GstVideoFrame video_frame;
    hailo_media_library_buffer hailo_buffer;
    if (hailo_buffer_meta)
    {
        return hailo_buffer_meta->buffer_ptr;
    }
    video_info = gst_video_info_new_from_caps(caps);
    if (!video_info)
    {
        GST_CAT_ERROR(GST_CAT_DEFAULT, "Failed to get video info from caps");
        return nullptr;
    }
    if (!gst_video_frame_map(&video_frame, video_info, buffer, GST_MAP_READ))
    {
        GST_CAT_ERROR(GST_CAT_DEFAULT, "Failed to map video frame");
        gst_video_info_free(video_info);
        return nullptr;
    }
    if (!create_hailo_buffer_from_video_frame(&video_frame, hailo_buffer))
    {
        GST_CAT_ERROR(GST_CAT_DEFAULT, "Failed to create hailo buffer from video frame");
        gst_video_frame_unmap(&video_frame);
        gst_video_info_free(video_info);
        return nullptr;
    }
    gst_video_frame_unmap(&video_frame);
    gst_video_info_free(video_info);
    return std::make_shared<hailo_media_library_buffer>(std::move(hailo_buffer));
}

static GstVideoMeta *add_video_meta_to_buffer(GstBuffer *buffer,
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

static GstVideoInfo *video_info_from_caps(HailoMediaLibraryBufferPtr hailo_buffer, GstCaps *caps)
{
    GstVideoInfo *video_info = gst_video_info_new();
    if (!gst_video_info_from_caps(video_info, caps))
    {
        GST_CAT_ERROR(GST_CAT_DEFAULT, "Failed to get video info from caps");
        gst_video_info_free(video_info);
        return nullptr;
    }
    if (hailo_buffer->hailo_pix_buffer->width != (guint)video_info->width || hailo_buffer->hailo_pix_buffer->height != (guint)video_info->height)
    {
        GST_CAT_ERROR(GST_CAT_DEFAULT, "Output frame size (%ld, %ld) does not match srcpad size (%d, %d)", hailo_buffer->hailo_pix_buffer->width, hailo_buffer->hailo_pix_buffer->height, video_info->width, video_info->height);
        gst_video_info_free(video_info);
        return nullptr;
    }
    return video_info;
}

/**
 * Creates a GstBuffer from a HailoMediaLibraryBufferPtr
 * Create GstMemory for each plane and set the destroy notify to hailo_media_library_plane_unref
 *
 * @param[in] hailo_buffer HailoMediaLibraryBufferPtr
 * @return GstBuffer
 */
GstBuffer *gst_buffer_from_hailo_buffer(HailoMediaLibraryBufferPtr hailo_buffer, GstCaps *caps)
{
    GstBuffer *gst_outbuf = gst_buffer_new();

    for (uint i = 0; i < hailo_buffer->get_num_of_planes(); i++)
    {
        dsp_data_plane_t plane = hailo_buffer->hailo_pix_buffer->planes[i];
        std::pair<HailoMediaLibraryBufferPtr, guint> *hailo_plane;
        hailo_plane = new std::pair<HailoMediaLibraryBufferPtr, guint>();
        hailo_plane->first = hailo_buffer;
        hailo_plane->second = i;

        void *data;
        if (hailo_buffer->hailo_pix_buffer->memory == DSP_MEMORY_TYPE_DMABUF)
        {
            (void)DmaMemoryAllocator::get_instance().get_ptr(plane.fd, &data);
        }
        else
        {
            data = plane.userptr;
        }

        // log DSP buffer plane ptr: " << plane.userptr
        gst_buffer_append_memory(gst_outbuf,
                                 gst_memory_new_wrapped(GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, data, plane.bytesused, 0, plane.bytesused,
                                                        hailo_plane, GDestroyNotify(hailo_media_library_plane_unref)));
    }
    gst_buffer_add_hailo_buffer_meta(gst_outbuf, hailo_buffer, gst_buffer_get_size(gst_outbuf));

    if (caps)
    {
        GstVideoInfo *video_info = video_info_from_caps(hailo_buffer, caps);
        if (!video_info)
        {
            GST_CAT_ERROR(GST_CAT_DEFAULT, "Failed to get video info from caps");
            gst_buffer_unref(gst_outbuf);
            return nullptr;
        }

        GstVideoMeta *meta = add_video_meta_to_buffer(gst_outbuf, video_info, hailo_buffer);
        gst_video_info_free(video_info);
        if (!meta)
        {
            GST_CAT_ERROR(GST_CAT_DEFAULT, "Failed to add video meta to buffer");
            gst_buffer_unref(gst_outbuf);
            return nullptr;
        }
    }
    else
    {
        GST_CAT_WARNING(GST_CAT_DEFAULT, "No caps provided, not adding video meta to buffer");
    }

    // add hailo_v4l2_meta
    gst_buffer_add_hailo_v4l2_meta(gst_outbuf,
                                   hailo_buffer->video_fd,
                                   hailo_buffer->buffer_index,
                                   hailo_buffer->vsm,
                                   hailo_buffer->isp_ae_fps,
                                   hailo_buffer->isp_ae_converged
                                   );

    return gst_outbuf;
}

static bool create_hailo_buffer_from_video_frame(GstVideoFrame *video_frame, hailo_media_library_buffer &hailo_buffer)
{
    GstHailoV4l2Meta *hailo_v4l2_meta = nullptr;
    GType hailo_v4l2_type = g_type_from_name(HAILO_V4L2_META_API_NAME);
    if (hailo_v4l2_type)
    {
        hailo_v4l2_meta = reinterpret_cast<GstHailoV4l2Meta *>(gst_buffer_get_meta(video_frame->buffer, hailo_v4l2_type));
    }
    DspImagePropertiesPtr input_dsp_image_props_ptr = std::make_shared<dsp_image_properties_t>();
    if (!create_dsp_buffer_from_video_frame(video_frame, *input_dsp_image_props_ptr))
        return false;

    hailo_buffer.create(nullptr, input_dsp_image_props_ptr);
    if (hailo_v4l2_meta)
    {
        hailo_buffer.vsm = hailo_v4l2_meta->vsm;
        hailo_buffer.isp_ae_fps = hailo_v4l2_meta->isp_ae_fps;
        hailo_buffer.isp_ae_converged = hailo_v4l2_meta->isp_ae_converged;
        hailo_buffer.video_fd = hailo_v4l2_meta->video_fd;
    }
    hailo_media_library_buffer_ref(&hailo_buffer);
    return true;
}

int get_fd(GstVideoFrame *video_frame, int index)
{
    int fd = -1;

    // First, try to get the fd using the DmaMemoryAllocator
    void *data = (void *)GST_VIDEO_FRAME_PLANE_DATA(video_frame, index);
    if (DmaMemoryAllocator::get_instance().get_fd(data, fd) == MEDIA_LIBRARY_SUCCESS)
    {
        return fd;
    }

    // If failed, try to get the fd from GStreamer
    // GstMemory *memory = gst_buffer_peek_memory(video_frame->buffer, index);
    // if (memory && gst_is_dmabuf_memory(memory))
    // {
    //     fd = gst_dmabuf_memory_get_fd(memory);
    // }

    return fd;
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
        int y_channel_fd = get_fd(video_frame, 0);

        dsp_data_plane_t y_plane_data = {
            .bytesperline = y_channel_stride,
            .bytesused = y_channel_size};

        if (y_channel_fd == -1)
        {
            // TODO: Remove in the future
            y_plane_data.userptr = y_channel_data;
        }
        else
        {
            y_plane_data.fd = y_channel_fd;
        }
        // Gather uv channel info
        void *uv_channel_data = (void *)GST_VIDEO_FRAME_PLANE_DATA(video_frame, 1);
        size_t uv_channel_stride = GST_VIDEO_FRAME_PLANE_STRIDE(video_frame, 1);
        size_t uv_channel_size = uv_channel_stride * image_height / 2;
        int uv_channel_fd = get_fd(video_frame, 1);

        dsp_data_plane_t uv_plane_data = {
            .bytesperline = uv_channel_stride,
            .bytesused = uv_channel_size,
        };

        if (uv_channel_fd == -1)
        {
            // TODO: Remove in the future
            uv_plane_data.userptr = uv_channel_data;
        }
        else
        {
            uv_plane_data.fd = uv_channel_fd;
        }

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

        if (y_channel_fd == -1)
        {
            // TODO: Remove in the future
            dsp_image_props.memory = DSP_MEMORY_TYPE_USERPTR;
        }
        else
        {
            dsp_image_props.memory = DSP_MEMORY_TYPE_DMABUF;
        }

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
            int channel_fd = get_fd(video_frame, i);

            dsp_data_plane_t plane_data = {
                .bytesperline = channel_stride,
                .bytesused = channel_size,
            };
            if (channel_fd == -1)
            {
                // TODO: Remove in the future
                plane_data.userptr = channel_data;
            }
            else
            {
                plane_data.fd = channel_fd;
            }

            a420_planes[i] = plane_data;
        }

        // Fill in dsp_image_properties_t values
        dsp_image_props = (dsp_image_properties_t){
            .width = image_width,
            .height = image_height,
            .planes = a420_planes,
            .planes_count = n_planes,
            .format = DSP_IMAGE_FORMAT_A420};

        if (get_fd(video_frame, 0) == -1)
        {
            // TODO: Remove in the future
            dsp_image_props.memory = DSP_MEMORY_TYPE_USERPTR;
        }
        else
        {
            dsp_image_props.memory = DSP_MEMORY_TYPE_DMABUF;
        }

        break;
    }
    default:
    {
        return false;
    }
    }

    return true;
}
