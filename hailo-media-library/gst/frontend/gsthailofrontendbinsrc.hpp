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

#include <gst/gst.h>
#include <glib-object.h>
#include <glib.h>
#include <memory>
#include <mutex>
#include <string>
#include <functional>

#include "media_library/config_manager.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/config_parser.hpp"
#include "media_library/pre_isp_denoise.hpp"
#include "media_library/hdr_manager.hpp"
#include "isp_manager.hpp"
#include "v4l2_ctrl.hpp"

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
    std::string config_file_path;
    std::string config_string;
    std::string device_id;

    bool m_elements_linked = false;
    // Pools live for the element's lifetime and only init() once, on the first
    // start. preallocate_from_config must therefore also run only once —
    // otherwise subsequent NULL_TO_READY transitions waste a full bg prealloc
    // that no pool will consume.
    bool m_prealloc_done = false;
    GstElement *m_v4l2src = nullptr;
    GstElement *m_capsfilter = nullptr;
    GstElement *m_queue = nullptr;
    GstElement *m_frontend = nullptr;
    std::unique_ptr<ConfigManagerInteractor, std::function<void(ConfigManagerInteractor *)>>
        m_config_manager_interactor{nullptr,
                                    std::default_delete<ConfigManagerInteractor>{}}; // may be owning or non-owning
    std::shared_ptr<ConfigParser> m_frontend_config_parser;
    frontend_config_t m_frontend_config;
    std::unique_ptr<IspManager>
        m_isp_manager; // order matters! should be before hdr and pre_isp_denoise for proper destruction
    std::unique_ptr<HdrManager> m_hdr;
    MediaLibraryPreIspDenoisePtr m_pre_isp_denoise;
    std::mutex m_config_mutex;
    std::shared_ptr<v4l2::v4l2ControlManager> m_v4l2_ctrl_manager;
    guint64 frame_count = 0;
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
