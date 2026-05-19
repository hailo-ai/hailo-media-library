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
 * @file gsthailovision.hpp
 * @brief GStreamer multi-output source element wrapping the MediaLibrary frontend.
 *
 * Provides the @c gsthailovision element — a GstElement that owns and
 * initialises the shared MediaLibrary instance for a pipeline. It manages
 * video capture, dewarp, and multi-resize via the frontend API and exposes
 * one source pad per output stream.
 *
 * ## Properties
 * - **config-string** (string, read-write, READY) — inline MediaLibrary JSON configuration.
 * - **config-path** (string, read-write, READY) — path to a MediaLibrary JSON config file.
 * - **profile-name** (string, read-write, PLAYING) — profile name to switch to at runtime.
 * - **current-profile** (pointer, read-only) — newly allocated config_profile_t* from medialib.
 *   Caller owns the returned pointer and must @c delete it.
 * - **override-profile** (pointer, write-only, PLAYING) — pointer to config_profile_t to apply
 *   via set_override_parameters() without a full profile switch.
 **/
#pragma once

#include <gst/gst.h>
#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

#define GST_TYPE_HAILO_VISION (gst_hailo_vision_get_type())
#define GST_HAILO_VISION(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_VISION, GstHailoVision))
#define GST_HAILO_VISION_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_VISION, GstHailoVisionClass))
#define GST_IS_HAILO_VISION(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_VISION))
#define GST_IS_HAILO_VISION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_VISION))
#define GST_HAILO_VISION_CAST(obj) ((GstHailoVision *)(obj))

typedef struct _GstHailoVision GstHailoVision;
typedef struct _GstHailoVisionParams GstHailoVisionParams;
typedef struct _GstHailoVisionClass GstHailoVisionClass;

/**
 * @brief GstElement subclass that drives the MediaLibrary frontend pipeline.
 *
 * Creates and owns the shared MediaLibrary instance (registered in
 * MediaLibInstanceRegistry) and drives the frontend capture loop.
 * Downstream @c gsthailoencoder elements obtain the same MediaLibrary
 * instance via the registry using the pipeline name as key.
 */
struct _GstHailoVision
{
    GstElement element;           ///< Parent GstElement instance.
    GstHailoVisionParams *params; ///< Private implementation data.
};

struct _GstHailoVisionClass
{
    GstElementClass parent_class;
};

G_GNUC_INTERNAL GType gst_hailo_vision_get_type(void);

G_END_DECLS
