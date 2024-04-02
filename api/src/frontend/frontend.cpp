#include "media_library/frontend.hpp"
#include "media_library/media_library_logger.hpp"
#include "frontend_internal.hpp"
#include "gsthailobuffermeta.hpp"
#include "buffer_utils.hpp"

#define OUTPUT_SINK_ID(idx) ("sink" + std::to_string(idx))
#define OUTPUT_FPS_SINK_ID(idx) ("fpsdisplaysink" + std::to_string(idx))
#define PRINT_FPS false

MediaLibraryFrontend::MediaLibraryFrontend(std::shared_ptr<Impl> impl) : m_impl(impl) {}

tl::expected<MediaLibraryFrontendPtr, media_library_return> MediaLibraryFrontend::create(frontend_src_element_t src_element, std::string json_config)
{
    auto impl_expected = MediaLibraryFrontend::Impl::create(src_element, json_config);
    if (!impl_expected.has_value())
    {
        return tl::make_unexpected(impl_expected.error());
    }
    std::shared_ptr<MediaLibraryFrontend::Impl> impl = impl_expected.value();
    return std::make_shared<MediaLibraryFrontend>(impl);
}

media_library_return MediaLibraryFrontend::start()
{
    return m_impl->start();
}

media_library_return MediaLibraryFrontend::stop()
{
    return m_impl->stop();
}

media_library_return MediaLibraryFrontend::configure(std::string json_config)
{
    return m_impl->configure(json_config);
}

media_library_return MediaLibraryFrontend::subscribe(FrontendCallbacksMap callbacks)
{
    return m_impl->subscribe(callbacks);
}

media_library_return MediaLibraryFrontend::add_buffer(HailoMediaLibraryBufferPtr ptr)
{
    return m_impl->add_buffer(ptr);
    return MEDIA_LIBRARY_ERROR;
}

media_library_return
MediaLibraryFrontend::Impl::add_buffer(HailoMediaLibraryBufferPtr ptr)
{
    GstBuffer *gst_buffer = gst_buffer_from_hailo_buffer(ptr, m_appsrc_caps);
    if (!gst_buffer)
    {
        GST_ERROR_OBJECT(m_appsrc, "Frontend add_buffer failed to get GstBuffer from HailoMediaLibraryBuffer");
        return MEDIA_LIBRARY_ERROR;
    }

    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), gst_buffer);
    if (ret != GST_FLOW_OK)
    {
        GST_ERROR_OBJECT(m_appsrc, "Failed to push buffer to appsrc");
        return MEDIA_LIBRARY_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

tl::expected<std::vector<frontend_output_stream_t>, media_library_return> MediaLibraryFrontend::get_outputs_streams()
{
    return m_impl->get_outputs_streams();
}

tl::expected<std::shared_ptr<MediaLibraryFrontend::Impl>, media_library_return> MediaLibraryFrontend::Impl::create(frontend_src_element_t src_element, std::string config)
{
    media_library_return status;
    std::shared_ptr<MediaLibraryFrontend::Impl> fe = std::make_shared<MediaLibraryFrontend::Impl>(src_element, config, status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }
    return fe;
}

MediaLibraryFrontend::Impl::Impl(frontend_src_element_t src_element, std::string config, media_library_return &status)
    : m_src_element(src_element), m_config_str(config), m_send_buffer_id(0)
{
    gst_init(nullptr, nullptr);

    m_json_config = nlohmann::json::parse(config);

    m_pipeline = gst_parse_launch(create_pipeline_string().c_str(), NULL);
    if (!m_pipeline)
    {
        LOGGER__ERROR("Failed to create pipeline");
        status = MEDIA_LIBRARY_ERROR;
        return;
    }

    if (m_src_element == FRONTEND_SRC_ELEMENT_APPSRC)
    {
        GstElement *appsrc = gst_bin_get_by_name(GST_BIN(m_pipeline), "src");
        m_appsrc = GST_APP_SRC(appsrc);
        m_appsrc_caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
                                            "NV12", "width", G_TYPE_INT, 3840, "height",
                                            G_TYPE_INT, 2160, "framerate", GST_TYPE_FRACTION,
                                            30, 1, NULL),
        g_object_set(G_OBJECT(m_appsrc), "caps", m_appsrc_caps, NULL);
    }

    m_main_loop = g_main_loop_new(NULL, FALSE);

    set_gst_callbacks();

    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryFrontend::Impl::~Impl()
{
    if (m_src_element == FRONTEND_SRC_ELEMENT_APPSRC)
        gst_caps_unref(m_appsrc_caps);
    gst_object_unref(m_pipeline);
}

media_library_return MediaLibraryFrontend::Impl::subscribe(FrontendCallbacksMap callback)
{
    for (auto const &cb : callback)
    {
        auto cb_iter = m_callbacks.find(cb.first);
        if (cb_iter == m_callbacks.end()) // id does not exist as a key
        {
            m_callbacks[cb.first] = std::vector<FrontendWrapperCallback>();
        }
        m_callbacks[cb.first].push_back(cb.second);
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryFrontend::Impl::start()
{
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        LOGGER__ERROR("Failed to start pipeline");
        return MEDIA_LIBRARY_ERROR;
    }
    m_main_loop_thread = std::make_shared<std::thread>(
        [this]()
        { g_main_loop_run(m_main_loop); });

    GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
    if (frontend == nullptr)
    {
        LOGGER__ERROR("Failed to get frontend element");
        return MEDIA_LIBRARY_ERROR;
    }

    // Get privacy mask blender from frontend bin
    gpointer val = nullptr;
    g_object_get(G_OBJECT(frontend), "privacy-mask", &val, NULL);
    PrivacyMaskBlender *value_ptr = reinterpret_cast<PrivacyMaskBlender *>(val);
    m_privacy_blender = value_ptr->shared_from_this();
    gst_object_unref(frontend);

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryFrontend::Impl::stop()
{
    if (m_send_buffer_id != 0)
    {
        g_source_remove(m_send_buffer_id);
        m_send_buffer_id = 0;
    }
    gboolean ret = gst_element_send_event(m_pipeline, gst_event_new_eos());
    if (!ret)
    {
        LOGGER__ERROR("Failed to stop pipeline");
        return MEDIA_LIBRARY_ERROR;
    }

    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    g_main_loop_quit(m_main_loop);
    m_main_loop_thread->join();
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryFrontend::Impl::configure(std::string json_config)
{
    m_json_config = nlohmann::json::parse(json_config);
    GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
    if (frontend == nullptr)
    {
        LOGGER__ERROR("Failed to get frontend element");
        return MEDIA_LIBRARY_ERROR;
    }
    g_object_set(G_OBJECT(frontend), "config-string", json_config.c_str(), NULL);
    gst_object_unref(frontend);
    return MEDIA_LIBRARY_SUCCESS;
}

tl::expected<std::vector<frontend_output_stream_t>, media_library_return> MediaLibraryFrontend::Impl::get_outputs_streams()
{
    if (!m_output_streams.empty())
    {
        return m_output_streams;
    }

    std::vector<frontend_output_stream_t> result;
    nlohmann::json outputs_config = m_json_config["output_video"]["resolutions"];
    for (size_t i = 0; i < outputs_config.size(); i++)
    {
        auto output_cfg = outputs_config[i];
        frontend_output_stream_t output;
        output.id = OUTPUT_SINK_ID(i);
        output.width = output_cfg["width"];
        output.height = output_cfg["height"];
        output.framerate = output_cfg["framerate"];
        result.push_back(output);
    }
    m_output_streams = result;
    return result;
}

std::string MediaLibraryFrontend::Impl::create_pipeline_string()
{
    auto outputs_expected = get_outputs_streams();
    if (!outputs_expected.has_value())
    {
        LOGGER__ERROR("Failed to get output streams ids");
        throw new std::runtime_error("Failed to get output streams ids");
    }
    std::ostringstream pipeline;

    switch (m_src_element)
    {
    case FRONTEND_SRC_ELEMENT_APPSRC:
        pipeline << "appsrc name=src do-timestamp=true format=buffers block=true is-live=true max-buffers=5 max-bytes=0 ! ";
        pipeline << "queue leaky=downstream max-size-buffers=1 max-size-time=0 max-size-bytes=0 ! ";
        pipeline << "video/x-raw,format=NV12,width=3840,height=2160,framerate=30/1 ! ";
        pipeline << "hailofrontend name=frontend config-string='" << std::string(m_json_config.dump()) << "' ";
        break;
    case FRONTEND_SRC_ELEMENT_V4L2SRC:
        pipeline << "hailofrontendbinsrc name=frontend config-string='" << std::string(m_json_config.dump()) << "' ";
        break;
    default:
        LOGGER__ERROR("Invalid src element {}", m_src_element);
        throw new std::runtime_error("frontend src element not supported");
    }

    for (frontend_output_stream_t s : outputs_expected.value())
    {
        pipeline << "frontend. ! ";
        pipeline << "queue leaky=no max-size-buffers=3 max-size-time=0 max-size-bytes=0 ! ";
        pipeline << "fpsdisplaysink signal-fps-measurements=true name=fpsdisplay" << s.id << " text-overlay=false sync=false video-sink=\"appsink qos=false wait-on-eos=false max-buffers=1 name=" << s.id << "\" ";
    }

    auto pipeline_str = pipeline.str();
    LOGGER__INFO("Pipeline: gst-launch-1.0 {}", pipeline_str);

    return pipeline_str;
}

// /**
//  * Print the FPS of the pipeline
//  *
//  * @note Prints the FPS to the stdout.
//  */
void MediaLibraryFrontend::Impl::on_fps_measurement(GstElement *fpsdisplaysink,
                                                    gdouble fps,
                                                    gdouble droprate,
                                                    gdouble avgfps)
{
    gchar *name;
    g_object_get(G_OBJECT(fpsdisplaysink), "name", &name, NULL);
    std::cout << name << ", DROP RATE: " << droprate << " FPS: " << fps << " AVG_FPS: " << avgfps << std::endl;
}

void MediaLibraryFrontend::Impl::set_gst_callbacks()
{
    if (m_src_element == FRONTEND_SRC_ELEMENT_APPSRC)
    {
        GstAppSrcCallbacks appsrc_callbacks = {NULL};
        GstElement *appsrc = gst_bin_get_by_name(GST_BIN(m_pipeline), "src");
        m_appsrc = GST_APP_SRC(appsrc);
        gst_app_src_set_callbacks(GST_APP_SRC(appsrc), &appsrc_callbacks, (void *)this, NULL);
        gst_object_unref(appsrc);
    }

    auto expected_streams = get_outputs_streams();
    if (!expected_streams.has_value())
    {
        LOGGER__ERROR("Failed to get output streams ids");
        return;
    }
    GstAppSinkCallbacks appsink_callbacks = {NULL};
    appsink_callbacks.new_sample = new_sample;
    for (auto s : expected_streams.value())
    {
        LOGGER__INFO("Setting callback for sink {}", s.id);
        if (PRINT_FPS)
        {
            GstElement *fpssink = gst_bin_get_by_name(GST_BIN(m_pipeline), (std::string("fpsdisplay") + s.id).c_str());
            g_signal_connect(fpssink, "fps-measurements", G_CALLBACK(fps_measurement), this);
        }
        GstElement *appsink = gst_bin_get_by_name(GST_BIN(m_pipeline), s.id.c_str());
        gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &appsink_callbacks, (void *)this, NULL);
        gst_object_unref(appsink);
    }
}

void MediaLibraryFrontend::Impl::on_need_data(GstAppSrc *appsrc, guint size)
{
    throw new std::runtime_error("FRONTEND_SRC_ELEMENT_APPSRC not supported yet");
}

void MediaLibraryFrontend::Impl::on_enough_data(GstAppSrc *appsrc)
{
    throw new std::runtime_error("FRONTEND_SRC_ELEMENT_APPSRC not supported yet");
}

GstFlowReturn MediaLibraryFrontend::Impl::on_new_sample(output_stream_id_t id, GstAppSink *appsink)
{
    if (m_callbacks.empty())
    {
        return GST_FLOW_OK;
    }
    GstSample *sample;
    GstBuffer *buffer;
    sample = gst_app_sink_pull_sample(appsink);
    buffer = gst_sample_get_buffer(sample);
    GstHailoBufferMeta *buffer_meta = gst_buffer_get_hailo_buffer_meta(buffer);
    if (!buffer_meta)
    {
        LOGGER__ERROR("Failed to get hailo buffer meta");
        GST_ERROR("Failed to get hailo buffer meta");
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    HailoMediaLibraryBufferPtr buffer_ptr = buffer_meta->buffer_ptr;
    uint32_t used_size = buffer_meta->used_size;
    if (!buffer_ptr)
    {
        LOGGER__ERROR("Failed to get hailo buffer ptr");
        GST_ERROR("Failed to get hailo buffer ptr");
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    auto cb_iter = m_callbacks.find(id);
    if (cb_iter == m_callbacks.end()) // id does not exist as a key
    {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
    for (auto cb : cb_iter->second)
    {
        buffer_ptr->increase_ref_count();
        cb(buffer_ptr, used_size);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

PrivacyMaskBlenderPtr MediaLibraryFrontend::Impl::get_privacy_mask_blender()
{
    return m_privacy_blender;
}

PrivacyMaskBlenderPtr MediaLibraryFrontend::get_privacy_mask_blender()
{
    return m_impl->get_privacy_mask_blender();
}