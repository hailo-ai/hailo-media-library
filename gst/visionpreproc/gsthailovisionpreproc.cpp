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
#include <tl/expected.hpp> 
#include "gsthailovisionpreproc.hpp"
#include "buffer_utils/buffer_utils.hpp"
#include "v4l2_vsm/hailo_vsm_meta.h"
#include "v4l2_vsm/hailo_vsm.h"
#include <gst/video/video.h>

GST_DEBUG_CATEGORY_STATIC(gst_hailo_vision_preproc_debug);
#define GST_CAT_DEFAULT gst_hailo_vision_preproc_debug

// Pad Templates
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink",
                                                                    GST_PAD_SINK,
                                                                    GST_PAD_ALWAYS,
                                                                    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src_%u",
                                                                   GST_PAD_SRC,
                                                                   GST_PAD_REQUEST,
                                                                   GST_STATIC_CAPS_ANY);

#define _do_init \
    GST_DEBUG_CATEGORY_INIT(gst_hailo_vision_preproc_debug, "hailovisionpreproc", 0, "Hailo Vision Pre Proc element");

#define gst_hailo_vision_preproc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoVisionPreProc, gst_hailo_vision_preproc, GST_TYPE_ELEMENT, _do_init);

static void gst_hailo_vision_preproc_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailo_vision_preproc_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static GstFlowReturn gst_hailo_vision_preproc_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer);
static GstPad *gst_hailo_vision_preproc_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name, const GstCaps *caps);
static void gst_hailo_vision_preproc_release_pad(GstElement *element, GstPad *pad);
static gboolean gst_hailo_vision_preproc_sink_query(GstPad *pad, GstObject *parent, GstQuery *query);
static GstStateChangeReturn gst_hailo_vision_preproc_change_state(GstElement *element, GstStateChange transition);
static void gst_hailo_vision_preproc_dispose(GObject *object);
static void gst_hailo_vision_preproc_reset(GstHailoVisionPreProc *self);
static void gst_hailo_vision_preproc_release_srcpad(GstPad *pad, GstHailoVisionPreProc *self);

static gboolean gst_hailo_handle_caps_query(GstHailoVisionPreProc *self, GstPad *pad, GstQuery *query);
static gboolean gst_hailo_vision_preproc_sink_event(GstPad *pad,
                                                    GstObject *parent, GstEvent *event);
static gboolean gst_hailo_handle_caps_event(GstHailoVisionPreProc *self, GstCaps *caps);
static gboolean gst_hailo_set_srcpad_caps(GstHailoVisionPreProc *self, GstPad *srcpad, output_resolution_t &output_res);
static gboolean intersect_peer_srcpad_caps(GstHailoVisionPreProc *self, GstPad *sinkpad, GstPad *srcpad, output_resolution_t &output_res);
static gboolean gst_hailo_vision_preproc_create(GstHailoVisionPreProc *self);

enum
{
  PROP_PAD_0,
  PROP_CONFIG_FILE_PATH,
  PROP_CONFIG_STRING,
};

static void
gst_hailo_vision_preproc_class_init(GstHailoVisionPreProcClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *)klass;
  gstelement_class = (GstElementClass *)klass;

  gobject_class->set_property = gst_hailo_vision_preproc_set_property;
  gobject_class->get_property = gst_hailo_vision_preproc_get_property;
  gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_hailo_vision_preproc_dispose);

  g_object_class_install_property(gobject_class, PROP_CONFIG_FILE_PATH,
                                  g_param_spec_string("config-file-path", "Config file path",
                                                      "JSON config file path to load",
                                                      "",
                                                      (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

  g_object_class_install_property(gobject_class, PROP_CONFIG_STRING,
                                  g_param_spec_string("config-string", "Config string",
                                                      "JSON config string to load",
                                                      "",
                                                      (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
  // Pad templates
  gst_element_class_add_static_pad_template(gstelement_class, &src_template);
  gst_element_class_add_static_pad_template(gstelement_class, &sink_template);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_hailo_vision_preproc_change_state);

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR(gst_hailo_vision_preproc_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR(gst_hailo_vision_preproc_release_pad);

  gst_element_class_set_static_metadata(gstelement_class, "Hailo Vision PreProc", "Hailo Vision PreProc",
                                        "Hailo Vision PreProc", "Hailo");
}

static void
gst_hailo_vision_preproc_init(GstHailoVisionPreProc *vision_preproc)
{
  GST_DEBUG_OBJECT(vision_preproc, "init");
  vision_preproc->config_file_path = NULL;
  vision_preproc->srcpads = {};
  vision_preproc->medialib_vision_pre_proc = NULL;

  vision_preproc->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");

  gst_pad_set_chain_function(vision_preproc->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_vision_preproc_chain));
  gst_pad_set_query_function(vision_preproc->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_vision_preproc_sink_query));
  gst_pad_set_event_function(vision_preproc->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_vision_preproc_sink_event));

  GST_PAD_SET_PROXY_CAPS(vision_preproc->sinkpad);
  gst_element_add_pad(GST_ELEMENT(vision_preproc), vision_preproc->sinkpad);
}

static GstFlowReturn gst_hailo_vision_preproc_push_output_frames(GstHailoVisionPreProc *self, std::vector<hailo_media_library_buffer> &output_frames)
{
  guint output_frames_size = output_frames.size();
  if (output_frames_size < self->srcpads.size())
  {
    GST_ERROR_OBJECT(self, "Number of output frames (%d) is lower than the number of srcpads (%ld)", output_frames_size, self->srcpads.size());
    return GST_FLOW_ERROR;
  }
  else if (output_frames_size > self->srcpads.size())
  {
    GST_WARNING_OBJECT(self, "Number of output frames (%d) is higher than the number of srcpads (%ld)", output_frames_size, self->srcpads.size());
  }

  for(guint i = 0; i < self->srcpads.size(); i++)
  {
    if (output_frames[i].hailo_pix_buffer == nullptr)
    {
      GST_DEBUG_OBJECT(self, "Skipping output frame %d to match requested framerate", i);
      continue;
    }

    DspImagePropertiesPtr output_dsp_image_props = output_frames[i].hailo_pix_buffer;
    GstPad *srcpad = self->srcpads[i];
    // Get caps from srcpad
    GstCaps *caps = gst_pad_get_current_caps(srcpad);
    if (caps)
    {
      GstVideoInfo *video_info = gst_video_info_new();
      bool caps_status = gst_video_info_from_caps(video_info, caps);
      gst_caps_unref(caps);
      if (!caps_status)
      {
        GST_ERROR_OBJECT(self, "Failed to parse video info from caps for srcpad: %d", i);
        gst_video_info_free(video_info);
        return GST_FLOW_ERROR;
      }

      if(output_dsp_image_props->width != (guint)video_info->width || output_dsp_image_props->height != (guint)video_info->height)
      {
        GST_ERROR_OBJECT(self, "Output frame size (%ld, %ld) does not match srcpad size (%d, %d)", output_dsp_image_props->width, output_dsp_image_props->height, video_info->width, video_info->height);
        output_frames[i].decrease_ref_count();
        gst_video_info_free(video_info);
        return GST_FLOW_ERROR;
      }

      guint buffer_size = video_info->size;
      GST_DEBUG_OBJECT(self, "Creating GstBuffer from dsp buffer size: %d", buffer_size);
      GstBuffer *gst_outbuf = create_gst_buffer_from_hailo_buffer(output_frames[i], buffer_size);

      GST_DEBUG_OBJECT(self, "Pushing buffer to srcpad name %s", gst_pad_get_name(srcpad));
      gst_pad_push(srcpad, gst_outbuf);
      gst_video_info_free(video_info);
    }
    else
    {
      output_frames[i].decrease_ref_count();
    }
  }

  return GST_FLOW_OK;
}

static GstFlowReturn gst_hailo_vision_preproc_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
  GstHailoVisionPreProc *self = GST_HAILO_VISION_PREPROC(parent);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT(self, "Chain - Received buffer from sinkpad");

  // Get the vsm from the buffer
  GstHailoVsmMeta *meta = reinterpret_cast<GstHailoVsmMeta *>(gst_buffer_get_meta(buffer, g_type_from_name(HAILO_VSM_META_API_NAME)));

  if (meta == NULL)
  {
    GST_ERROR_OBJECT(self, "Cannot get VSM metadata from buffer");
    return GST_FLOW_ERROR;
  }

  uint index = meta->v4l2_index;
  hailo15_vsm vsm = meta->vsm;
  GST_DEBUG_OBJECT(self, "Got VSM metadata, index: %d vsm x: %d vsm y: %d", index, vsm.dx, vsm.dy);

  GstCaps *input_caps = gst_pad_get_current_caps(pad);

  GstVideoInfo *input_video_info = gst_video_info_new();
  gst_video_info_from_caps(input_video_info, input_caps);

  gst_caps_unref(input_caps);
  hailo_media_library_buffer hailo_buffer;

  // Map input and output buffers to GstVideoFrame
  GstVideoFrame input_video_frame;
  if (gst_video_frame_map(&input_video_frame, input_video_info, buffer, GST_MAP_READ))
  {
    GST_DEBUG_OBJECT(self, "Creating dsp buffer from video frame width: %d height: %d", input_video_info->width, input_video_info->height);
    if(!create_hailo_buffer_from_video_frame(&input_video_frame, hailo_buffer, vsm))
    {
      GST_ERROR_OBJECT(self, "Cannot create hailo buffer from video frame");
      ret = GST_FLOW_ERROR;
    }
  }
  else
  {
    GST_ERROR_OBJECT(self, "Cannot map input buffer to frame");
    ret = GST_FLOW_ERROR;
  }

  gst_video_frame_unmap(&input_video_frame);
  gst_video_info_free(input_video_info);
  if (ret == GST_FLOW_ERROR)
    return ret;

  std::vector<hailo_media_library_buffer> output_frames;
  GST_DEBUG_OBJECT(self, "Acquiring buffers from media library");

  GST_DEBUG_OBJECT(self, "Call media library handle frame - GstBuffer offset %ld", GST_BUFFER_OFFSET(buffer));
  media_library_return media_lib_ret = self->medialib_vision_pre_proc->handle_frame(hailo_buffer, output_frames);
  
  //TODO: move into handle_fame
  hailo_media_library_buffer_unref(&hailo_buffer);

  if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
  {
    GST_ERROR_OBJECT(self, "Media library handle frame failed on error %d", media_lib_ret);
    gst_buffer_unref(buffer);
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT(self, "Handle frame done");
  ret = gst_hailo_vision_preproc_push_output_frames(self, output_frames);
  gst_buffer_unref(buffer);

  return ret;
}

static GstCaps*
gst_hailo_create_caps_from_output_config(GstHailoVisionPreProc *self, output_resolution_t &output_res)
{
    GstCaps *caps;
    guint framerate = (guint)output_res.framerate;
    if (framerate == 0)
      framerate = 1;

    output_video_config_t &outp_config = self->medialib_vision_pre_proc->get_output_video_config();
    dsp_image_format_t &dsp_image_format = outp_config.format;
    std::string format = "";
    switch(dsp_image_format)
    {
      case DSP_IMAGE_FORMAT_RGB:
        format = "RGB";
        break;
      case DSP_IMAGE_FORMAT_GRAY8:
        format = "GRAY8";
        break;
      case DSP_IMAGE_FORMAT_NV12:
        format = "NV12";
        break;
      case DSP_IMAGE_FORMAT_A420:
        format = "A420";
        break;
      default:
        GST_ERROR_OBJECT(self, "Unsupported dsp image format %d", dsp_image_format);
        return NULL;
    }

    GST_DEBUG_OBJECT(self, "Creating caps - width = %ld height = %ld framerate = %d", output_res.dimensions.destination_width, output_res.dimensions.destination_height, output_res.framerate);
    caps = gst_caps_new_simple("video/x-raw",
                               "format", G_TYPE_STRING, format.c_str(),
                               "width", G_TYPE_INT, (guint)output_res.dimensions.destination_width,
                               "height", G_TYPE_INT, (guint)output_res.dimensions.destination_height,
                               "framerate", GST_TYPE_FRACTION, framerate, 1,
                                 NULL);

    return caps;
}

static gboolean
gst_hailo_set_srcpad_caps(GstHailoVisionPreProc *self, GstPad *srcpad, output_resolution_t &output_res)
{
    GstCaps *caps_result, *outcaps, *query_caps = NULL;
    gboolean ret = TRUE;

    query_caps = gst_hailo_create_caps_from_output_config(self, output_res);

    // Query the peer srcpad to obtain wanted resolution
    caps_result = gst_pad_peer_query_caps(srcpad, query_caps);

    // gst_caps_fixate takes ownership of caps_result, no need to unref it.
    outcaps = gst_caps_fixate(caps_result);

    // Check if outcaps intersects of query_caps
    GST_DEBUG_OBJECT(self, "Caps event - fixated peer srcpad caps %" GST_PTR_FORMAT, outcaps);

    if (gst_caps_is_empty(outcaps) || !gst_caps_is_fixed(outcaps))
    {
      GST_ERROR_OBJECT(self, "Caps event - set caps is not possible, Failed to match required caps with srcpad %s", gst_pad_get_name(srcpad));
      ret = FALSE;
    }
    else
    {
      // set the caps on the peer srcpad
      gboolean srcpad_set_caps_result = gst_pad_set_caps(srcpad, outcaps);
      if (!srcpad_set_caps_result)
      {
        GST_ERROR_OBJECT(self, "Failed to set caps on srcpad %s", gst_pad_get_name(srcpad));
        ret = FALSE;
      }
    }

    gst_caps_unref(query_caps);
    gst_caps_unref(outcaps);
    return ret;
}

static gboolean
gst_hailo_handle_caps_event(GstHailoVisionPreProc *self, GstCaps *caps)
{
    gboolean ret = TRUE;
    if (self->medialib_vision_pre_proc == nullptr)
    {
      GST_ERROR_OBJECT(self, "self->medialib_vision_pre_proc nullptr at time of caps query");
      return FALSE;
    }

    output_video_config_t &outp_config = self->medialib_vision_pre_proc->get_output_video_config();
    guint num_of_srcpads = self->srcpads.size();

    if (num_of_srcpads > outp_config.resolutions.size())
    {
      GST_ERROR_OBJECT(self, "Number of srcpads (%d) exceeds number of output resolutions (%ld)", num_of_srcpads, outp_config.resolutions.size());
      return FALSE;
    }

    for(guint i=0; i<num_of_srcpads; i++)
    {
      ret = gst_hailo_set_srcpad_caps(self, self->srcpads[i], outp_config.resolutions[i]);
      if (!ret)
        return FALSE;
    }

    return ret;
}

static gboolean 
gst_hailo_vision_preproc_sink_event(GstPad *pad,
                                    GstObject *parent, GstEvent *event)
{
  GstHailoVisionPreProc *self = GST_HAILO_VISION_PREPROC(parent);
  gboolean ret = FALSE;
  GST_DEBUG_OBJECT(self, "Received event from sinkpad");

  switch (GST_EVENT_TYPE(event))
  {
  case GST_EVENT_CAPS:
  {
    GST_DEBUG_OBJECT(self, "Received caps event from sinkpad");
    GstCaps *caps;
    gst_event_parse_caps(event, &caps);
    ret = gst_hailo_handle_caps_event(self, caps);
    gst_event_unref(event);
    break;
  }
  default:
  {
    /* just call the default handler */
    ret = gst_pad_event_default(pad, parent, event);
    break;
  }
  }
  return ret;
}

static gboolean 
intersect_peer_srcpad_caps(GstHailoVisionPreProc *self, GstPad *sinkpad, GstPad *srcpad, output_resolution_t &output_res)
{
    GstCaps *query_caps, *intersect_caps, *peercaps;
    gboolean ret = TRUE;

    query_caps = gst_hailo_create_caps_from_output_config(self, output_res);

    /* query the peer with the transformed filter */
    peercaps = gst_pad_peer_query_caps(srcpad, query_caps);
    GST_DEBUG_OBJECT(sinkpad, "peercaps %" GST_PTR_FORMAT, peercaps);

    /* intersect with the peer caps */
    intersect_caps = gst_caps_intersect(query_caps, peercaps);

    GST_DEBUG_OBJECT(sinkpad, "intersect_caps %" GST_PTR_FORMAT, intersect_caps);
    // validate intersect caps
    if (gst_caps_is_empty(intersect_caps))
    {
        GST_ERROR_OBJECT(self, "Failed to intersect caps - with srcpad %s and requested width %ld height %ld and framerate %d", gst_pad_get_name(srcpad), output_res.dimensions.destination_width, output_res.dimensions.destination_height, output_res.framerate);
        ret = FALSE;
    }

    if (peercaps)
      gst_caps_unref (peercaps);

    gst_caps_unref(intersect_caps);
    gst_caps_unref(query_caps);
    return ret;
}

static gboolean
gst_hailo_handle_caps_query(GstHailoVisionPreProc *self, GstPad *pad, GstQuery *query)
{
    // get pad name and direction
    const gchar *pad_name = gst_pad_get_name(pad);
    GstPadDirection pad_direction = gst_pad_get_direction(pad);

    GST_DEBUG_OBJECT(pad, "Received caps query from sinkpad name %s direction %d", pad_name, pad_direction);
    GstCaps *caps_result, *allowed_caps, *qcaps;
    /* we should report the supported caps here which are all */
    allowed_caps = gst_pad_get_pad_template_caps(pad);

    gst_query_parse_caps(query, &qcaps);
    if (qcaps && allowed_caps && !gst_caps_is_any(allowed_caps))
    {
        GST_DEBUG_OBJECT(pad, "qcaps %" GST_PTR_FORMAT, qcaps);
        // caps query - intersect template caps (allowed caps) with incomming caps query
        caps_result = gst_caps_intersect(allowed_caps, qcaps);
    }
    else
    {
        // no caps query - return template caps
        caps_result = allowed_caps;
    }

    GST_DEBUG_OBJECT (pad, "allowed template  %" GST_PTR_FORMAT, caps_result);
    if (self->medialib_vision_pre_proc == nullptr)
    {
      GST_ERROR_OBJECT(pad, "self->medialib_vision_pre_proc nullptr at time of caps query");
      return FALSE;
    }
    output_video_config_t &outp_config = self->medialib_vision_pre_proc->get_output_video_config();
    for(guint i = 0; i < self->srcpads.size(); i++)
    {
        if (!intersect_peer_srcpad_caps(self, pad, self->srcpads[i], outp_config.resolutions[i]))
        {
          gst_caps_unref(caps_result);
          return FALSE;
        }

    }

    // set the caps result
    gst_query_set_caps_result(query, caps_result);
    gst_caps_unref(caps_result);

    return TRUE;
}


static gboolean
gst_hailo_vision_preproc_sink_query(GstPad *pad,
                                 GstObject *parent, GstQuery *query)
{
  GstHailoVisionPreProc *self = GST_HAILO_VISION_PREPROC(parent);
  GST_DEBUG_OBJECT(self, "Received query from sinkpad");
  gboolean ret;

  switch (GST_QUERY_TYPE(query))
  {
  case GST_QUERY_ALLOCATION:
  {
    GST_DEBUG_OBJECT(self, "Received allocation query from sinkpad");
    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
    ret = gst_pad_query_default(pad, parent, query);
    break;
  }
  case GST_QUERY_CAPS:
  {
      ret = gst_hailo_handle_caps_query(self, pad, query);
      break;
  }
  case GST_QUERY_ACCEPT_CAPS:
  {
      GstCaps *caps;
      gst_query_parse_accept_caps(query, &caps);
      GST_DEBUG("accept caps %" GST_PTR_FORMAT, caps);
      gst_query_set_accept_caps_result(query, true);
      ret = TRUE;
      break;
  }
  default:
  {
      /* just call the default handler */
      ret = gst_pad_query_default(pad, parent, query);
      break;
  }
  }
  return ret;
}

static void gst_hailo_vision_preproc_dispose(GObject *object)
{
  GstHailoVisionPreProc *self = GST_HAILO_VISION_PREPROC(object);
  GST_DEBUG_OBJECT(self, "dispose");

  if (self->config_file_path)
  {
    g_free(self->config_file_path);
    self->config_file_path = NULL;
  }

  if (self->medialib_vision_pre_proc)
  {
    self->medialib_vision_pre_proc.reset();
    self->medialib_vision_pre_proc = NULL;
  }

  gst_hailo_vision_preproc_reset(self);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
gst_hailo_vision_preproc_release_srcpad(GstPad *pad, GstHailoVisionPreProc *self)
{
    if (pad != NULL)
    {
        GST_DEBUG_OBJECT(self, "Releasing srcpad %s", gst_pad_get_name(pad));
        gst_pad_set_active(pad, FALSE);
        gst_element_remove_pad(GST_ELEMENT_CAST(self), pad);
    }
}

static void
gst_hailo_vision_preproc_reset(GstHailoVisionPreProc *self)
{
    GST_DEBUG_OBJECT(self, "reset");
    if (self->sinkpad != NULL)
    {
        self->sinkpad = NULL;
    }

    for (GstPad *srcpad : self->srcpads)
    {
      gst_hailo_vision_preproc_release_srcpad(srcpad, self);
    }
    self->srcpads.clear();
}

std::string read_string_from_file(gchar *file_path)
{
    std::ifstream file_to_read;
    file_to_read.open(file_path);
    if (!file_to_read.is_open())
      throw std::runtime_error("config path is not valid");
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)), std::istreambuf_iterator<char>());
    file_to_read.close();
    return file_string;
}

void strip_pipeline_syntax(std::string &pipeline_input)
{
  if (pipeline_input.front() == '\'' && pipeline_input.back() == '\'') 
  {
    pipeline_input.erase(0, 1);
    pipeline_input.pop_back();
  }
}

static void gst_hailo_vision_preproc_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
  GstHailoVisionPreProc *self = GST_HAILO_VISION_PREPROC(object);

  switch (property_id)
  {
  // Handle property assignments here
  case PROP_CONFIG_FILE_PATH:
    {
      self->config_file_path = g_value_dup_string(value);
      GST_DEBUG_OBJECT(self, "config_file_path: %s", self->config_file_path);
      self->config_string = read_string_from_file(self->config_file_path);

      if (self->medialib_vision_pre_proc == nullptr)
      {
        gst_hailo_vision_preproc_create(self);
      } else {
        media_library_return config_status = self->medialib_vision_pre_proc->configure(self->config_string);
        if (config_status != MEDIA_LIBRARY_SUCCESS)
          GST_ERROR_OBJECT(self, "configuration error: %d", config_status);
      }
      break;
    }
  case PROP_CONFIG_STRING:
    {
        self->config_string = g_strdup(g_value_get_string(value));
        strip_pipeline_syntax(self->config_string);

        if (self->medialib_vision_pre_proc == nullptr)
        {
          gst_hailo_vision_preproc_create(self);
        } else {
          media_library_return config_status = self->medialib_vision_pre_proc->configure(self->config_string);
          if (config_status != MEDIA_LIBRARY_SUCCESS)
              GST_ERROR_OBJECT(self, "configuration error: %d", config_status);
        }
        break;
    }
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

static void
gst_hailo_vision_preproc_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
  GstHailoVisionPreProc *self = GST_HAILO_VISION_PREPROC(object);

  switch (property_id)
  {
  // Handle property retrievals here
  case PROP_CONFIG_FILE_PATH:
  {
    g_value_set_string(value, self->config_file_path);
    break;
  }
  case PROP_CONFIG_STRING:
  {
    g_value_set_string(value, self->config_string.c_str());
    break;
  }
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

static GstPad *
gst_hailo_vision_preproc_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name, const GstCaps *caps)
{
  GstPad *srcpad;
  GstHailoVisionPreProc *self = GST_HAILO_VISION_PREPROC(element);
  GST_DEBUG_OBJECT(self, "Request new pad name: %s", name);

  GST_OBJECT_LOCK(self);
  srcpad = gst_pad_new_from_template(templ, name);
  GST_OBJECT_UNLOCK(self);

  gst_pad_set_active(srcpad, TRUE);
  gst_element_add_pad(GST_ELEMENT(self), srcpad);
  self->srcpads.emplace_back(srcpad);

  return srcpad;
}

static gboolean
gst_hailo_vision_preproc_create(GstHailoVisionPreProc *self)
{
      tl::expected<MediaLibraryVisionPreProcPtr, media_library_return> vision_preproc = MediaLibraryVisionPreProc::create(self->config_string);
      if (vision_preproc.has_value())
      {
          self->medialib_vision_pre_proc = vision_preproc.value();
      }
      else
      {
          GST_ERROR_OBJECT(self, "Vision Pre-Proc configuration error: %d", vision_preproc.error());
          throw std::runtime_error("Vision Pre-Proc failed to configure, check config file.");
      }
      return TRUE;
}

static void
gst_hailo_vision_preproc_release_pad(GstElement *element, GstPad *pad)
{
  GstHailoVisionPreProc *self = GST_HAILO_VISION_PREPROC(element);
  gchar *name = gst_pad_get_name(pad);
  GST_DEBUG_OBJECT(self, "Release pad: %s", name);
  gst_element_remove_pad(element, pad);
}

static GstStateChangeReturn gst_hailo_vision_preproc_change_state(GstElement *element, GstStateChange transition)
{
  GstStateChangeReturn result = GST_STATE_CHANGE_SUCCESS;
  GstHailoVisionPreProc *self = GST_HAILO_VISION_PREPROC(element);
  result = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

  switch (transition)
  {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
      GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_READY_TO_PAUSED");
    }
    default:
      break;
  }

    return result;
}