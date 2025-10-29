#include <nlohmann/json.hpp>
#include <tl/expected.hpp>
#include <unordered_map>

#include "media_library/frontend.hpp"
#include "glib.h"
#include "gst/gstelement.h"
#include "gst/gstpipeline.h"
#include "media_library/config_manager.hpp"
#include "media_library/logger_macros.hpp"
#include "media_library/media_library_logger.hpp"
#include "frontend_internal.hpp"
#include "gsthailobuffermeta.hpp"
#include "gstmedialibcommon.hpp"
#include "buffer_utils.hpp"
#include "media_library/media_library_types.hpp"
#include <nlohmann/json.hpp>

static constexpr std::chrono::milliseconds MAIN_LOOP_WAIT_DURATION{100};

#define OUTPUT_SINK_ID(idx) ("sink" + std::to_string(idx))
#define OUTPUT_FPS_SINK_ID(idx) ("fpsdisplaysink" + std::to_string(idx))
#define PRINT_FPS false

#define MODULE_NAME LoggerType::Api

MediaLibraryFrontend::MediaLibraryFrontend(std::shared_ptr<Impl> impl) : m_impl(impl)
{
}

tl::expected<MediaLibraryFrontendPtr, media_library_return> MediaLibraryFrontend::create()
{
    auto impl_expected = MediaLibraryFrontend::Impl::create();
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

media_library_return MediaLibraryFrontend::set_config(const std::string &json_config)
{
    return m_impl->set_config(json_config);
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

tl::expected<std::vector<frontend_output_stream_t>, media_library_return> MediaLibraryFrontend::get_outputs_streams()
{
    return m_impl->get_outputs_streams();
}

tl::expected<frontend_config_t, media_library_return> MediaLibraryFrontend::get_config()
{
    return m_impl->get_config();
}

media_library_return MediaLibraryFrontend::set_config(const frontend_config_t &config)
{
    return m_impl->set_config(config);
}

std::unordered_map<output_stream_id_t, float> MediaLibraryFrontend::get_output_streams_current_fps()
{
    return m_impl->get_output_streams_current_fps();
}

media_library_return MediaLibraryFrontend::set_freeze(bool freeze)
{
    return m_impl->set_freeze(freeze);
}

media_library_return MediaLibraryFrontend::Impl::add_buffer(HailoMediaLibraryBufferPtr ptr)
{
    GstBufferPtr buffer = gst_buffer_from_hailo_buffer(ptr, m_appsrc_caps);
    if (!buffer)
    {
        GST_ERROR_OBJECT(m_appsrc.get(), "Frontend add_buffer failed to get GstBuffer from HailoMediaLibraryBuffer");
        return MEDIA_LIBRARY_ERROR;
    }

    GstFlowReturn ret = glib_cpp::ptrs::push_buffer_to_app_src(m_appsrc, buffer);
    if (ret != GST_FLOW_OK)
    {
        GST_ERROR_OBJECT(m_appsrc.get(), "Failed to push buffer to appsrc");
        return MEDIA_LIBRARY_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

tl::expected<std::shared_ptr<MediaLibraryFrontend::Impl>, media_library_return> MediaLibraryFrontend::Impl::create()
{
    media_library_return status;
    std::shared_ptr<MediaLibraryFrontend::Impl> fe = std::make_shared<MediaLibraryFrontend::Impl>(status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }
    return fe;
}

MediaLibraryFrontend::Impl::Impl(media_library_return &status)
    : m_src_element(frontend_src_element_t::UNKNOWN), m_send_buffer_id(0), m_config_manager(CONFIG_SCHEMA_FRONTEND)
{
    gst_init(nullptr, nullptr);

    m_main_loop = g_main_loop_new(NULL, FALSE);

    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryFrontend::Impl::~Impl()
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Cleaning frontend gst pipeline");
    stop();
}

std::optional<std::vector<frontend_output_stream_t>> MediaLibraryFrontend::Impl::create_output_streams_from_config(
    const frontend_config_t &cfg)
{
    std::vector<frontend_output_stream_t> outs;
    const auto &res = cfg.multi_resize_config.application_input_streams_config.resolutions;
    outs.reserve(res.size());

    for (size_t i = 0; i < res.size(); ++i)
    {
        const auto &o = res[i];
        frontend_output_stream_t s{};
        s.id = o.stream_id.empty() ? OUTPUT_SINK_ID(i) : o.stream_id;
        s.width = static_cast<uint16_t>(o.dimensions.destination_width);
        s.height = static_cast<uint16_t>(o.dimensions.destination_height);
        s.target_fps = static_cast<uint16_t>(o.framerate);
        s.current_fps = 0;
        outs.push_back(std::move(s));
    }
    return outs;
}

tl::expected<GstElementPtr, media_library_return> MediaLibraryFrontend::Impl::get_frontend_element()
{
    GstElementPtr frontendbinsrc = glib_cpp::ptrs::get_bin_by_name(m_pipeline, "frontend");
    // get the element with type hailofrontend from the bin
    GstElementPtr frontend = glib_cpp::ptrs::get_bin_by_name(frontendbinsrc, "hailofrontendelement");
    if (frontend == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get frontend element");
        return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }

    return frontend;
}

tl::expected<frontend_config_t, media_library_return> MediaLibraryFrontend::Impl::get_config()
{
    auto frontendbinsrc = glib_cpp::ptrs::get_bin_by_name(m_pipeline, "frontend");
    if (frontendbinsrc == nullptr)
    {
        return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }

    frontend_element_config_t frontend_element_config;
    gpointer value = nullptr;
    g_object_get(frontendbinsrc, "config", &value, NULL);
    frontend_element_config = *reinterpret_cast<frontend_element_config_t *>(value);

    hdr_config_t hdr_config;
    hailort_t hailort_config;
    input_video_config_t input_config;
    isp_t isp_config;
    application_analytics_config_t application_analytics_config;

    if (ConfigManager::get_input_stream_type(m_current_config) == frontend_src_element_t::V4L2SRC)
    {
        // For hailofrontendbinsrc, we can get these configs directly
        g_object_get(frontendbinsrc, "hdr-config", &value, NULL);
        hdr_config = *reinterpret_cast<hdr_config_t *>(value);

        g_object_get(frontendbinsrc, "hailort-config", &value, NULL);
        hailort_config = *reinterpret_cast<hailort_t *>(value);

        g_object_get(frontendbinsrc, "input-video-config", &value, NULL);
        input_config = *reinterpret_cast<input_video_config_t *>(value);

        g_object_get(frontendbinsrc, "isp-config", &value, NULL);
        isp_config = *reinterpret_cast<isp_t *>(value);

        application_analytics_config = m_current_config.application_analytics_config;
    }
    else
    {
        // hailofrontend (APPSRC), doesn't have these configs, so we grab the ones from the original JSON config
        frontend_config_t config;
        if (m_config_manager.config_string_to_struct<frontend_config_t>(m_json_config_str, config) ==
            MEDIA_LIBRARY_SUCCESS)
        {
            hdr_config = config.hdr_config;
            hailort_config = config.hailort_config;
            input_config = config.input_config;
            isp_config = config.isp_config;
            application_analytics_config = config.application_analytics_config;
        }
        else
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to parse config from JSON for APPSRC");
            return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
        }
    }

    frontend_config_t frontend_config;
    frontend_config.ldc_config = frontend_element_config.ldc_config;
    frontend_config.denoise_config = frontend_element_config.denoise_config;
    frontend_config.multi_resize_config = frontend_element_config.multi_resize_config;
    frontend_config.input_config = input_config;
    frontend_config.hdr_config = hdr_config;
    frontend_config.hailort_config = hailort_config;
    frontend_config.isp_config = isp_config;
    frontend_config.application_analytics_config = application_analytics_config;

    return frontend_config;
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
    if (is_started())
    {
        return MEDIA_LIBRARY_SUCCESS;
    }

    if (m_json_config_str.empty())
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

    LOGGER__MODULE__TRACE(MODULE_NAME, "Starting frontend gst pipeline");
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start pipeline");
        return MEDIA_LIBRARY_ERROR;
    }
    m_main_loop_thread = std::make_shared<std::thread>([this]() { g_main_loop_run(m_main_loop); });

    // Wait for the main loop to start before we proceed to avoid racing condition in case start() is called again
    // before g_main_loop is ready/running
    if (!wait_for_main_loop(MAIN_LOOP_WAIT_DURATION))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start main loop thread");
        return MEDIA_LIBRARY_ERROR;
    }

    GstElementPtr frontend = glib_cpp::ptrs::get_bin_by_name(m_pipeline, "frontend");
    if (frontend == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get frontend element");
        return MEDIA_LIBRARY_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryFrontend::Impl::stop()
{
    if (!is_started())
    {
        return MEDIA_LIBRARY_SUCCESS;
    }
    if (m_send_buffer_id != 0)
    {
        g_source_remove(m_send_buffer_id);
        m_send_buffer_id = 0;
    }
    gboolean ret = gst_element_send_event(m_pipeline, gst_event_new_eos());
    if (!ret)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to stop pipeline");
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

    if (m_main_loop_thread->joinable())
    {
        m_main_loop_thread->join();
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryFrontend::Impl::set_config(const std::string &json_config)
{
    if (!json_config.empty() && (json_config == m_json_config_str))
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Same config string as current, no need to reconfigure");
        return MEDIA_LIBRARY_SUCCESS;
    }

    if (m_config_manager.validate_configuration(json_config) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to validate given json config");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    frontend_config_t frontend_config{};
    if (m_config_manager.config_string_to_struct<frontend_config_t>(json_config, frontend_config) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to convert frontend JSON config to struct");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    const std::string old_json = m_json_config_str;
    m_json_config_str = json_config;

    media_library_return ret = set_config(frontend_config);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        m_json_config_str = old_json;
    }

    return ret;
}

media_library_return MediaLibraryFrontend::Impl::set_config(const frontend_config_t &config)
{
    if (m_pipeline == nullptr)
    {
        const auto src = ConfigManager::get_input_stream_type(config);
        const auto [w, h] = ConfigManager::get_input_resolution(config);
        auto outputs_rt = create_output_streams_from_config(config);
        if (!outputs_rt.has_value())
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;

        if (!init_pipeline(config, src, w, h, outputs_rt.value()))
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;

        // align internal state with string-path first-time init
        m_src_element = src;
        m_output_streams = std::move(outputs_rt.value());
        m_current_config = config;
        m_has_config = true;

        return MEDIA_LIBRARY_SUCCESS;
    }
    auto frontendbinsrc = glib_cpp::ptrs::get_bin_by_name(m_pipeline, "frontend");
    media_library_return ret = MEDIA_LIBRARY_ERROR;
    if (frontendbinsrc != nullptr)
    {
        m_current_config = config; // keep a stable address
        g_object_set(frontendbinsrc.as_g_object(), "config", &m_current_config, NULL);
        ret = MEDIA_LIBRARY_SUCCESS;
    }

    return ret;
}

bool MediaLibraryFrontend::Impl::is_started()
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

bool MediaLibraryFrontend::Impl::wait_for_main_loop(std::chrono::milliseconds timeout)
{
    if (m_main_loop == nullptr)
    {
        return false;
    }

    auto start_time = std::chrono::steady_clock::now();

    while (!g_main_loop_is_running(m_main_loop))
    {
        if (std::chrono::steady_clock::now() - start_time > timeout)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return true;
}

tl::expected<std::vector<frontend_output_stream_t>, media_library_return> MediaLibraryFrontend::Impl::
    get_outputs_streams()
{
    if (m_output_streams.empty())
    {
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }
    return m_output_streams;
}

bool MediaLibraryFrontend::Impl::init_pipeline(const frontend_config_t &config, frontend_src_element_t source_type,
                                               uint16_t input_width, uint16_t input_height,
                                               std::vector<frontend_output_stream_t> &output_streams)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Initializing frontend gst pipeline");

    GstElementPtr pipeline =
        gst_parse_launch(create_pipeline(config, source_type, input_width, input_height, output_streams).c_str(), NULL);

    if (pipeline == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create pipeline");
        return false;
    }

    GstElementPtr appsrc;
    GstCapsPtr appsrc_caps;
    if (source_type == frontend_src_element_t::APPSRC)
    {
        appsrc = glib_cpp::ptrs::get_bin_by_name(pipeline, "src");
        appsrc_caps =
            gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12", "width", G_TYPE_INT, input_width,
                                "height", G_TYPE_INT, input_height, "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
        g_object_set(appsrc.as_g_object(), "caps", appsrc_caps.get(), NULL);
    }

    if (!set_gst_callbacks(pipeline, source_type, output_streams))
    {
        return false;
    }

    if (appsrc != nullptr)
    {
        m_appsrc = glib_cpp::ptrs::element_to_app_src(appsrc);
        m_appsrc_caps = std::move(appsrc_caps);
    }

    m_pipeline = std::move(pipeline);
    return true;
}

std::string MediaLibraryFrontend::Impl::create_pipeline(const frontend_config_t &config,
                                                        frontend_src_element_t source_type, uint16_t input_width,
                                                        uint16_t input_height,
                                                        const std::vector<frontend_output_stream_t> &output_streams)
{
    std::string frontend_json_config = m_config_manager.config_struct_to_string<frontend_config_t>(config);
    std::ostringstream pipeline;

    switch (source_type)
    {
    case frontend_src_element_t::APPSRC:
        pipeline
            << "appsrc name=src do-timestamp=true format=buffers block=true is-live=true max-buffers=5 max-bytes=0 ! ";
        pipeline << "queue leaky=downstream max-size-buffers=1 max-size-time=0 max-size-bytes=0 ! ";
        pipeline << "video/x-raw,format=NV12,width=" << input_width << ",height=" << input_height
                 << ",framerate=30/1 ! ";
        pipeline << "hailofrontend name=frontend config-string='" << frontend_json_config << "' ";
        break;
    case frontend_src_element_t::V4L2SRC:
        pipeline << "hailofrontendbinsrc name=frontend config-string='" << frontend_json_config << "' ";
        break;
    default:
        LOGGER__MODULE__ERROR(MODULE_NAME, "Invalid src element {}", static_cast<int>(source_type));
        throw new std::runtime_error("frontend src element not supported");
    }

    for (const frontend_output_stream_t &s : output_streams)
    {
        pipeline << "frontend. ! ";
        pipeline << "queue leaky=no max-size-buffers=3 max-size-time=0 max-size-bytes=0 ! ";
        pipeline
            << "fpsdisplaysink fps-update-interval=2000 signal-fps-measurements=true name=fpsdisplay" << s.id
            << " text-overlay=false sync=false video-sink=\"appsink qos=false wait-on-eos=false max-buffers=1 name="
            << s.id << "\" ";
    }

    auto pipeline_str = pipeline.str();
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Pipeline: gst-launch-1.0 {}", pipeline_str);

    return pipeline_str;
}

// /**
//  * Print the FPS of the pipeline
//  *
//  * @note Prints the FPS to the stdout.
//  */
void MediaLibraryFrontend::Impl::fps_measurement(GstElement *fpsdisplaysink, gdouble fps, gdouble droprate,
                                                 gdouble avgfps, frontend_output_stream_t *output_stream)
{
    if (PRINT_FPS)
    {
        auto name = glib_cpp::get_name(fpsdisplaysink);
        std::cout << name << ", DROP RATE: " << droprate << " FPS: " << fps << " AVG_FPS: " << avgfps << std::endl;
    }
    output_stream->current_fps = static_cast<float>(fps);
}

bool MediaLibraryFrontend::Impl::set_gst_callbacks(GstElementPtr &pipeline, frontend_src_element_t source_type,
                                                   std::vector<frontend_output_stream_t> &output_streams)
{
    GstElementPtr appsrc;
    std::vector<GstElementPtr> fpssinks;

    if (source_type == frontend_src_element_t::APPSRC)
    {
        const gchar *gst_element_name = "src";
        appsrc = glib_cpp::ptrs::get_bin_by_name(pipeline, gst_element_name);
        if (appsrc == nullptr)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Could not find gst element {}", gst_element_name);
            return false;
        }
    }

    for (auto &output_stream : output_streams)
    {
        const std::string gst_element_name = std::string("fpsdisplay") + output_stream.id;
        GstElementPtr fpssink = glib_cpp::ptrs::get_bin_by_name(pipeline, gst_element_name);
        if (fpssink == nullptr)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Could not find gst element {}", gst_element_name);
            return false;
        }
        fpssinks.push_back(std::move(fpssink));
    }

    if (appsrc != nullptr)
    {
        GstAppSrcCallbacks appsrc_callbacks = {};
        gst_app_src_set_callbacks(GST_APP_SRC(appsrc.get()), &appsrc_callbacks, (void *)this, NULL);
    }

    GstAppSinkCallbacks appsink_callbacks = {};
    appsink_callbacks.new_sample = new_sample;
    for (size_t i = 0; i < output_streams.size(); ++i)
    {
        auto &output_stream = output_streams[i];
        auto fpssink = std::move(fpssinks[i]);

        LOGGER__MODULE__INFO(MODULE_NAME, "Setting callback for sink {}", output_stream.id);

        g_signal_connect(fpssink, "fps-measurements", G_CALLBACK(fps_measurement), &output_stream);

        GstElementPtr appsink = glib_cpp::ptrs::get_bin_by_name(pipeline, output_stream.id);
        gst_app_sink_set_callbacks(GST_APP_SINK(appsink.get()), &appsink_callbacks, (void *)this, NULL);
    }

    return true;
}

GstFlowReturn MediaLibraryFrontend::Impl::on_new_sample(output_stream_id_t id, GstAppSink *appsink)
{
    if (m_callbacks.empty())
    {
        return GST_FLOW_OK;
    }
    GstSamplePtr sample;
    GstBufferPtr buffer;
    sample = gst_app_sink_pull_sample(appsink);
    buffer = glib_cpp::ptrs::get_buffer_from_sample(sample);
    GstHailoBufferMeta *buffer_meta = gst_buffer_get_hailo_buffer_meta(buffer);
    if (!buffer_meta)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get hailo buffer meta");
        GST_ERROR("Failed to get hailo buffer meta");
        return GST_FLOW_ERROR;
    }

    HailoMediaLibraryBufferPtr buffer_ptr = buffer_meta->buffer_ptr;
    uint32_t used_size = buffer_meta->used_size;
    if (!buffer_ptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get hailo buffer ptr");
        GST_ERROR("Failed to get hailo buffer ptr");
        return GST_FLOW_ERROR;
    }

    auto cb_iter = m_callbacks.find(id);
    if (cb_iter == m_callbacks.end()) // id does not exist as a key
    {
        return GST_FLOW_OK;
    }
    for (auto cb : cb_iter->second)
    {
        cb(buffer_ptr, used_size);
    }

    return GST_FLOW_OK;
}

std::unordered_map<output_stream_id_t, float> MediaLibraryFrontend::Impl::get_output_streams_current_fps()
{
    std::unordered_map<output_stream_id_t, float> output_streams_fps;
    for (const frontend_output_stream_t &output : m_output_streams)
    {
        output_streams_fps[output.id] = output.current_fps;
    }
    return output_streams_fps;
}

media_library_return MediaLibraryFrontend::Impl::set_freeze(bool freeze)
{
    GstElementPtr frontend = glib_cpp::ptrs::get_bin_by_name(m_pipeline, "frontend");
    if (frontend == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get frontend element");
        return MEDIA_LIBRARY_ERROR;
    }
    g_object_set(frontend.as_g_object(), "freeze", freeze, NULL);
    return MEDIA_LIBRARY_SUCCESS;
}

gboolean MediaLibraryFrontend::Impl::on_bus_call(GstMessage *msg)
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
