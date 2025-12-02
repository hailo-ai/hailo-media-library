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
#include "media_library/config_parser.hpp"
#include "media_library/logger_macros.hpp"
#include "media_library/media_library_logger.hpp"
#include "media_library/encoder.hpp"
#include "buffer_utils.hpp"
#include "encoder_internal.hpp"
#include "gsthailobuffermeta.hpp"
#include "gstmedialibcommon.hpp"
#include "media_library/media_library_types.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

#define ENCODER_QUEUE_NAME "encoder_q"
#define PRINT_FPS true

#define MODULE_NAME LoggerType::Api

tl::expected<std::shared_ptr<MediaLibraryEncoder::Impl>, media_library_return> MediaLibraryEncoder::Impl::create(
    std::string name)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    std::shared_ptr<MediaLibraryEncoder::Impl> encoder = std::make_shared<MediaLibraryEncoder::Impl>(status, name);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return encoder;
}

media_library_return MediaLibraryEncoder::Impl::init_buffer_pool(const InputParams &input_params)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Initializing encoder buffer pool");
    std::string name = "jpeg_encoder";
    uint frame_width = input_params.width;
    uint frame_height = input_params.height;
    size_t max_buffers = input_params.max_pool_size;

    m_buffer_pool = std::make_shared<MediaLibraryBufferPool>(frame_width, frame_height, HAILO_FORMAT_GRAY8, max_buffers,
                                                             HAILO_MEMORY_TYPE_DMABUF, name);
    if (m_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to initialize buffer pool");
        return media_library_return::MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }
    LOGGER__MODULE__INFO(MODULE_NAME, "Buffer pool initialized successfully with frame size {}x{}", frame_width,
                         frame_height);
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryEncoder::Impl::Impl(media_library_return &status, std::string name)
    : m_main_context(g_main_context_default()), m_encoder_type(EncoderType::None),
      m_config_parser(CONFIG_SCHEMA_ENCODER_AND_BLENDING)
{
    m_name = name;

    gst_init(nullptr, nullptr);

    m_main_loop = g_main_loop_new(m_main_context, FALSE);
    m_appsrc_state = APPSRC_STATE_UNINITIALIZED;
    m_current_fps = 0;
    m_buffer_pool = NULL;
    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryEncoder::Impl::~Impl()
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Cleaning encoder gst pipeline");
    stop();
}

tl::expected<MediaLibraryEncoderPtr, media_library_return> MediaLibraryEncoder::create(std::string name)
{
    auto impl_expected = Impl::create(name);
    if (impl_expected.has_value())
    {
        return std::make_shared<MediaLibraryEncoder>(impl_expected.value());
    }
    else
    {
        return tl::make_unexpected(impl_expected.error());
    }
}

MediaLibraryEncoder::MediaLibraryEncoder(std::shared_ptr<MediaLibraryEncoder::Impl> impl) : m_impl(impl)
{
}

media_library_return MediaLibraryEncoder::Impl::subscribe(AppWrapperCallback callback)
{
    std::unique_lock<std::shared_mutex> lock(m_callbacks_mutex);
    m_callbacks.push_back(callback);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryEncoder::Impl::unsubscribe()
{
    std::unique_lock<std::shared_mutex> lock(m_callbacks_mutex);
    m_callbacks.clear();
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryEncoder::Impl::start()
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Starting encoder");
    if (is_started())
    {
        return MEDIA_LIBRARY_SUCCESS;
    }
    if (!m_has_config)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "set_config() must be called before start()");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    if (m_bus_watch_id != 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Cannot add bus watch, pipeline is already have a bus watch");
        return MEDIA_LIBRARY_ERROR;
    }
    GstBusPtr bus = gst_element_get_bus(m_pipeline);
    m_bus_watch_id = gst_bus_add_watch(bus, (GstBusFunc)bus_call, this);

    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start encoder pipeline");
        return MEDIA_LIBRARY_ERROR;
    }

    m_main_loop_thread = std::make_shared<std::thread>([this]() { g_main_loop_run(m_main_loop); });

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryEncoder::Impl::load_blenders()
{
    GstElementPtr encoder_bin = glib_cpp::ptrs::get_bin_by_name(m_pipeline, m_name);
    if (encoder_bin == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get encoder bin");
        return MEDIA_LIBRARY_ERROR;
    }
    gpointer value = nullptr;
    g_object_get(encoder_bin, "osd-blender", &value, NULL);
    osd::Blender *osd_blender = static_cast<osd::Blender *>(value);
    m_osd_blender = osd_blender->shared_from_this();
    value = nullptr;
    g_object_get(encoder_bin, "privacy-mask-blender", &value, NULL);
    PrivacyMaskBlender *privacy_mask_blender = static_cast<PrivacyMaskBlender *>(value);
    m_privacy_mask_blender = privacy_mask_blender->shared_from_this();
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryEncoder::Impl::stop()
{
    if (!is_started())
    {
        return MEDIA_LIBRARY_SUCCESS;
    }

    gboolean ret = gst_element_send_event(m_pipeline, gst_event_new_eos());
    if (!ret)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to stop the encoder pipeline");
        return MEDIA_LIBRARY_ERROR;
    }

    // Wait for pipeline to stop
    auto start_time = std::chrono::steady_clock::now();
    std::chrono::seconds timeout(1);
    bool passed_timeout = false;
    while (is_started() && !passed_timeout)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        passed_timeout = (std::chrono::steady_clock::now() - start_time) >= timeout;
    };

    if (passed_timeout)
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Sending EOS did not stop pipeline, stopping manually");
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        g_main_loop_quit(m_main_loop);
    }

    if (0 != m_bus_watch_id)
    {
        g_source_remove(m_bus_watch_id);
        m_bus_watch_id = 0;
    }

    g_main_context_wakeup(m_main_context);
    if (m_main_loop_thread->joinable())
    {
        m_main_loop_thread->join();
    }
    return MEDIA_LIBRARY_SUCCESS;
}

std::string MediaLibraryEncoder::Impl::create_pipeline(const InputParams &input_params, EncoderType encoder_type)
{
    std::string caps2;
    std::string pipeline;
    std::string hailoencoderbin_setting;

    if (encoder_type == EncoderType::Hailo)
    {
        caps2 = std::string("video/x-h264,framerate=") + std::to_string(input_params.framerate) + "/1";
    }
    else
    {
        caps2 = std::string("image/jpeg,framerate=") + std::to_string(input_params.framerate) + "/1";
    }

    if (m_set_config_by_string)
    {
        hailoencoderbin_setting =
            "hailoencodebin name=" + m_name + " config-string='" + m_json_config_str + "' ! " + caps2 + " ! ";
    }
    else
    {
        hailoencoderbin_setting + "hailoencodebin name=" + m_name + " ! " + caps2 + " ! ";
    }
    std::string fpsdisplaysink_name = get_fpsdisplaysink_name();
    pipeline = std::string("appsrc do-timestamp=true format=time block=true is-live=true max-bytes=0 max-buffers=1 "
                           "name=encoder_src ! "
                           "queue name=") +
               ENCODER_QUEUE_NAME + " leaky=no max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! " +
               hailoencoderbin_setting +
               "queue leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! "
               "fpsdisplaysink fps-update-interval=2000 signal-fps-measurements=true name=" +
               fpsdisplaysink_name + " " +
               "text-overlay=false sync=false video-sink=\"appsink wait-on-eos=false max-buffers=1 qos=false "
               "name=encoder_sink\"";

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Pipeline: gst-launch-1.0 {}", pipeline);

    return pipeline;
}

std::string MediaLibraryEncoder::Impl::get_fpsdisplaysink_name() const
{
    return "fpsdisplaysink_sensor" + std::to_string(m_sensor_index) + "_" + m_name;
}

// /**
//  * Print the FPS of the pipeline
//  *
//  * @note Prints the FPS to the stdout.
//  */
void MediaLibraryEncoder::Impl::on_fps_measurement(GstElement *fpsdisplaysink, gdouble fps, gdouble droprate,
                                                   gdouble avgfps)
{
    if (PRINT_FPS)
    {
        auto name = glib_cpp::get_name(fpsdisplaysink);
        std::cout << name << ", DROP RATE: " << droprate << " FPS: " << fps << " AVG_FPS: " << avgfps << std::endl;
    }
}

/**
 * @brief print warning when queue is overrun for 10 times
 *
 * @param queue element that is overrun
 * @param user_data
 */
static void on_queue_overrun(GstElement *queue, gpointer)
{
    static uint8_t encoder_count = 0;
    auto queue_name = glib_cpp::get_name(queue);
    if (queue_name == ENCODER_QUEUE_NAME)
    {
        // print every 10th time
        if (encoder_count++ != 10)
        {
            return;
        }
        encoder_count = 0;
    }

    GST_DEBUG_OBJECT(queue, "queue overrun detected %s", queue_name.c_str());
}

/**
 * Set the Appsink callbacks
 *
 * @param[in] pipeline        The pipeline as a GstElement.
 * @return                   true if all elements were found and callbacks were set up successfully,
 *                           false if any element could not be found or there was an error.
 * @note Sets the new_sample callback, without callback user data (NULL).
 */
bool MediaLibraryEncoder::Impl::set_gst_callbacks(GstElementPtr &pipeline)
{
    GstAppSinkCallbacks appsink_callbacks = {};

    std::string fpsdisplaysink_name = get_fpsdisplaysink_name();
    GstElementPtr fpssink = glib_cpp::ptrs::get_bin_by_name(pipeline, fpsdisplaysink_name.c_str());
    if (fpssink == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Could not find gst element {}", fpsdisplaysink_name);
        return false;
    }

    const gchar *gst_element_name = "encoder_sink";
    GstElementPtr appsink = glib_cpp::ptrs::get_bin_by_name(pipeline, gst_element_name);
    if (appsink == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Could not find gst element {}", gst_element_name);
        return false;
    }

    gst_element_name = ENCODER_QUEUE_NAME;
    GstElementPtr encoder_queue = glib_cpp::ptrs::get_bin_by_name(pipeline, gst_element_name);
    if (encoder_queue == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Could not find gst element {}", gst_element_name);
        return false;
    }

    g_signal_connect(fpssink, "fps-measurements", G_CALLBACK(fps_measurement), this);
    appsink_callbacks.new_sample = this->new_sample;
    gst_app_sink_set_callbacks(GST_APP_SINK(appsink.get()), &appsink_callbacks, (void *)this, NULL);

    // Connect to the "overrun" signal of the queue
    g_signal_connect(encoder_queue, "overrun", G_CALLBACK(on_queue_overrun), NULL);
    return true;
}

gboolean MediaLibraryEncoder::Impl::on_bus_call(GstMessage *msg)
{
    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_EOS: {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        g_main_loop_quit(m_main_loop);
        break;
    }
    case GST_MESSAGE_ERROR: {
        glib_cpp::t_error_message err = glib_cpp::parse_error(msg);
        LOGGER__MODULE__ERROR(MODULE_NAME, "Received an error message from the pipeline: {}", err.message);
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Error debug info: {}", err.debug_info);
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        g_main_loop_quit(m_main_loop);
        break;
    }
    default:
        break;
    }
    return TRUE;
}

media_library_return MediaLibraryEncoder::Impl::force_keyframe()
{
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    GstElementPtr encoder_bin = glib_cpp::ptrs::get_bin_by_name(m_pipeline, m_name);
    if (encoder_bin == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Got null encoder bin element in get_config");
        return MEDIA_LIBRARY_ERROR;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Force Keyframe requested from Encoder API");
    GstEvent *event = gst_video_event_new_downstream_force_key_unit(GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE,
                                                                    GST_CLOCK_TIME_NONE, TRUE, 1);
    GstPadPtr sinkpad = gst_element_get_static_pad(encoder_bin, "sink");
    if (!gst_pad_send_event(sinkpad, event))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to send force key unit event to encoder");
        ret = MEDIA_LIBRARY_ERROR;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Force Keyframe sent to encoder");
    return ret;
}

media_library_return MediaLibraryEncoder::Impl::add_buffer(HailoMediaLibraryBufferPtr ptr)
{
    GstBufferPtr buffer = gst_buffer_from_hailo_buffer(ptr, m_appsrc_caps);
    if (!buffer)
    {
        return MEDIA_LIBRARY_ERROR;
    }
    GstFlowReturn ret = this->add_buffer_internal(buffer);
    if (ret != GST_FLOW_OK)
    {
        return MEDIA_LIBRARY_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

GstFlowReturn MediaLibraryEncoder::Impl::add_buffer_internal(GstBufferPtr &buffer)
{
    GstFlowReturn ret = glib_cpp::ptrs::push_buffer_to_app_src(m_appsrc, buffer);
    if (ret != GST_FLOW_OK)
    {
        GST_ERROR_OBJECT(m_appsrc.get(), "Failed to push buffer to appsrc");
        return ret;
    }
    return ret;
}

GstFlowReturn MediaLibraryEncoder::Impl::on_new_sample(GstAppSink *appsink)
{
    {
        std::shared_lock<std::shared_mutex> lock(m_callbacks_mutex);
        if (m_callbacks.empty())
        {
            return GST_FLOW_OK;
        }
    }
    GstSamplePtr sample;
    GstBufferPtr buffer;
    GstHailoBufferMeta *buffer_meta;
    HailoMediaLibraryBufferPtr buffer_ptr;
    uint32_t used_size;
    sample = gst_app_sink_pull_sample(appsink);
    if (!sample)
    {
        GST_ERROR("Failed to get sample from appsink May be EOS!");
        return GST_FLOW_OK;
    }
    buffer = glib_cpp::ptrs::get_buffer_from_sample(sample);
    if (!buffer)
    {
        GST_ERROR("Failed to get buffer from sample");
        return GST_FLOW_OK;
    }
    switch (m_encoder_type)
    {
    case EncoderType::Hailo: {
        buffer_meta = gst_buffer_get_hailo_buffer_meta(buffer);
        if (!buffer_meta)
        {
            GST_ERROR("Failed to get hailo buffer meta");
            return GST_FLOW_ERROR;
        }

        buffer_ptr = buffer_meta->buffer_ptr;
        used_size = buffer_meta->used_size;
        if (!buffer_ptr)
        {
            GST_ERROR("Failed to get hailo buffer ptr");
            return GST_FLOW_ERROR;
        }

        if (gst_buffer_is_writable(buffer.get()))
            gst_buffer_remove_meta(buffer, &buffer_meta->meta);
        break;
    }
    case EncoderType::Jpeg: {
        HailoMediaLibraryBufferPtr hailo_buffer = std::make_shared<hailo_media_library_buffer>();
        if (m_buffer_pool->acquire_buffer(hailo_buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to acquire buffer");
            return GST_FLOW_ERROR;
        }
        hailo_buffer->sync_start();
        size_t input_size;
        hailo_buffer_from_jpeg_gst_buffer(buffer, hailo_buffer, &input_size);
        hailo_buffer->sync_end();

        if (!hailo_buffer)
        {
            GST_ERROR("Failed to get hailo buffer ptr from jpeg");
            return GST_FLOW_ERROR;
        }
        used_size = input_size;
        buffer_ptr = hailo_buffer;
        break;
    }
    default:
        GST_ERROR("Invalid encoder type");
        return GST_FLOW_ERROR;
    }

    {
        std::shared_lock<std::shared_mutex> lock(m_callbacks_mutex);
        for (auto &callback : m_callbacks)
        {
            callback(buffer_ptr, used_size);
        }
    }

    return GST_FLOW_OK;
}

media_library_return MediaLibraryEncoder::subscribe(AppWrapperCallback callback)
{
    return m_impl->subscribe(callback);
}

media_library_return MediaLibraryEncoder::unsubscribe()
{
    return m_impl->unsubscribe();
}

media_library_return MediaLibraryEncoder::start()
{
    return m_impl->start();
}

media_library_return MediaLibraryEncoder::stop()
{
    return m_impl->stop();
}

media_library_return MediaLibraryEncoder::add_buffer(HailoMediaLibraryBufferPtr ptr)
{
    return m_impl->add_buffer(ptr);
}

std::shared_ptr<osd::Blender> MediaLibraryEncoder::Impl::get_osd_blender()
{
    return m_osd_blender;
}

std::shared_ptr<osd::Blender> MediaLibraryEncoder::get_osd_blender()
{
    return m_impl->get_osd_blender();
}

std::shared_ptr<PrivacyMaskBlender> MediaLibraryEncoder::Impl::get_privacy_mask_blender()
{
    return m_privacy_mask_blender;
}

std::shared_ptr<PrivacyMaskBlender> MediaLibraryEncoder::get_privacy_mask_blender()
{
    return m_impl->get_privacy_mask_blender();
}

// English code

bool MediaLibraryEncoder::Impl::init_pipeline(const encoder_config_t &config, const InputParams &input_params,
                                              EncoderType encoder_type)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Initializing encoder gst pipeline (struct)");
    GstElementPtr pipeline = gst_parse_launch(create_pipeline(input_params, encoder_type).c_str(), NULL);
    if (pipeline == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create pipeline");
        return false;
    }

    // Resolve appsrc
    GstElementPtr appsrc = glib_cpp::ptrs::get_bin_by_name(pipeline, "encoder_src");
    if (appsrc == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Could not find gst element encoder_src");
        return false;
    }

    // Callbacks (now fpsdisplaysink/appsink exist in the parsed pipeline)
    if (!set_gst_callbacks(pipeline))
    {
        return false;
    }

    // JPEG buffer pool parity
    if (encoder_type == EncoderType::Jpeg)
    {
        if (init_buffer_pool(input_params) != MEDIA_LIBRARY_SUCCESS)
        {
            return false;
        }
    }
    else
    {
        m_buffer_pool = nullptr;
    }

    // Set struct config BY VALUE on encoder bin (equivalent to JSON's config-string)
    if (!m_set_config_by_string)
    {

        GstElementPtr encoder_bin = glib_cpp::ptrs::get_bin_by_name(pipeline, m_name);
        if (encoder_bin == nullptr || !encoder_bin.as_g_object())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Could not find encoder bin {}", m_name);
            return false;
        }
        g_object_set(encoder_bin.as_g_object(), "user-config", config, nullptr);
    }
    m_appsrc = glib_cpp::ptrs::element_to_app_src(appsrc);
    m_pipeline = std::move(pipeline);

    load_blenders();
    return true;
}

bool MediaLibraryEncoder::Impl::is_started()
{
    if (m_main_loop == nullptr)
    {
        return false;
    }
    if (!g_main_loop_is_running(m_main_loop))
    {
        return false;
    }
    return true;
}

media_library_return MediaLibraryEncoder::Impl::set_config(const std::string &json_config_str)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Configuring encoder using json config");

    if (m_config_parser.validate_configuration(json_config_str) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Validation of encoder json config failed");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    encoder_config_t encoder_config{};
    if (m_config_parser.config_string_to_struct<encoder_config_t>(json_config_str, encoder_config) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to convert encoder JSON config to struct");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    m_set_config_by_string = true;
    const std::string old_json = m_json_config_str;
    m_json_config_str = json_config_str;

    const auto rc = set_config(encoder_config);
    if (rc != MEDIA_LIBRARY_SUCCESS)
    {
        m_json_config_str = old_json;
    }

    return rc;
}

media_library_return MediaLibraryEncoder::Impl::set_config(const encoder_config_t &config)
{
    LOGGER__MODULE__INFO(
        MODULE_NAME,
        "Configuring encoder using struct config (original behavior when pipeline exists; build on first use)");

    const EncoderType config_encoder_type = extract_encoder_type(config);
    const InputParams new_input_params = extract_input_params(config);

    if (!m_has_config)
    {
        if (!init_pipeline(config, new_input_params, config_encoder_type))
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init encoder pipeline (first-time)");
            return MEDIA_LIBRARY_ERROR;
        }
        m_encoder_type = config_encoder_type;
    }
    else
    {
        GstElementPtr encoder_bin = glib_cpp::ptrs::get_bin_by_name(m_pipeline, m_name);
        if (encoder_bin == nullptr)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Got null encoder bin element in set_config");
            return MEDIA_LIBRARY_ERROR;
        }

        g_object_set(encoder_bin, "user-config", config, NULL);
    }
    m_appsrc_caps =
        gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, new_input_params.format.c_str(), "width",
                            G_TYPE_INT, new_input_params.width, "height", G_TYPE_INT, new_input_params.height,
                            "framerate", GST_TYPE_FRACTION, new_input_params.framerate, 1, NULL);
    g_object_set(m_appsrc.as_g_object(), "caps", m_appsrc_caps.get(), NULL);

    m_input_params = new_input_params;
    m_current_config = config;
    m_has_config = true;
    m_set_config_by_string = false;
    return MEDIA_LIBRARY_SUCCESS;
}

encoder_config_t MediaLibraryEncoder::Impl::get_config()
{
    if (m_encoder_type == EncoderType::Jpeg)
    {
        // Getting actual config from jpeg encoder is not supported
        return jpeg_encoder_config_t{};
    }

    encoder_config_t config;
    GstElementPtr encoder_bin = glib_cpp::ptrs::get_bin_by_name(m_pipeline, m_name);
    if (encoder_bin == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Got null encoder bin element in get_config");
        return config;
    }
    gpointer value = nullptr;
    g_object_get(encoder_bin, "config", &value, NULL);
    config = *static_cast<encoder_config_t *>(value);
    return config;
}

encoder_config_t MediaLibraryEncoder::Impl::get_user_config()
{
    encoder_config_t config;
    GstElementPtr encoder_bin = glib_cpp::ptrs::get_bin_by_name(m_pipeline, m_name);
    if (encoder_bin == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Got null encoder bin element in get_user_config");
        return config;
    }
    gpointer value = nullptr;
    g_object_get(encoder_bin, "user-config", &value, NULL);
    config = *static_cast<encoder_config_t *>(value);
    return config;
}

EncoderType MediaLibraryEncoder::Impl::get_type()
{
    return m_encoder_type;
}

media_library_return MediaLibraryEncoder::set_config(encoder_config_t &config)
{
    return m_impl->set_config(config);
}

media_library_return MediaLibraryEncoder::set_config(const std::string &config)
{
    return m_impl->set_config(config);
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

encoder_monitors MediaLibraryEncoder::Impl::get_encoder_monitors()
{
    encoder_monitors monitors;
    GstElementPtr encoder_bin = glib_cpp::ptrs::get_bin_by_name(m_pipeline, m_name);
    if (encoder_bin == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Got null encoder bin element in get_encoder_monitors");
        return monitors;
    }
    gpointer value = nullptr;
    g_object_get(encoder_bin, "encoder-monitors", &value, NULL);
    monitors = *static_cast<encoder_monitors *>(value);
    return monitors;
}

encoder_monitors MediaLibraryEncoder::get_encoder_monitors()
{
    return m_impl->get_encoder_monitors();
}

void MediaLibraryEncoder::Impl::set_sensor_index(size_t sensor_index)
{
    m_sensor_index = sensor_index;
}

void MediaLibraryEncoder::set_sensor_index(size_t sensor_index)
{
    m_impl->set_sensor_index(sensor_index);
}

InputParams MediaLibraryEncoder::Impl::extract_input_params(const encoder_config_t &cfg)
{
    InputParams p{};
    std::visit(
        [&](auto &&v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, jpeg_encoder_config_t>)
            {
                p.format = v.input_stream.format;
                p.width = v.input_stream.width;
                p.height = v.input_stream.height;
                p.framerate = v.input_stream.framerate;
                p.max_pool_size = (v.input_stream.max_pool_size) ? v.input_stream.max_pool_size : DEFAULT_MAX_POOL_SIZE;
            }
            else if constexpr (std::is_same_v<T, hailo_encoder_config_t>)
            {
                p.format = v.input_stream.format;
                p.width = v.input_stream.width;
                p.height = v.input_stream.height;
                p.framerate = v.input_stream.framerate;
                p.max_pool_size = (v.input_stream.max_pool_size) ? v.input_stream.max_pool_size : DEFAULT_MAX_POOL_SIZE;
            }
            else
            {
                p.format.clear();
                p.width = 0;
                p.height = 0;
                p.framerate = 0;
                p.max_pool_size = 0;
            }
        },
        cfg);
    return p;
}

EncoderType MediaLibraryEncoder::Impl::extract_encoder_type(const encoder_config_t &cfg)
{
    // Pick a clear default if the variant doesn't match known types.
    EncoderType type = EncoderType::Hailo; // choose the default that fits your system best
    std::visit(
        [&](auto &&v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, jpeg_encoder_config_t>)
            {
                type = EncoderType::Jpeg;
            }
            else if constexpr (std::is_same_v<T, hailo_encoder_config_t>)
            {
                type = EncoderType::Hailo;
            }
        },
        cfg);
    return type;
}
