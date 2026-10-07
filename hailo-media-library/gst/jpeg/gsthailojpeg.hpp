#pragma once
#ifndef _GST_HAILOJPEGENC_HPP_
#define _GST_HAILOJPEGENC_HPP_

#include <glib-object.h>
#include <glib.h>
#include <gst/video/gstvideoencoder.h>
#include <gst/video/video.h>
#include <stdint.h>
#include <sys/types.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "encoder_config_types.hpp"
#include "media_library/jpeg_encoder.hpp"

struct GstHailoJpegEnc;

G_BEGIN_DECLS

#define GST_TYPE_HAILOJPEGENC (gst_hailojpegenc_get_type())
#define GST_HAILOJPEGENC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILOJPEGENC, GstHailoJpegEnc))
#define GST_HAILOJPEGENC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILOJPEGENC, GstHailoJpegEncClass))
#define GST_IS_HAILOJPEGENC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILOJPEGENC))
#define GST_IS_HAILOJPEGENC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILOJPEGENC))

static constexpr uint DEFAULT_NUM_OF_THREADS = 1;
static constexpr uint64_t DEFAULT_MIN_FORCE_KEY_UNIT_INTERVAL = 0;
static constexpr int JPEG_DEFAULT_QUALITY = 85;
static constexpr int JPEG_DEFAULT_IDCT_METHOD = 1; // JDCT_FASTEST without dragging jpeglib.h here

typedef struct _GstHailoJpegEncParams
{
    std::string stream_id;
    encoder_config_t encoder_user_config;
    std::unique_ptr<JpegEncoder> jpeg_encoder;
    uint64_t jpegenc_min_force_key_unit_interval = DEFAULT_MIN_FORCE_KEY_UNIT_INTERVAL;
    int jpeg_quality = JPEG_DEFAULT_QUALITY;
    int jpeg_idct_method = JPEG_DEFAULT_IDCT_METHOD;
    uint num_of_threads = DEFAULT_NUM_OF_THREADS;

    std::thread completion_thread;
    std::mutex pending_mtx;
    std::condition_variable pending_cv;
    std::queue<GstVideoCodecFrame *> pending_frames;
    std::atomic<bool> stop_completion{false};
} GstHailoJpegEncParams;

struct GstHailoJpegEnc
{
    GstVideoEncoder parent;
    GstHailoJpegEncParams *params = nullptr;
};

struct GstHailoJpegEncClass
{
    GstVideoEncoderClass parent;
};

GType gst_hailojpegenc_get_type(void);
G_END_DECLS

#define GST_TYPE_IDCT_METHOD (gst_idct_method_get_type())
GType gst_idct_method_get_type(void);

#endif /* _GST_HAILOJPEGENC_HPP_ */
