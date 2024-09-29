#pragma once
#include "media_library/frontend.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/privacy_mask.hpp"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <thread>

class MediaLibraryFrontend::Impl final
{
public:
    static tl::expected<std::shared_ptr<MediaLibraryFrontend::Impl>, media_library_return> create(frontend_src_element_t src_element, std::string json_config);

    ~Impl();
    Impl(frontend_src_element_t src_element, std::string json_config, media_library_return &status);

    tl::expected<std::vector<frontend_output_stream_t>, media_library_return> get_outputs_streams();
    media_library_return subscribe(FrontendCallbacksMap callback);
    media_library_return start();
    media_library_return stop();
    media_library_return add_buffer(HailoMediaLibraryBufferPtr ptr);
    media_library_return configure(std::string json_config);
    media_library_return configure(frontend_config_t config);
    tl::expected<GstElement *, media_library_return> get_frontend_element();
    tl::expected<frontend_config_t, media_library_return> get_config();

    void on_need_data(GstAppSrc *appsrc, guint size);
    void on_enough_data(GstAppSrc *appsrc);
    GstFlowReturn on_new_sample(output_stream_id_t id, GstAppSink *appsink);
    void on_fps_measurement(GstElement *fpssink, gdouble fps, gdouble droprate, gdouble avgfps);

    PrivacyMaskBlenderPtr get_privacy_mask_blender();
    float get_current_fps();

private:
    static void fps_measurement(GstElement *fpssink, gdouble fps,
                                gdouble droprate, gdouble avgfps,
                                gpointer user_data)
    {
        MediaLibraryFrontend::Impl *fe = static_cast<MediaLibraryFrontend::Impl *>(user_data);
        fe->on_fps_measurement(fpssink, fps, droprate, avgfps);
        fe->update_fps(fps);
    }
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
        gchar *name = gst_element_get_name(GST_ELEMENT(appsink));
        MediaLibraryFrontend::Impl *fe = static_cast<MediaLibraryFrontend::Impl *>(user_data);
        GstFlowReturn ret = fe->on_new_sample(name, appsink);
        g_free(name);
        return ret;
    }
    void update_fps(gdouble fps) {
        m_current_fps = static_cast<float>(fps);
    }

    void set_gst_callbacks();
    std::string create_pipeline_string();

    frontend_src_element_t m_src_element;
    std::string m_config_str;
    nlohmann::json m_json_config;
    std::vector<frontend_output_stream_t> m_output_streams;
    guint m_send_buffer_id;
    GstElement *m_pipeline;
    std::map<output_stream_id_t, std::vector<FrontendWrapperCallback>> m_callbacks;
    PrivacyMaskBlenderPtr m_privacy_blender;
    float m_current_fps;
    GstAppSrc *m_appsrc;
    GstCaps *m_appsrc_caps;
    GMainLoop *m_main_loop;
    std::shared_ptr<std::thread> m_main_loop_thread;
};