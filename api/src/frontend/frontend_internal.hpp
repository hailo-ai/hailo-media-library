#pragma once
#include "media_library/config_parser.hpp"
#include "media_library/frontend.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/privacy_mask.hpp"
#include "gstmedialibcommon.hpp"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <thread>
#include <shared_mutex>

class MediaLibraryFrontend::Impl final
{
  public:
    struct output_view_t
    {
        std::string id;
        uint16_t width = 0;
        uint16_t height = 0;
        uint16_t framerate = 0;
    };

    static tl::expected<std::shared_ptr<MediaLibraryFrontend::Impl>, media_library_return> create();

    ~Impl();
    Impl(media_library_return &status);

    tl::expected<std::vector<frontend_output_stream_t>, media_library_return> get_outputs_streams();
    media_library_return subscribe(FrontendCallbacksMap callback);
    tl::expected<std::vector<std::string>, media_library_return> get_all_subscribers_ids();
    media_library_return unsubscribe_all();
    media_library_return unsubscribe(const std::string &id);
    media_library_return start();
    media_library_return stop();
    media_library_return add_buffer(HailoMediaLibraryBufferPtr ptr);
    media_library_return set_config(const std::string &json_config);
    media_library_return set_config(const frontend_config_t &config);
    tl::expected<GstElementPtr, media_library_return> get_frontend_element();
    tl::expected<frontend_config_t, media_library_return> get_config();
    media_library_return set_freeze(bool freeze);
    void on_need_data(GstAppSrc *appsrc, guint size);
    bool is_started();
    bool wait_for_pipeline_playing(std::chrono::milliseconds timeout);
    bool wait_for_main_loop(std::chrono::milliseconds timeout);
    void on_enough_data(GstAppSrc *appsrc);
    GstFlowReturn on_new_sample(output_stream_id_t id, GstAppSink *appsink);
    void on_fps_measurement(GstElement *fpssink, gdouble fps, gdouble droprate, gdouble avgfps);

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

    bool set_gst_callbacks(GstElementPtr &pipeline, frontend_src_element_t source_type,
                           std::vector<frontend_output_stream_t> &output_streams);
    GstElementPtr create_pipeline(const frontend_config_t &config, frontend_src_element_t source_type,
                                  uint16_t input_width, uint16_t input_height,
                                  std::vector<frontend_output_stream_t> &output_streams);
    bool create_output_stream_elements(GstElement *pipeline, GstElement *frontend, frontend_output_stream_t &stream);
    media_library_return handle_outputs_change(const frontend_config_t &new_cfg);

    // Helper functions for handle_outputs_change
    struct OutputStreamsDiff
    {
        std::vector<std::pair<int, frontend_output_stream_t>> added;
        std::vector<std::pair<int, frontend_output_stream_t>> removed;
        std::vector<int> modified;
    };

    OutputStreamsDiff compute_output_stream_diff(const std::vector<frontend_output_stream_t> &old_outs,
                                                 const std::vector<frontend_output_stream_t> &new_outs);
    media_library_return verify_removed_outputs_not_subscribed(
        const std::vector<std::pair<int, frontend_output_stream_t>> &removed_outputs);
    bool remove_output_stream(const frontend_output_stream_t &removed_output);
    media_library_return add_output_stream(frontend_output_stream_t &stream);

    bool init_pipeline(const frontend_config_t &config, frontend_src_element_t source_type, uint16_t input_width,
                       uint16_t input_height);

    static std::optional<std::vector<frontend_output_stream_t>> create_output_streams_string(
        const nlohmann::json &output_streams_json);
    static std::vector<output_view_t> get_output_streams_view(const frontend_config_t &cfg);
    static std::optional<std::vector<frontend_output_stream_t>> create_output_streams_from_config(
        const frontend_config_t &cfg);
    bool is_config_change_allowed(const std::vector<output_view_t> &old_outs,
                                  const std::vector<output_view_t> &new_outs, frontend_src_element_t new_input_type);

    GstAppSrcPtr m_appsrc;
    GstCapsPtr m_appsrc_caps;
    GMainLoopPtr m_main_loop;
    GstElementPtr m_pipeline;
    guint m_bus_watch_id = 0;

    std::mutex m_config_mtx{};
    bool m_has_config = false;
    frontend_config_t m_current_config{};
    frontend_src_element_t m_src_element;
    std::string m_json_config_str;
    std::vector<frontend_output_stream_t> m_output_streams;
    guint m_send_buffer_id;
    std::map<output_stream_id_t, FrontendWrapperCallback> m_callbacks;
    std::shared_mutex m_callbacks_mutex;
    std::shared_ptr<std::thread> m_main_loop_thread;
    ConfigParser m_config_parser;
};
