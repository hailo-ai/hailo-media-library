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

PrivacyMaskBlenderPtr MediaLibraryFrontend::get_privacy_mask_blender()
{
    return m_impl->get_privacy_mask_blender();
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
    deinit_pipeline();

    if (m_main_loop != nullptr)
    {
        g_main_loop_unref(m_main_loop);
        m_main_loop = nullptr;
    }
}

frontend_src_element_t MediaLibraryFrontend::Impl::get_input_stream_type(const std::string &validated_json_config)
{
    if (validated_json_config.empty())
    {
        return frontend_src_element_t::UNKNOWN;
    }
    nlohmann::json::json_pointer src_type_ptr("/input_video/source_type");
    nlohmann::json tmp_config_json = nlohmann::json::parse(validated_json_config, nullptr, false);

    std::string source_type_str = tmp_config_json.value(src_type_ptr, DEFAULT_INPUT_STREAM_TYPE);
    if (source_type_str == "V4L2SRC")
    {
        return frontend_src_element_t::V4L2SRC;
    }
    else if (source_type_str == "APPSRC")
    {
        return frontend_src_element_t::APPSRC;
    }

    return frontend_src_element_t::UNKNOWN;
}

std::pair<uint16_t, uint16_t> MediaLibraryFrontend::Impl::get_input_resolution(const std::string &validated_json_config)
{
    if (validated_json_config.empty())
    {
        return {0, 0};
    }
    nlohmann::json::json_pointer resolution_ptr("/input_video/resolution");
    nlohmann::json tmp_config_json = nlohmann::json::parse(validated_json_config, nullptr, false);

    if (!tmp_config_json.contains(resolution_ptr))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to find resolution info in json config");
        return {0, 0};
    }

    return {tmp_config_json.at(resolution_ptr)["width"], tmp_config_json.at(resolution_ptr)["height"]};
}

nlohmann::json MediaLibraryFrontend::Impl::get_output_streams_json(const std::string &validated_json_config)
{
    if (validated_json_config.empty())
    {
        return {};
    }
    nlohmann::json::json_pointer resolutions_ptr("/application_input_streams/resolutions");
    nlohmann::json tmp_config_json = nlohmann::json::parse(validated_json_config, nullptr, false);

    if (!tmp_config_json.contains(resolutions_ptr))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to find outputs info in json config");
        return {};
    }

    return tmp_config_json.at(resolutions_ptr);
}

std::optional<std::vector<frontend_output_stream_t>> MediaLibraryFrontend::Impl::create_output_streams(
    const nlohmann::json &output_streams_json)
{
    if (!output_streams_json.is_array())
    {
        return std::nullopt;
    }

    std::vector<frontend_output_stream_t> output_streams;
    for (size_t i = 0; i < output_streams_json.size(); ++i)
    {
        auto output_cfg = output_streams_json[i];
        frontend_output_stream_t output;
        output.id = OUTPUT_SINK_ID(i);
        output.width = output_cfg["width"];
        output.height = output_cfg["height"];
        output.target_fps = output_cfg["framerate"];
        output.current_fps = 0;
        output_streams.push_back(output);
    }
    return output_streams;
}

tl::expected<GstElement *, media_library_return> MediaLibraryFrontend::Impl::get_frontend_element()
{
    // get gsthailofrontendbinsrc element from m_pipeline
    GstElement *frontendbinsrc = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
    // get the element with type hailofrontend from the bin
    GstElement *frontend = gst_bin_get_by_name(GST_BIN(frontendbinsrc), "hailofrontendelement");
    if (frontend == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get frontend element");
        return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }

    gst_object_unref(frontendbinsrc);
    return frontend;
}

tl::expected<frontend_config_t, media_library_return> MediaLibraryFrontend::Impl::get_config()
{
    auto frontendbinsrc = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
    if (frontendbinsrc)
    {
        frontend_element_config_t frontend_element_config;
        gpointer value = nullptr;
        g_object_get(frontendbinsrc, "config", &value, NULL);
        frontend_element_config = *reinterpret_cast<frontend_element_config_t *>(value);

        hdr_config_t hdr_config;
        g_object_get(frontendbinsrc, "hdr-config", &value, NULL);
        hdr_config = *reinterpret_cast<hdr_config_t *>(value);

        g_object_get(frontendbinsrc, "hailort-config", &value, NULL);
        hailort_t hailort_config;
        hailort_config = *reinterpret_cast<hailort_t *>(value);

        g_object_get(frontendbinsrc, "input-video-config", &value, NULL);
        input_video_config_t input_config;
        input_config = *reinterpret_cast<input_video_config_t *>(value);

        g_object_get(frontendbinsrc, "isp-config", &value, NULL);
        isp_t isp_config;
        isp_config = *reinterpret_cast<isp_t *>(value);

        gst_object_unref(frontendbinsrc);

        frontend_config_t frontend_config;
        frontend_config.ldc_config = frontend_element_config.ldc_config;
        frontend_config.denoise_config = frontend_element_config.denoise_config;
        frontend_config.multi_resize_config = frontend_element_config.multi_resize_config;
        frontend_config.input_config = input_config;
        frontend_config.hdr_config = hdr_config;
        frontend_config.hailort_config = hailort_config;
        frontend_config.isp_config = isp_config;

        return frontend_config;
    }
    else
    {
        gst_object_unref(frontendbinsrc);
        return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }
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

    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start pipeline");
        return MEDIA_LIBRARY_ERROR;
    }
    m_main_loop_thread = std::make_shared<std::thread>([this]() { g_main_loop_run(m_main_loop); });

    GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
    if (frontend == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get frontend element");
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

    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_remove_watch(bus);
    gst_object_unref(bus);

    if (m_main_loop_thread->joinable())
    {
        m_main_loop_thread->join();
    }

    return MEDIA_LIBRARY_SUCCESS;
}

bool MediaLibraryFrontend::Impl::is_config_change_allowed(nlohmann::json old_output_streams_config,
                                                          nlohmann::json new_output_streams_config,
                                                          frontend_src_element_t new_config_input_stream_type)
{
    if (new_config_input_stream_type != m_src_element)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config change not allowed, input stream type is different");
        return false;
    }

    // only change allowed in outputs is framerate
    for (auto &output_stream : old_output_streams_config)
    {
        output_stream.erase(output_stream.find("framerate"));
    }
    for (auto &output_stream : new_output_streams_config)
    {
        output_stream.erase(output_stream.find("framerate"));
    }
    if (old_output_streams_config != new_output_streams_config)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config change not allowed, output streams are different");
        return false;
    }
    return true;
}

media_library_return MediaLibraryFrontend::Impl::set_config(const std::string &json_config)
{
    if (!json_config.empty() && (json_config == m_json_config_str))
    {
        return MEDIA_LIBRARY_SUCCESS;
    }

    if (m_config_manager.validate_configuration(json_config) != MEDIA_LIBRARY_SUCCESS)
    {
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    media_library_return rc = MEDIA_LIBRARY_SUCCESS;

    const nlohmann::json old_config_output_streams = get_output_streams_json(m_json_config_str);
    const nlohmann::json new_config_output_streams = get_output_streams_json(json_config);

    const frontend_src_element_t new_config_input_stream_type = get_input_stream_type(json_config);
    const auto [input_width, input_height] = get_input_resolution(json_config);

    if (!m_json_config_str.empty() &&
        !is_config_change_allowed(old_config_output_streams, new_config_output_streams,
                                  new_config_input_stream_type)) // require replacing working pipeline
    {
        LOGGER__MODULE__ERROR(
            MODULE_NAME,
            "Failed to set config, input or output streams cannot be changed after successful frontend configure");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    if (m_json_config_str.empty())
    {
        auto output_streams = create_output_streams(new_config_output_streams);
        if (!output_streams.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get output streams from json config");
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
        if (!init_pipeline(json_config, new_config_input_stream_type, input_width, input_height,
                           output_streams.value()))
        {
            return MEDIA_LIBRARY_ERROR;
        }
        m_src_element = new_config_input_stream_type;
        m_output_streams = std::move(
            output_streams.value()); // has to move, when setting fps cb in init_pipeline output_stream given as arg
    }
    else
    {
        GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
        if (frontend == nullptr)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get frontend element");
            return MEDIA_LIBRARY_UNINITIALIZED;
        }

        g_object_set(G_OBJECT(frontend), "config-string", json_config.c_str(), NULL);
        gst_object_unref(frontend);
    }

    m_json_config_str = json_config;
    return rc;
}

media_library_return MediaLibraryFrontend::Impl::set_config(const frontend_config_t &config)
{
    if (m_pipeline == nullptr)
    {
        return MEDIA_LIBRARY_UNINITIALIZED;
    }
    auto frontendbinsrc = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
    media_library_return ret = MEDIA_LIBRARY_ERROR;
    if (frontendbinsrc != nullptr)
    {
        g_object_set(G_OBJECT(frontendbinsrc), "config", &config, NULL);
        ret = MEDIA_LIBRARY_SUCCESS;
    }
    gst_object_unref(frontendbinsrc);
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

tl::expected<std::vector<frontend_output_stream_t>, media_library_return> MediaLibraryFrontend::Impl::
    get_outputs_streams()
{
    if (m_output_streams.empty())
    {
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }
    return m_output_streams;
}

void MediaLibraryFrontend::Impl::deinit_pipeline()
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Cleaning frontend gst pipeline");
    stop();
    if ((m_src_element == frontend_src_element_t::APPSRC) && (m_appsrc_caps != nullptr))
    {
        gst_caps_unref(m_appsrc_caps);
        m_appsrc_caps = nullptr;
    }
    if (m_appsrc != nullptr)
    {
        gst_object_unref(m_appsrc);
        m_appsrc = nullptr;
    }
    if (m_pipeline != nullptr)
    {
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }
}

bool MediaLibraryFrontend::Impl::init_pipeline(const std::string &frontend_json_config,
                                               frontend_src_element_t source_type, uint16_t input_width,
                                               uint16_t input_height,
                                               std::vector<frontend_output_stream_t> &output_streams)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Initializing frontend gst pipeline");

    GstElement *pipeline = gst_parse_launch(
        create_pipeline_string(frontend_json_config, source_type, input_width, input_height, output_streams).c_str(),
        NULL);
    if (pipeline == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create pipeline");
        return false;
    }

    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, (GstBusFunc)bus_call, this);
    gst_object_unref(bus);

    GstElement *appsrc = nullptr;
    GstCaps *appsrc_caps = nullptr;
    if (source_type == frontend_src_element_t::APPSRC)
    {
        appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "src");
        appsrc_caps =
            gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12", "width", G_TYPE_INT, input_width,
                                "height", G_TYPE_INT, input_height, "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
        g_object_set(G_OBJECT(appsrc), "caps", appsrc_caps, NULL);
    }

    if (!set_gst_callbacks(pipeline, source_type, output_streams))
    {
        return false;
    }

    if (appsrc != nullptr)
    {
        m_appsrc = GST_APP_SRC(appsrc);
        m_appsrc_caps = appsrc_caps;
    }

    m_pipeline = pipeline;
    return true;
}

std::string MediaLibraryFrontend::Impl::create_pipeline_string(
    const std::string &frontend_json_config, frontend_src_element_t source_type, uint16_t input_width,
    uint16_t input_height, const std::vector<frontend_output_stream_t> &output_streams)
{
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
    LOGGER__MODULE__INFO(MODULE_NAME, "Pipeline: gst-launch-1.0 {}", pipeline_str);

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

bool MediaLibraryFrontend::Impl::set_gst_callbacks(GstElement *pipeline, frontend_src_element_t source_type,
                                                   std::vector<frontend_output_stream_t> &output_streams)
{
    GstElement *appsrc = nullptr;
    std::vector<GstElement *> fpssinks;

    if (source_type == frontend_src_element_t::APPSRC)
    {
        const gchar *gst_element_name = "src";
        appsrc = gst_bin_get_by_name(GST_BIN(pipeline), gst_element_name);
        if (appsrc == nullptr)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Could not find gst element {}", gst_element_name);
            return false;
        }
    }

    for (auto &output_stream : output_streams)
    {
        const std::string gst_element_name = std::string("fpsdisplay") + output_stream.id;
        GstElement *fpssink = gst_bin_get_by_name(GST_BIN(pipeline), gst_element_name.c_str());
        if (fpssink == nullptr)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Could not find gst element {}", gst_element_name);
            return false;
        }
        fpssinks.push_back(fpssink);
    }

    if (appsrc != nullptr)
    {
        GstAppSrcCallbacks appsrc_callbacks = {};
        gst_app_src_set_callbacks(GST_APP_SRC(appsrc), &appsrc_callbacks, (void *)this, NULL);
        gst_object_unref(appsrc);
    }

    GstAppSinkCallbacks appsink_callbacks = {};
    appsink_callbacks.new_sample = new_sample;
    for (size_t i = 0; i < output_streams.size(); ++i)
    {
        auto &output_stream = output_streams[i];
        auto &fpssink = fpssinks[i];

        LOGGER__MODULE__INFO(MODULE_NAME, "Setting callback for sink {}", output_stream.id);

        g_signal_connect(fpssink, "fps-measurements", G_CALLBACK(fps_measurement), &output_stream);
        gst_object_unref(fpssink);

        GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline), output_stream.id.c_str());
        gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &appsink_callbacks, (void *)this, NULL);
        gst_object_unref(appsink);
    }

    return true;
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
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get hailo buffer meta");
        GST_ERROR("Failed to get hailo buffer meta");
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    HailoMediaLibraryBufferPtr buffer_ptr = buffer_meta->buffer_ptr;
    uint32_t used_size = buffer_meta->used_size;
    if (!buffer_ptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get hailo buffer ptr");
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
        cb(buffer_ptr, used_size);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

PrivacyMaskBlenderPtr MediaLibraryFrontend::Impl::get_privacy_mask_blender()
{
    return m_privacy_blender;
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
    GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
    if (frontend == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get frontend element");
        return MEDIA_LIBRARY_ERROR;
    }
    g_object_set(G_OBJECT(frontend), "freeze", freeze, NULL);
    gst_object_unref(frontend);
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
