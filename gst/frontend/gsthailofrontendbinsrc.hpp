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
#include <mutex>
#include <gst/gst.h>
#include <tl/expected.hpp>
#include "media_library/isp_utils.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/config_manager.hpp"
#include "media_library/post_isp_denoise.hpp"
#include "media_library/pre_isp_denoise.hpp"
#include "media_library/hdr_manager.hpp"
#include "gsthailofrontend.hpp"

G_BEGIN_DECLS

#define GST_TYPE_HAILO_FRONTEND_BINSRC (gst_hailofrontendbinsrc_get_type())
#define GST_HAILO_FRONTEND_BINSRC(obj)                                                                                 \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_FRONTEND_BINSRC, GstHailoFrontendBinSrc))
#define GST_HAILO_FRONTEND_BINSRC_CLASS(klass)                                                                         \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_FRONTEND_BINSRC, GstHailoFrontendBinSrcClass))
#define GST_IS_HAILO_FRONTEND_BINSRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_FRONTEND_BINSRC))
#define GST_IS_HAILO_FRONTEND_BINSRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_FRONTEND_BINSRC))

typedef struct _GstHailoFrontendBinSrc GstHailoFrontendBinSrc;
typedef struct _GstHailoFrontendBinSrcParams GstHailoFrontendBinSrcParams;
typedef struct _GstHailoFrontendBinSrcClass GstHailoFrontendBinSrcClass;

struct _GstHailoFrontendBinSrcParams
{
    std::vector<GstPad *> srcpads;

    std::string config_file_path;
    std::string config_string;
    std::string device_id;

    bool m_elements_linked = false;
    GstElement *m_v4l2src = nullptr;
    GstElement *m_capsfilter = nullptr;
    GstElement *m_queue = nullptr;
    GstElement *m_frontend = nullptr;
    std::shared_ptr<ConfigManager> m_frontend_config_manager;
    frontend_config_t m_frontend_config;
    std::unique_ptr<HdrManager> m_hdr;
    MediaLibraryPreIspDenoisePtr m_pre_isp_denoise;
    std::mutex m_config_mutex;
    std::shared_ptr<v4l2::v4l2ControlManager> m_v4l2_ctrl_manager;
};

struct _GstHailoFrontendBinSrc
{
    GstBin base_hailofrontendbinsrc;
    GstHailoFrontendBinSrcParams *params = nullptr;
};

struct _GstHailoFrontendBinSrcClass
{
    GstBinClass base_hailofrontendbinsrc_class;
};

GType gst_hailofrontendbinsrc_get_type(void);

G_END_DECLS
