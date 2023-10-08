#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include "media_library/encoder_class.hpp"

G_BEGIN_DECLS

typedef struct _GstHailoEncoder GstHailoEncoder;
typedef struct _GstHailoEncoderClass GstHailoEncoderClass;
#define GST_TYPE_HAILO_ENCODER (gst_hailo_encoder_get_type())
#define GST_HAILO_ENCODER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_ENCODER, GstHailoEncoder))
#define GST_HAILO_ENCODER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_ENCODER, GstHailoEncoderClass))
#define GST_IS_HAILO_ENCODER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_ENCODER))
#define GST_IS_HAILO_ENCODER_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_ENCODER))
#define GST_HAILO_ENCODER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_HAILO_ENCODER, GstHailoEncoderClass))

struct _GstHailoEncoder
{
  GstVideoEncoder parent;
  GstVideoCodecState *input_state;
  std::unique_ptr<Encoder> encoder;
  std::string config;
  std::string config_path;
  gboolean stream_restart;
  GQueue *dts_queue;
};

struct _GstHailoEncoderClass
{
  GstVideoEncoderClass parent_class;
};

G_GNUC_INTERNAL GType gst_hailo_encoder_get_type(void);

G_END_DECLS
