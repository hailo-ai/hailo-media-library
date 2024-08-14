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
#include "media_library/media_library_logger.hpp"
#include "media_library/encoder.hpp"
#include "buffer_utils.hpp"
#include "encoder_internal.hpp"
#include "gsthailobuffermeta.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>

#define ENCODER_QUEUE_NAME "encoder_q"
#define PRINT_FPS true

tl::expected<std::shared_ptr<MediaLibraryEncoder::Impl>, media_library_return>
MediaLibraryEncoder::Impl::create(std::string json_config, std::string name)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    std::shared_ptr<MediaLibraryEncoder::Impl> encoder = std::make_shared<MediaLibraryEncoder::Impl>(json_config, status, name);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return encoder;
}

MediaLibraryEncoder::Impl::Impl(std::string json_config,
                                media_library_return &status,
                                std::string name)
    : m_json_config(json_config)
{
    m_name = name;
    nlohmann::json encoder_config = nlohmann::json::parse(m_json_config);
    nlohmann::json input_stream_config = encoder_config["encoding"]["input_stream"];

    // Validating json with json shcema is performed in media_library/src/hailo_encoder/encoder_config.cpp
    m_input_params.format = input_stream_config["format"];
    m_input_params.width = input_stream_config["width"];
    m_input_params.height = input_stream_config["height"];
    m_input_params.framerate = input_stream_config["framerate"];
    m_encoder_type = ConfigManager::get_encoder_type(encoder_config);
    gst_init(nullptr, nullptr);
    m_pipeline = gst_parse_launch(create_pipeline_string(encoder_config).c_str(), NULL);
    if (!m_pipeline)
    {
        LOGGER__ERROR("Failed to create pipeline");
        status = MEDIA_LIBRARY_ERROR;
        return;
    }
    gst_bus_add_watch(gst_element_get_bus(m_pipeline), (GstBusFunc)bus_call, this);
    m_main_loop = g_main_loop_new(NULL, FALSE);
    this->set_gst_callbacks();
    m_appsrc_state = APPSRC_STATE_UNINITIALIZED;
    m_current_fps = 0;

    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryEncoder::Impl::~Impl()
{
    gst_caps_unref(m_appsrc_caps);
    gst_object_unref(m_pipeline);
}

tl::expected<MediaLibraryEncoderPtr, media_library_return>
MediaLibraryEncoder::create(std::string json_config, std::string name)
{
    auto impl_expected = Impl::create(json_config, name);
    if (impl_expected.has_value())
    {
        return std::make_shared<MediaLibraryEncoder>(impl_expected.value());
    }
    else
    {
        return tl::make_unexpected(impl_expected.error());
    }
}

MediaLibraryEncoder::MediaLibraryEncoder(
    std::shared_ptr<MediaLibraryEncoder::Impl> impl)
    : m_impl(impl)
{
}

media_library_return MediaLibraryEncoder::Impl::subscribe(AppWrapperCallback callback)
{
    m_callbacks.push_back(callback);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryEncoder::Impl::start()
{
    GstStateChangeReturn ret =
        gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        LOGGER__ERROR("Failed to start encoder pipeline");
        return MEDIA_LIBRARY_ERROR;
    }
    m_main_loop_thread = std::make_shared<std::thread>(
        [this]()
        { g_main_loop_run(m_main_loop); });

    GstElement *encoder_bin = gst_bin_get_by_name(GST_BIN(m_pipeline), m_name.c_str());
    if (encoder_bin == nullptr)
    {
        LOGGER__ERROR("Failed to get encoder bin");
        return MEDIA_LIBRARY_ERROR;
    }
    gpointer value = nullptr;
    g_object_get(encoder_bin, "blender", &value, NULL);
    osd::Blender *blender = reinterpret_cast<osd::Blender *>(value);
    m_blender = blender->shared_from_this();
    gst_object_unref(encoder_bin);

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryEncoder::Impl::stop()
{
    gboolean ret = gst_element_send_event(m_pipeline, gst_event_new_eos());
    if (!ret)
    {
        LOGGER__ERROR("Failed to stop the encoder pipeline");
        return MEDIA_LIBRARY_ERROR;
    }

    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    g_main_loop_quit(m_main_loop);
    m_main_loop_thread->join();
    return MEDIA_LIBRARY_SUCCESS;
}

/**
 * Create the gstreamer pipeline as string
 *
 * @return A string containing the gstreamer pipeline.
 * @note prints the return value to the stdout.
 */
std::string MediaLibraryEncoder::Impl::create_pipeline_string(
    nlohmann::json encode_osd_json_config)
{
    std::string pipeline = "";
    auto json_osd_encoder_config = encode_osd_json_config.dump();

    std::ostringstream caps2;
    if (m_encoder_type == EncoderType::Hailo) {
        caps2 << "video/x-h264,framerate=" << m_input_params.framerate << "/1";
    } else {
        caps2 << "image/jpeg,framerate=" << m_input_params.framerate << "/1";
    }

    pipeline =
        "appsrc do-timestamp=true format=time block=true is-live=true max-bytes=0 "
        "max-buffers=1 name=encoder_src ! "
        "queue name=" +
        std::string(ENCODER_QUEUE_NAME) + " leaky=no max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! " +
        "hailoencodebin config-string=" +
        "'" + // Add ' in case the json string contains spaces
        std::string(json_osd_encoder_config) +
        "'" + // Close '
        " name=" + m_name.c_str() +
        " ! " + caps2.str() + " ! " +
        "queue leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! "
        "fpsdisplaysink fps-update-interval=2000 signal-fps-measurements=true name=fpsdisplaysink "
        "text-overlay=false sync=false video-sink=\"appsink wait-on-eos=false max-buffers=1 qos=false "
        "name=encoder_sink\"";

    LOGGER__INFO("Pipeline: gst-launch-1.0 {}", pipeline);

    return pipeline;
}

// /**
//  * Print the FPS of the pipeline
//  *
//  * @note Prints the FPS to the stdout.
//  */
void MediaLibraryEncoder::Impl::on_fps_measurement(GstElement *fpsdisplaysink,
                                                   gdouble fps,
                                                   gdouble droprate,
                                                   gdouble avgfps)
{
    gchar *name;
    g_object_get(G_OBJECT(fpsdisplaysink), "name", &name, NULL);
    if (PRINT_FPS)
        std::cout << name << ", DROP RATE: " << droprate << " FPS: " << fps << " AVG_FPS: " << avgfps << std::endl;
    g_free(name);
}

/**
 * @brief print warning when queue is overrun for 10 times
 *
 * @param queue element that is overrun
 * @param user_data
 */
static void on_queue_overrun(GstElement *queue, gpointer user_data)
{
    static uint8_t encoder_count = 0;
    const gchar *queue_name = gst_element_get_name(queue);
    if (strcmp(queue_name, ENCODER_QUEUE_NAME) == 0)
    {
        if (encoder_count++ != 10) // print every 10th time
            return;
        encoder_count = 0;
    }

    GST_DEBUG_OBJECT(queue, "queue overrun detected %s", queue_name);
}

/**
 * Set the Appsink callbacks
 *
 * @param[in] pipeline        The pipeline as a GstElement.
 * @note Sets the new_sample callback, without callback user data (NULL).
 */
void MediaLibraryEncoder::Impl::set_gst_callbacks()
{
    GstAppSinkCallbacks appsink_callbacks = {};

    GstElement *fpssink =
    gst_bin_get_by_name(GST_BIN(m_pipeline), "fpsdisplaysink");
        g_signal_connect(fpssink, "fps-measurements", G_CALLBACK(fps_measurement),
                         this);
    GstElement *appsink = gst_bin_get_by_name(GST_BIN(m_pipeline), "encoder_sink");
    appsink_callbacks.new_sample = this->new_sample;

    gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &appsink_callbacks,
                               (void *)this, NULL);
    gst_object_unref(appsink);
    gst_object_unref(fpssink);

    GstElement *appsrc = gst_bin_get_by_name(GST_BIN(m_pipeline), "encoder_src");
    m_appsrc = GST_APP_SRC(appsrc);
    m_appsrc_caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
                                        m_input_params.format.c_str(), "width",
                                        G_TYPE_INT, m_input_params.width, "height",
                                        G_TYPE_INT, m_input_params.height,
                                        "framerate", GST_TYPE_FRACTION,
                                        m_input_params.framerate, 1, NULL),
    g_object_set(G_OBJECT(m_appsrc), "caps", m_appsrc_caps, NULL);

    gst_object_unref(appsrc);

    GstElement *encoder_queue = gst_bin_get_by_name(GST_BIN(m_pipeline), ENCODER_QUEUE_NAME);

    // Connect to the "overrun" signal of the queue
    g_signal_connect(encoder_queue, "overrun", G_CALLBACK(on_queue_overrun), NULL);
    gst_object_unref(encoder_queue);
}

gboolean MediaLibraryEncoder::Impl::on_bus_call(GstBus *bus, GstMessage *msg)
{
    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_EOS:
    {
        //TODO: EOS never received
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        g_main_loop_quit(m_main_loop);
        m_main_loop_thread->join();
        break;
    }
    case GST_MESSAGE_ERROR:
    {
        gchar *debug;
        GError *err;

        gst_message_parse_error(msg, &err, &debug);
        LOGGER__ERROR("Received an error message from the pipeline: {}", err->message);
        g_error_free(err);
        LOGGER__DEBUG("Error debug info: {}", (debug) ? debug : "none");
        g_free(debug);
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        g_main_loop_quit(m_main_loop);
        break;
    }
    default:
        break;
    }
    return TRUE;
}

media_library_return 
MediaLibraryEncoder::Impl::force_keyframe()
{
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    GstElement *encoder_bin = gst_bin_get_by_name(GST_BIN(m_pipeline), m_name.c_str());
    if (encoder_bin == nullptr)
    {
        LOGGER__ERROR("Failed to get encoder bin from pipeline");
        std::cout << "Got null encoder bin element in get_config" << std::endl;
        gst_object_unref(encoder_bin);
        return MEDIA_LIBRARY_ERROR;
    }

    LOGGER__INFO("Force Keyframe requested from Encoder API");
    GstEvent *event = gst_video_event_new_downstream_force_key_unit(GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, TRUE, 1);
    GstPad *sinkpad = gst_element_get_static_pad(encoder_bin, "sink");
    if (!gst_pad_send_event(sinkpad, event))
    {
        LOGGER__ERROR("Failed to send force key unit event to encoder");
        ret = MEDIA_LIBRARY_ERROR;
    }

    gst_object_unref(sinkpad);
    gst_object_unref(encoder_bin);

    LOGGER__DEBUG("Force Keyframe sent to encoder");
    return ret;
}

media_library_return
MediaLibraryEncoder::Impl::add_buffer(HailoMediaLibraryBufferPtr ptr)
{
    GstBuffer *gst_buffer = gst_buffer_from_hailo_buffer(ptr, m_appsrc_caps);
    if (!gst_buffer)
    {
        return MEDIA_LIBRARY_ERROR;
    }
    GstFlowReturn ret = this->add_buffer_internal(gst_buffer);
    if (ret != GST_FLOW_OK)
    {
        return MEDIA_LIBRARY_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

GstFlowReturn MediaLibraryEncoder::Impl::add_gst_buffer(GstBuffer *buffer)
{
    return this->add_buffer_internal(buffer);
}

GstFlowReturn MediaLibraryEncoder::Impl::add_buffer_internal(GstBuffer *buffer)
{
    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buffer);
    if (ret != GST_FLOW_OK)
    {
        GST_ERROR_OBJECT(m_appsrc, "Failed to push buffer to appsrc");
        return ret;
    }
    return ret;
}

GstFlowReturn MediaLibraryEncoder::Impl::on_new_sample(GstAppSink *appsink)
{
    if (m_callbacks.empty())
    {
        return GST_FLOW_OK;
    }
    GstSample *sample;
    GstBuffer *buffer;
    GstHailoBufferMeta *buffer_meta;
    HailoMediaLibraryBufferPtr buffer_ptr;
    uint32_t used_size;
    sample = gst_app_sink_pull_sample(appsink);
    if (!sample)
    {
        GST_ERROR("Failed to get sample from appsink May be EOS!");
        return GST_FLOW_OK;
    }
    buffer = gst_sample_get_buffer(sample);
    if (!buffer)
    {
        GST_ERROR("Failed to get buffer from sample");
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
    switch (m_encoder_type)
    {
    case EncoderType::Hailo:
    {
        buffer_meta = gst_buffer_get_hailo_buffer_meta(buffer);
        if (!buffer_meta)
        {
            GST_ERROR("Failed to get hailo buffer meta");
            gst_sample_unref(sample);
            return GST_FLOW_ERROR;
        }

        buffer_ptr = buffer_meta->buffer_ptr;
        used_size = buffer_meta->used_size;
        if (!buffer_ptr)
        {
            GST_ERROR("Failed to get hailo buffer ptr");
            gst_sample_unref(sample);
            return GST_FLOW_ERROR;
        }

        if (gst_buffer_is_writable(buffer))
            gst_buffer_remove_meta(buffer, &buffer_meta->meta);
        break;

    } 
    case EncoderType::Jpeg:
    {
        buffer_ptr = hailo_buffer_from_jpeg_gst_buffer(buffer);
        buffer_ptr->pts = GST_BUFFER_PTS(buffer);
        if (!buffer_ptr)
        {
            GST_ERROR("Failed to get hailo buffer ptr from jpeg");
            gst_sample_unref(sample);
            return GST_FLOW_ERROR;
        }
        used_size = buffer_ptr->get_plane_size(0);
        break;
    }
    default:
        GST_ERROR("Invalid encoder type");
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    for (auto &callback : m_callbacks)
    {
        callback(buffer_ptr, used_size);
    }

    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

media_library_return MediaLibraryEncoder::subscribe(AppWrapperCallback callback)
{
    return m_impl->subscribe(callback);
}

media_library_return MediaLibraryEncoder::start() { return m_impl->start(); }

media_library_return MediaLibraryEncoder::stop() { return m_impl->stop(); }

media_library_return MediaLibraryEncoder::add_buffer(HailoMediaLibraryBufferPtr ptr)
{
    return m_impl->add_buffer(ptr);
}

std::shared_ptr<osd::Blender> MediaLibraryEncoder::Impl::get_blender()
{
    return m_blender;
}

std::shared_ptr<osd::Blender> MediaLibraryEncoder::get_blender()
{
    return m_impl->get_blender();
}

media_library_return MediaLibraryEncoder::Impl::configure(encoder_config_t &config)
{
    if (m_encoder_type == EncoderType::Jpeg) {
        std::cout << "configure function does not support jpeg encoders" << std::endl;
        return MEDIA_LIBRARY_ERROR;
    }

    hailo_encoder_config_t &hailo_config = std::get<hailo_encoder_config_t>(config);
    input_config_t config_input_stream = hailo_config.input_stream;

    m_input_params.format = config_input_stream.format;
    m_input_params.width = config_input_stream.width;
    m_input_params.height = config_input_stream.height;
    m_input_params.framerate = config_input_stream.framerate;
    m_appsrc_caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
                                        m_input_params.format.c_str(), "width",
                                        G_TYPE_INT, m_input_params.width, "height",
                                        G_TYPE_INT, m_input_params.height,
                                        "framerate", GST_TYPE_FRACTION,
                                        m_input_params.framerate, 1, NULL),
    g_object_set(G_OBJECT(m_appsrc), "caps", m_appsrc_caps, NULL);

    GstElement *encoder_bin = gst_bin_get_by_name(GST_BIN(m_pipeline), m_name.c_str());
    if (encoder_bin == nullptr)
    {
        std::cout << "Got null encoder bin element in get_config" << std::endl;
        return MEDIA_LIBRARY_ERROR;
    }

    g_object_set(encoder_bin, "user-config", config, NULL);
    gst_object_unref(encoder_bin);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryEncoder::set_force_videorate(bool force)
{
    return m_impl->set_force_videorate(force);
}

media_library_return MediaLibraryEncoder::Impl::set_force_videorate(bool force)
{
    GstElement *encoder_bin = gst_bin_get_by_name(GST_BIN(m_pipeline), m_name.c_str());
    if (encoder_bin == nullptr)
    {
        std::cout << "Got null encoder bin element in get_config" << std::endl;
        return MEDIA_LIBRARY_ERROR;
    }

    g_object_set(encoder_bin, "force-videorate", force, NULL);
    gst_object_unref(encoder_bin);
    return MEDIA_LIBRARY_SUCCESS;
}

encoder_config_t MediaLibraryEncoder::Impl::get_config()
{
    if (m_encoder_type == EncoderType::Jpeg) {
        // Getting actual config from jpeg encoder is not supported
        return jpeg_encoder_config_t{};
    }

    encoder_config_t config;
    GstElement *encoder_bin = gst_bin_get_by_name(GST_BIN(m_pipeline), m_name.c_str());
    if (encoder_bin == nullptr)
    {
        std::cout << "Got null encoder bin element in get_config" << std::endl;
        return config;
    }
    gpointer value = nullptr;
    g_object_get(encoder_bin, "config", &value, NULL);
    config = *reinterpret_cast<encoder_config_t *>(value);
    gst_object_unref(encoder_bin);
    return config;
}

encoder_config_t MediaLibraryEncoder::Impl::get_user_config()
{
    if (m_encoder_type == EncoderType::Jpeg) {
        // Getting actual config from jpeg encoder is not supported
        return jpeg_encoder_config_t{};
    }

    encoder_config_t config;
    GstElement *encoder_bin = gst_bin_get_by_name(GST_BIN(m_pipeline), m_name.c_str());
    if (encoder_bin == nullptr)
    {
        std::cout << "Got null encoder bin element in get_user_config" << std::endl;
        return config;
    }
    gpointer value = nullptr;
    g_object_get(encoder_bin, "user-config", &value, NULL);
    config = *reinterpret_cast<encoder_config_t *>(value);
    gst_object_unref(encoder_bin);
    return config;
}

EncoderType MediaLibraryEncoder::Impl::get_type()
{
    return m_encoder_type;
}

media_library_return MediaLibraryEncoder::configure(encoder_config_t &config)
{
    return m_impl->configure(config);
}

media_library_return MediaLibraryEncoder::force_keyframe()
{
    return m_impl->force_keyframe();
}

encoder_config_t MediaLibraryEncoder::get_config()
{
    return m_impl->get_config();
}

encoder_config_t MediaLibraryEncoder::get_user_config()
{
    return m_impl->get_user_config();
}

EncoderType MediaLibraryEncoder::get_type()
{
    return m_impl->get_type();
}

float MediaLibraryEncoder::Impl::get_current_fps()
{
    return m_current_fps;
}

float MediaLibraryEncoder::get_current_fps()
{
    return m_impl->get_current_fps();
}
