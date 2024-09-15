#pragma once
#ifndef _GST_HAILOJPEGENC_HPP_
#define _GST_HAILOJPEGENC_HPP_

#include <gst/gst.h>
#include <vector>
#include <memory>
#include "media_library/encoder_config.hpp"

G_BEGIN_DECLS

#define GST_TYPE_HAILOJPEGENC (gst_hailojpegenc_get_type())
#define GST_HAILOJPEGENC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILOJPEGENC, GstHailoJpegEnc))
#define GST_HAILOJPEGENC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILOJPEGENC, GstHailoJpegEncClass))
#define GST_IS_HAILOJPEGENC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILOJPEGENC))
#define GST_IS_HAILOJPEGENC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILOJPEGENC))

struct GstHailoJpegEnc
{
    GstBin parent;
    std::string config;
    std::string config_path;
    std::unique_ptr<EncoderConfig> encoder_config;
    std::shared_ptr<encoder_config_t> encoder_user_config;
    uint num_of_threads;
    GstPad *srcpad;
    GstPad *sinkpad;
    GstElement *m_roundrobin;
    GstElement *m_hailoroundrobin;
    std::vector<GstElement *> m_jpegencs;
    std::vector<GstElement *> m_queues;
    guint64 jpegenc_min_force_key_unit_interval;
    gint jpeg_quality;
    gint jpeg_idct_method;
    gboolean use_gpdma;
};

struct GstHailoJpegEncClass
{
    GstBinClass parent;
};

GType gst_hailojpegenc_get_type(void);
G_END_DECLS

#define GST_TYPE_IDCT_METHOD (gst_idct_method_get_type())
GType gst_idct_method_get_type(void);

#endif /* _GST_HAILOJPEGENC_HPP_ */
