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
/**
 * @file gsthailofrontendbinsrc.hpp
 * @brief  GStreamer HAILO_FRONTEND_BINSRC bin element implementation
 *
 **/

#pragma once

#include <thread>
#include <fstream>
#include <memory>
#include <gst/gst.h>
#include <tl/expected.hpp>
#include "media_library/isp_utils.hpp"
#include "media_library/denoise.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/config_manager.hpp"
#include "imaging/hailo_hdr.hpp"
#include "imaging/hailo_stitch.hpp"
#include "gsthailofrontend.hpp"

G_BEGIN_DECLS

#define GST_TYPE_HAILO_FRONTEND_BINSRC (gst_hailofrontendbinsrc_get_type())
#define GST_HAILO_FRONTEND_BINSRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_FRONTEND_BINSRC, GstHailoFrontendBinSrc))
#define GST_HAILO_FRONTEND_BINSRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_FRONTEND_BINSRC, GstHailoFrontendBinSrcClass))
#define GST_IS_HAILO_FRONTEND_BINSRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_FRONTEND_BINSRC))
#define GST_IS_HAILO_FRONTEND_BINSRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_FRONTEND_BINSRC))

typedef struct _GstHailoFrontendBinSrc GstHailoFrontendBinSrc;
typedef struct _GstHailoFrontendBinSrcClass GstHailoFrontendBinSrcClass;

struct _GstHailoFrontendBinSrc
{
    GstBin base_hailofrontendbinsrc;
    std::vector<GstPad *> srcpads;

    gchar *config_file_path;
    std::string config_string;
    std::string device_id;

    gboolean m_elements_linked;
    GstElement *m_v4l2src;
    GstElement *m_capsfilter;
    GstElement *m_queue;
    GstElement *m_frontend;
    std::shared_ptr<ConfigManager> m_input_config_manager;
    std::shared_ptr<ConfigManager> m_isp_config_manager;
    std::shared_ptr<ConfigManager> m_hailort_config_manager;
    std::shared_ptr<ConfigManager> m_hdr_config_manager;
    input_video_config_t m_input_config;
    hailort_t m_hailort_config;
    denoise_config_t m_denoise_config;
    isp_t m_isp_config;
    hdr_config_t m_hdr_config;
    std::unique_ptr<HailortAsyncStitching> m_hdr_stitcher;
    std::unique_ptr<HDR::HDRManager> m_hdr;

    media_library_return observe_denoising(const MediaLibraryDenoise::callbacks_t &callback)
    {
        GstHailoFrontend *frontend = GST_HAILO_FRONTEND(m_frontend);
        return frontend->observe_denoising(callback);
    }
};

struct _GstHailoFrontendBinSrcClass
{
    GstBinClass base_hailofrontendbinsrc_class;
};

GType gst_hailofrontendbinsrc_get_type(void);

G_END_DECLS
