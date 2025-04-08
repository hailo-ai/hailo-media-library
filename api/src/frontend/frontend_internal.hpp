#pragma once
#include "media_library/config_manager.hpp"
#include "media_library/frontend.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/privacy_mask.hpp"
#include "gstmedialibcommon.hpp"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <thread>

class MediaLibraryFrontend::Impl final
{
  public:
    static tl::expected<std::shared_ptr<MediaLibraryFrontend::Impl>, media_library_return> create();

    ~Impl();
    Impl(media_library_return &status);

    tl::expected<std::vector<frontend_output_stream_t>, media_library_return> get_outputs_streams();
    media_library_return subscribe(FrontendCallbacksMap callback);
    media_library_return start();
    media_library_return stop();
    media_library_return add_buffer(HailoMediaLibraryBufferPtr ptr);
    media_library_return set_config(const std::string &json_config);
    media_library_return set_config(const frontend_config_t &config);
    tl::expected<GstElement *, media_library_return> get_frontend_element();
    tl::expected<frontend_config_t, media_library_return> get_config();
    media_library_return set_freeze(bool freeze);
    void on_need_data(GstAppSrc *appsrc, guint size);
    bool is_started();
    void on_enough_data(GstAppSrc *appsrc);
    GstFlowReturn on_new_sample(output_stream_id_t id, GstAppSink *appsink);
    void on_fps_measurement(GstElement *fpssink, gdouble fps, gdouble droprate, gdouble avgfps);

    PrivacyMaskBlenderPtr get_privacy_mask_blender();
    std::unordered_map<output_stream_id_t, float> get_output_streams_current_fps();
    gboolean on_bus_call(GstMessage *msg);

  private:
    static void fps_measurement(GstElement *fpssink, gdouble fps, gdouble droprate, gdouble avgfps,
                                frontend_output_stream_t *output_stream);

    static void need_data(GstAppSrc *appsrc, guint size, gpointer user_data)
    {
        MediaLibraryFrontend::Impl *fe = static_cast<MediaLibraryFrontend::Impl *>(user_data);
        fe->on_need_data(appsrc, size);
    }
    static void enough_data(GstAppSrc *appsrc, gpointer user_data)
    {
        MediaLibraryFrontend::Impl *fe = static_cast<MediaLibraryFrontend::Impl *>(user_data);
        fe->on_enough_data(appsrc);
    }
    static GstFlowReturn new_sample(GstAppSink *appsink, gpointer user_data)
    {
        auto name = glib_cpp::get_name(GST_ELEMENT(appsink));
        MediaLibraryFrontend::Impl *fe = static_cast<MediaLibraryFrontend::Impl *>(user_data);
        GstFlowReturn ret = fe->on_new_sample(name, appsink);
        return ret;
    }
    static gboolean bus_call(GstBus *, GstMessage *msg, gpointer user_data)
    {
        MediaLibraryFrontend::Impl *fe = static_cast<MediaLibraryFrontend::Impl *>(user_data);
        return fe->on_bus_call(msg);
    }

    bool set_gst_callbacks(GstElement *pipeline, frontend_src_element_t source_type,
                           std::vector<frontend_output_stream_t> &output_streams);
    std::string create_pipeline_string(const std::string &frontend_json_config, frontend_src_element_t source_type,
                                       uint16_t input_width, uint16_t input_height,
                                       const std::vector<frontend_output_stream_t> &output_streams);
    bool init_pipeline(const std::string &frontend_json_config, frontend_src_element_t source_type,
                       uint16_t input_width, uint16_t input_height,
                       std::vector<frontend_output_stream_t> &output_streams);
    void deinit_pipeline();

    static std::optional<std::vector<frontend_output_stream_t>> create_output_streams(
        const nlohmann::json &output_streams_json);
    static nlohmann::json get_output_streams_json(const std::string &validated_json_config);
    static frontend_src_element_t get_input_stream_type(const std::string &validated_json_config);
    static std::pair<uint16_t, uint16_t> get_input_resolution(const std::string &validated_json_config);
    static constexpr const char *DEFAULT_INPUT_STREAM_TYPE = "V4L2SRC";
    bool is_config_change_allowed(nlohmann::json old_output_streams_config, nlohmann::json new_output_streams_config,
                                  frontend_src_element_t new_config_input_stream_type);

    GstAppSrc *m_appsrc = nullptr;
    GstCaps *m_appsrc_caps = nullptr;
    GMainLoop *m_main_loop = nullptr;
    GstElement *m_pipeline = nullptr;

    frontend_src_element_t m_src_element;
    std::string m_json_config_str;
    std::vector<frontend_output_stream_t> m_output_streams;
    guint m_send_buffer_id;
    std::map<output_stream_id_t, std::vector<FrontendWrapperCallback>> m_callbacks;
    PrivacyMaskBlenderPtr m_privacy_blender;
    std::shared_ptr<std::thread> m_main_loop_thread;
    ConfigManager m_config_manager;
};
