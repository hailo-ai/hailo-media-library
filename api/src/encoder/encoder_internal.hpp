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
#include "media_library/config_parser.hpp"
#include "media_library/encoder.hpp"
#include "gstmedialibcommon.hpp"
#include "media_library/privacy_mask.hpp"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <functional>
#include <iostream>
#include <queue>
#include <thread>
#include <vector>
#include <shared_mutex>
#include <nlohmann/json.hpp>

#define MIN_QUEUE_SIZE 5

struct InputParams
{
    std::string format;
    uint32_t width;
    uint32_t height;
    uint32_t framerate;
    uint32_t max_pool_size;
};
enum appsrc_state
{
    APPSRC_STATE_UNINITIALIZED = 0,
    APPSRC_STATE_NEED_DATA,
    APPSRC_STATE_ENOUGH_DATA
};
class MediaLibraryEncoder::Impl final
{
  private:
    GstAppSrcPtr m_appsrc;
    GstCapsPtr m_appsrc_caps;
    GMainContext *m_main_context = nullptr;
    GMainLoopPtr m_main_loop;
    GstElementPtr m_pipeline;
    guint m_bus_watch_id = 0;

    std::string m_name;
    InputParams m_input_params;
    std::vector<AppWrapperCallback> m_callbacks;
    std::shared_mutex m_callbacks_mutex;
    MediaLibraryBufferPoolPtr m_buffer_pool;
    std::shared_ptr<std::thread> m_main_loop_thread;
    std::string m_json_config_str;
    std::shared_ptr<osd::Blender> m_osd_blender;
    std::shared_ptr<PrivacyMaskBlender> m_privacy_mask_blender;
    appsrc_state m_appsrc_state;
    EncoderType m_encoder_type;
    float m_current_fps;
    ConfigParser m_config_parser;
    bool m_has_config = false;
    bool m_set_config_by_string = false;
    encoder_config_t m_current_config{};
    size_t m_sensor_index = 0;

    bool init_pipeline_string(const std::string &encoder_json_config, const InputParams &input_params,
                              EncoderType encoder_type);
    bool init_pipeline(const encoder_config_t &config, const InputParams &input_params, EncoderType encoder_type);
    bool is_started();

  public:
    static tl::expected<std::shared_ptr<MediaLibraryEncoder::Impl>, media_library_return> create(std::string name);

    ~Impl();
    Impl(media_library_return &status, std::string name);

  public:
    media_library_return subscribe(AppWrapperCallback callback);
    media_library_return unsubscribe();
    media_library_return start();
    media_library_return stop();
    media_library_return add_buffer(HailoMediaLibraryBufferPtr ptr);
    std::shared_ptr<osd::Blender> get_osd_blender();
    std::shared_ptr<PrivacyMaskBlender> get_privacy_mask_blender();
    media_library_return load_blenders();
    media_library_return set_config(const encoder_config_t &config);
    media_library_return set_config(const std::string &json_config);
    media_library_return set_force_videorate(bool force);
    encoder_config_t get_config();
    encoder_config_t get_user_config();
    EncoderType get_type();
    media_library_return force_keyframe();
    float get_current_fps();
    encoder_monitors get_encoder_monitors();
    void set_sensor_index(size_t sensor_index);
    static InputParams extract_input_params(const encoder_config_t &cfg);
    static EncoderType extract_encoder_type(const encoder_config_t &cfg);
    static constexpr uint32_t DEFAULT_MAX_POOL_SIZE = 5;
    /**
     * Below are public functions that are not part of the public API
     * but are public for GStreamer callbacks.
     */
  public:
    void on_fps_measurement(GstElement *fpssink, gdouble fps, gdouble droprate, gdouble avgfps);
    GstFlowReturn on_new_sample(GstAppSink *appsink);
    gboolean on_bus_call(GstMessage *msg);
    static void on_enc_pad_added(GstElement *src, GstPad *new_pad, gpointer user_data);

  private:
    static void fps_measurement(GstElement *fpssink, gdouble fps, gdouble droprate, gdouble avgfps, gpointer user_data)
    {
        MediaLibraryEncoder::Impl *encoder = static_cast<MediaLibraryEncoder::Impl *>(user_data);
        encoder->on_fps_measurement(fpssink, fps, droprate, avgfps);
        encoder->update_fps(fps);
    }
    static GstFlowReturn new_sample(GstAppSink *appsink, gpointer user_data)
    {
        MediaLibraryEncoder::Impl *encoder = static_cast<MediaLibraryEncoder::Impl *>(user_data);
        return encoder->on_new_sample(appsink);
    }
    static gboolean bus_call(GstBus *, GstMessage *msg, gpointer user_data)
    {
        MediaLibraryEncoder::Impl *encoder = static_cast<MediaLibraryEncoder::Impl *>(user_data);
        return encoder->on_bus_call(msg);
    }
    void update_fps(gdouble fps)
    {
        m_current_fps = static_cast<float>(fps);
    }

  private:
    media_library_return init_buffer_pool(const InputParams &input_params);
    bool set_gst_callbacks(GstElementPtr &pipeline);
    GstFlowReturn add_buffer_internal(GstBufferPtr &buffer);
    std::string create_pipeline_string(const std::string &encoder_json_config, const InputParams &input_params,
                                       EncoderType encoder_type);
    std::string create_pipeline(const InputParams &input_params, EncoderType encoder_type);
    std::string get_fpsdisplaysink_name() const;
};
