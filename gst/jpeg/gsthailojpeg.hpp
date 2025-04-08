#pragma once
#ifndef _GST_HAILOJPEGENC_HPP_
#define _GST_HAILOJPEGENC_HPP_

#include <gst/gst.h>
#include <vector>
#include <memory>
#include <jpeglib.h>
#include "media_library/encoder_config.hpp"

G_BEGIN_DECLS

#define GST_TYPE_HAILOJPEGENC (gst_hailojpegenc_get_type())
#define GST_HAILOJPEGENC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILOJPEGENC, GstHailoJpegEnc))
#define GST_HAILOJPEGENC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILOJPEGENC, GstHailoJpegEncClass))
#define GST_IS_HAILOJPEGENC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILOJPEGENC))
#define GST_IS_HAILOJPEGENC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILOJPEGENC))

static constexpr uint DEFAULT_NUM_OF_THREADS = 1;
static constexpr uint64_t DEFAULT_MIN_FORCE_KEY_UNIT_INTERVAL = 0;
static constexpr int JPEG_DEFAULT_QUALITY = 85;
static constexpr int JPEG_DEFAULT_IDCT_METHOD = JDCT_FASTEST;

typedef struct _GstHailoJpegEncParams
{
    std::string config;
    std::string config_path;
    std::unique_ptr<EncoderConfig> encoder_config;
    encoder_config_t encoder_user_config;
    uint num_of_threads = DEFAULT_NUM_OF_THREADS;
    GstPad *srcpad = nullptr;
    GstPad *sinkpad = nullptr;
    GstElement *m_roundrobin = nullptr;
    GstElement *m_hailoroundrobin = nullptr;
    std::vector<GstElement *> m_jpegencs;
    std::vector<GstElement *> m_queues;
    uint64_t jpegenc_min_force_key_unit_interval = DEFAULT_MIN_FORCE_KEY_UNIT_INTERVAL;
    int jpeg_quality = JPEG_DEFAULT_QUALITY;
    int jpeg_idct_method = JPEG_DEFAULT_IDCT_METHOD;
} GstHailoJpegEncParams;

struct GstHailoJpegEnc
{
    GstBin parent;
    GstHailoJpegEncParams *params = nullptr;
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
