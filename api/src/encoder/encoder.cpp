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
#include "media_library/encoder.hpp"
#include "buffer_utils.hpp"
#include "encoder_internal.hpp"
#include "gsthailobuffermeta.hpp"
#include "osd_utils.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>

#define OSD_QUEUE_NAME "osd_q"
#define ENCODER_QUEUE_NAME "encoder_q"
#define PRINT_FPS true

tl::expected<std::shared_ptr<MediaLibraryEncoder::Impl>, media_library_return>
MediaLibraryEncoder::Impl::create(std::string json_config)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    std::shared_ptr<MediaLibraryEncoder::Impl> encoder = std::make_shared<MediaLibraryEncoder::Impl>(json_config, status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return encoder;
}

MediaLibraryEncoder::Impl::Impl(std::string json_config,
                                media_library_return &status)
    : m_json_config(json_config)
{

    nlohmann::json encoder_config = nlohmann::json::parse(m_json_config);
    // Validating json with json shcema is performed in media_library/src/hailo_encoder/encoder_config.cpp
    m_input_params.format =
        encoder_config["encoder"]["config"]["input_stream"]["format"];
    m_input_params.width =
        encoder_config["encoder"]["config"]["input_stream"]["width"];
    m_input_params.height =
        encoder_config["encoder"]["config"]["input_stream"]["height"];
    m_input_params.framerate =
        encoder_config["encoder"]["config"]["input_stream"]["framerate"];
    gst_init(nullptr, nullptr);
    m_mutex = std::make_shared<std::mutex>();
    m_condvar = std::make_unique<std::condition_variable>();
    m_queue_size = std::min((int)encoder_config["encoder"]["gop_config"]["gop_size"], MIN_QUEUE_SIZE);
    m_pipeline = gst_parse_launch(create_pipeline_string(encoder_config).c_str(), NULL);
    if (!m_pipeline)
    {
        std::cout << "Failed to create pipeline" << std::endl;
        status = MEDIA_LIBRARY_ERROR;
        return;
    }
    m_send_buffer_id = 0;
    gst_bus_add_watch(gst_element_get_bus(m_pipeline), (GstBusFunc)bus_call, this);
    m_main_loop = g_main_loop_new(NULL, FALSE);
    this->set_gst_callbacks(m_pipeline);
    m_appsrc_state = APPSRC_STATE_UNINITIALIZED;
    
    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryEncoder::Impl::~Impl()
{
    gst_caps_unref(m_appsrc_caps);
    gst_object_unref(m_pipeline);
}

tl::expected<MediaLibraryEncoderPtr, media_library_return>
MediaLibraryEncoder::create(std::string json_config)
{
    auto impl_expected = Impl::create(json_config);
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

MediaLibraryEncoder::~MediaLibraryEncoder() = default;

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
        std::cout << "Failed to start pipeline" << std::endl;
        return MEDIA_LIBRARY_ERROR;
    }
    m_main_loop_thread = std::make_shared<std::thread>(
        [this]()
        { g_main_loop_run(m_main_loop); });

    GstElement *osd = gst_bin_get_by_name(GST_BIN(m_pipeline), "osd");
    if (osd == nullptr)
    {
        std::cout << "got null hailoosd element" << std::endl;
        return MEDIA_LIBRARY_ERROR;
    }

    m_blender = gst_hailoosd_get_blender(osd);
    gst_object_unref(osd);

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryEncoder::Impl::stop()
{
    if (m_send_buffer_id != 0)
    {
        g_source_remove(m_send_buffer_id);
        m_send_buffer_id = 0;
    }
    gboolean ret = gst_element_send_event(m_pipeline, gst_event_new_eos());
    if (!ret)
    {
        std::cout << "Failed to send EOS event" << std::endl;
        return MEDIA_LIBRARY_ERROR;
    }
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
    std::ostringstream caps;
    auto json_encoder_config = encode_osd_json_config["encoder"].dump();
    auto json_osd_config = encode_osd_json_config["osd"].dump();
    caps << "video/x-raw,format=" << m_input_params.format
         << ",width=" << m_input_params.width;
    caps << ",height=" << m_input_params.height
         << ",framerate=" << m_input_params.framerate << "/1";

    std::ostringstream caps2;
    caps2 << "video/x-h264,framerate=" << m_input_params.framerate << "/1";

    pipeline =
        "appsrc do-timestamp=true format=buffers is-live=true max-bytes=0 "
        "max-buffers=1 name=encoder_src ! "
        "queue name=" +
        std::string(OSD_QUEUE_NAME) + " leaky=no max-size-buffers=5 "
                                      "max-size-bytes=0 max-size-time=0 ! " +
        caps.str() +
        " ! "
        "hailoosd config-str=" +
        "'" + // Add ' in case the json string contains spaces
        std::string(json_osd_config) +
        "'" + // Close '
        " name=osd wait-for-writable-buffer=true ! "
        "queue name=" +
        std::string(ENCODER_QUEUE_NAME) + " leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
                                          "hailoencoder config-str=" +
        std::string(json_encoder_config) +
        " name=enco ! " + caps2.str() + " ! "
                                                                       "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
                                                                       "fpsdisplaysink signal-fps-measurements=true name=fpsdisplaysink "
                                                                       "text-overlay=false sync=false video-sink=\"appsink "
                                                                       "name=encoder_sink\"";

    std::cout << "Pipeline:" << std::endl;
    std::cout << "gst-launch-1.0 " << pipeline << std::endl;

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
    std::cout << name << ", DROP RATE: " << droprate << " FPS: " << fps << " AVG_FPS: " << avgfps << std::endl;
}

/**
 * @brief print warning when queue is overrun for 10 times
 *
 * @param queue element that is overrun
 * @param user_data
 */
static void on_queue_overrun(GstElement *queue, gpointer user_data)
{
    static uint8_t osd_count = 0;
    static uint8_t encoder_count = 0;
    const gchar *queue_name = gst_element_get_name(queue);
    if (strcmp(queue_name, OSD_QUEUE_NAME) == 0)
    {
        if (osd_count++ != 10) // print every 10th time
            return;
        osd_count = 0;
    }
    else if (strcmp(queue_name, ENCODER_QUEUE_NAME) == 0)
    {
        if (encoder_count++ != 10) // print every 10th time
            return;
        encoder_count = 0;
    }

    std::cerr << "Warning: queue '" << queue_name << "' overrun detected!" << std::endl;
}

/**
 * Set the Appsink callbacks
 *
 * @param[in] pipeline        The pipeline as a GstElement.
 * @note Sets the new_sample callback, without callback user data (NULL).
 */
void MediaLibraryEncoder::Impl::set_gst_callbacks(GstElement *pipeline)
{
    GstAppSinkCallbacks appsink_callbacks = {NULL};
    GstAppSrcCallbacks appsrc_callbacks = {NULL};

    GstElement *fpssink =
        gst_bin_get_by_name(GST_BIN(pipeline), "fpsdisplaysink");
    if (PRINT_FPS)
    {
        g_signal_connect(fpssink, "fps-measurements", G_CALLBACK(fps_measurement),
                         this);
    }
    GstElement *appsink = gst_bin_get_by_name(GST_BIN(fpssink), "encoder_sink");
    appsink_callbacks.new_sample = this->new_sample;

    gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &appsink_callbacks,
                               (void *)this, NULL);
    gst_object_unref(appsink);

    GstElement *appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "encoder_src");
    m_appsrc = GST_APP_SRC(appsrc);
    m_appsrc_caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
                                        m_input_params.format.c_str(), "width",
                                        G_TYPE_INT, m_input_params.width, "height",
                                        G_TYPE_INT, m_input_params.height,
                                        "framerate", GST_TYPE_FRACTION,
                                        m_input_params.framerate, 1, NULL),
    g_object_set(G_OBJECT(m_appsrc), "caps", m_appsrc_caps, NULL);

    appsrc_callbacks.need_data = this->need_data;
    appsrc_callbacks.enough_data = this->enough_data;
    gst_app_src_set_callbacks(GST_APP_SRC(appsrc), &appsrc_callbacks,
                              (void *)this, NULL);

    GstElement *osd_queue = gst_bin_get_by_name(GST_BIN(pipeline), "osd_q");
    GstElement *encoder_queue = gst_bin_get_by_name(GST_BIN(pipeline), "encoder_q");

    // Connect to the "overrun" signal of the queue
    g_signal_connect(osd_queue, "overrun", G_CALLBACK(on_queue_overrun), NULL);
    g_signal_connect(encoder_queue, "overrun", G_CALLBACK(on_queue_overrun), NULL);
}

gboolean MediaLibraryEncoder::Impl::on_bus_call(GstBus *bus, GstMessage *msg)
{
    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_EOS:
    {
        g_main_loop_quit(m_main_loop);
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        break;
    }
    case GST_MESSAGE_ERROR:
    {
        gchar *debug;
        GError *err;

        gst_message_parse_error(msg, &err, &debug);
        g_printerr("Debugging info: %s\n", (debug) ? debug : "none");
        g_free(debug);

        g_print("Error: %s\n", err->message);
        g_error_free(err);
        std::cout << "Error Message Received, quitting main loop" << std::endl;
        g_main_loop_quit(m_main_loop);
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        break;
    }
    default:
        break;
    }
    return TRUE;
}

media_library_return
MediaLibraryEncoder::Impl::add_buffer(HailoMediaLibraryBufferPtr ptr)
{
    GstBuffer *gst_buffer = gst_buffer_from_hailo_buffer(ptr, m_appsrc_caps);
    if (!gst_buffer)
    {
        return MEDIA_LIBRARY_ERROR;
    }
    this->add_buffer_internal(gst_buffer);
    return MEDIA_LIBRARY_SUCCESS;
}

void MediaLibraryEncoder::Impl::add_gst_buffer(GstBuffer *buffer)
{
    this->add_buffer_internal(buffer);
}

void MediaLibraryEncoder::Impl::add_buffer_internal(GstBuffer *buffer)
{
    std::unique_lock<std::mutex> lock(*m_mutex);
    m_condvar->wait(lock, [this]
                    { return m_queue.size() < m_queue_size; });
    m_queue.push(buffer);
    if ((m_appsrc_state == APPSRC_STATE_NEED_DATA) && (m_send_buffer_id == 0))
    {
        this->m_send_buffer_id = g_idle_add((GSourceFunc)idle_callback, this);
    }
}

gboolean MediaLibraryEncoder::Impl::on_idle_callback()
{
    return this->send_buffer();
}

gboolean MediaLibraryEncoder::Impl::push_buffer(GstBuffer *buffer)
{
    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buffer);
    return ret == GST_FLOW_OK;
}

gboolean MediaLibraryEncoder::Impl::send_buffer()
{
    gboolean ret = TRUE;
    if (!m_queue.empty())
    {
        GstBuffer *buffer = this->dequeue_buffer();
        if (!buffer)
        {
            std::cout << "Error dequeuing buffer" << std::endl;
            m_send_buffer_id = 0;
            return FALSE;
        }
        ret = this->push_buffer(buffer);
        if (!ret)
        {
            std::cout << "Error pushing buffer" << std::endl;
            m_send_buffer_id = 0;
        }
    }
    else
    {
        if (m_send_buffer_id != 0)
        {
            g_source_remove(m_send_buffer_id);
            m_send_buffer_id = 0;
        }
    }
    return ret;
}

GstBuffer *MediaLibraryEncoder::Impl::dequeue_buffer()
{
    std::unique_lock<std::mutex> lock(*m_mutex);
    GstBuffer *buffer = m_queue.front();
    m_queue.pop();
    m_condvar->notify_one();
    return buffer;
}

void MediaLibraryEncoder::Impl::on_need_data(GstAppSrc *appsrc, guint size)
{
    m_appsrc_state = APPSRC_STATE_NEED_DATA;
    if (m_send_buffer_id == 0)
    {
        this->m_send_buffer_id = g_idle_add((GSourceFunc)idle_callback, this);
    }
}

void MediaLibraryEncoder::Impl::on_enough_data(GstAppSrc *appsrc)
{
    m_appsrc_state = APPSRC_STATE_ENOUGH_DATA;
    if (m_send_buffer_id != 0)
    {
        g_source_remove(m_send_buffer_id);
        m_send_buffer_id = 0;
    }
}

GstFlowReturn MediaLibraryEncoder::Impl::on_new_sample(GstAppSink *appsink)
{
    GstSample *sample;
    GstBuffer *buffer;
    sample = gst_app_sink_pull_sample(appsink);
    buffer = gst_sample_get_buffer(sample);
    GstHailoBufferMeta *buffer_meta = gst_buffer_get_hailo_buffer_meta(buffer);
    if (!buffer_meta)
    {
        GST_ERROR("Failed to get hailo buffer meta");
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    HailoMediaLibraryBufferPtr buffer_ptr = buffer_meta->buffer_ptr;
    uint32_t used_size = buffer_meta->used_size;
    if (!buffer_ptr)
    {
        GST_ERROR("Failed to get hailo buffer ptr");
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    if (gst_buffer_is_writable(buffer))
        gst_buffer_remove_meta(buffer, &buffer_meta->meta);

    for (auto &callback : m_callbacks)
    {
        buffer_ptr->increase_ref_count();
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
