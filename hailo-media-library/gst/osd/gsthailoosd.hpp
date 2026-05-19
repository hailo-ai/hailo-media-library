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
#pragma once

#include <gst/base/gstbasetransform.h>
#include <glib-object.h>
#include <glib.h>
#include <memory>
#include <string>

#include "media_library/privacy_mask.hpp"
#include "osd.hpp"

G_BEGIN_DECLS

#define GST_TYPE_HAILO_OSD (gst_hailoosd_get_type())
#define GST_HAILO_OSD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_OSD, GstHailoOsd))
#define GST_HAILO_OSD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_OSD, GstHailoOsdClass))
#define GST_IS_HAILO_OSD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_OSD))
#define GST_IS_HAILO_OSD_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_OSD))

typedef struct _GstHailoOsd GstHailoOsd;
typedef struct _GstHailoOsdParams GstHailoOsdParams;
typedef struct _GstHailoOsdClass GstHailoOsdClass;

struct _GstHailoOsdParams
{
    std::string config_path;
    std::string config_str;
    std::string stream_id;
    std::shared_ptr<osd::OsdBlender> osd_blender;
    std::shared_ptr<osd::Configurer> osd_configurer;
    osd::OverlayRepositoryPtr osd_repository;
    std::shared_ptr<PrivacyMask> privacy_mask_blender;
    std::shared_ptr<PrivacyMaskConfigurer> privacy_mask_configurer;
    gboolean wait_for_writable_buffer = false;
    bool initialized = false;
};

struct _GstHailoOsd
{
    GstBaseTransform base_hailoosd;
    GstHailoOsdParams *params = nullptr;
};

struct _GstHailoOsdClass
{
    GstBaseTransformClass parent_class;
};

GType gst_hailoosd_get_type(void);

G_END_DECLS
