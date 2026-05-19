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
/**
 * @file main.cpp
 * @brief GStreamer vision + tiling detection + overlay + GStreamer encoder.
 *
 * Architecture:
 *
 *   GStreamer input:   gsthailovision ──sink0──→ queue → appsink
 *                                     ──sink1──→ queue → gsthailoencoder → queue → rtph264pay → udpsink
 *
 *   Analytics:         GstSourceStage → TilingDetectionPipeline → OverlayStage → GstSinkStage
 *
 *   GStreamer output:  appsrc → gsthailoencoder → rtph264pay → udpsink
 *
 * Both GStreamer pipelines share the same name so gsthailovision and
 * gsthailoencoder share the same MediaLibrary instance. They are separate
 * GstPipeline objects to avoid a deadlock during state transitions.
 */

#include <gst/gst.h>
#include <cxxopts/cxxopts.hpp>
#include <glib-object.h>
#include <glib.h>
#include <gst/gstformat.h>
#include <stddef.h>
#include <tl/expected.hpp>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <chrono>
#include <memory>
#include <utility>
#include <vector>

#include "media_library/signal_utils.hpp"
#include "hailo_analytics/analytics/ai_models_config.hpp"
#include "hailo_analytics/utils/stream_utils.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/analytics/tiling.hpp"
#include "hailo_analytics/pipeline/overlay/overlay_stage.hpp"
#include "hailo_analytics/pipeline/sinks/gst_sink_stage.hpp"
#include "hailo_analytics/pipeline/sources/gst_source_stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/analytics/detection.hpp"

#define HOST_IP "10.0.0.2"
#define UDP_PORT 5000
#define STREAM_ID_0 "sink0"
#define STREAM_ID_1 "sink1"
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/face_landmarks_medialib_config.json"
#define NO_PROFILE_SELECTED ""
#define TILING_PIPELINE "tiling_detection_pipeline"

// IMPORTANT: Both pipelines MUST share the same name. The GStreamer elements
// gsthailovision and gsthailoencoder use the pipeline name as a key to look up
// and share the same MediaLibrary instance. If the names differ, the encoder
// will try to get a separate MediaLibrary instance and fail to find the stream
// configuration set up by the vision element.
static constexpr const char *PIPELINE_NAME = "vision-analytics-encoder";

struct AppConfig
{
    int timeout;
    std::string config_path;
    std::string host_ip;
    std::string switch_profile;
    int switch_delay;
};

struct InputPipeline
{
    GstElement *pipeline;
    GstElement *vision;
    GstElement *appsink;
};

struct OutputPipeline
{
    GstElement *pipeline;
    GstElement *appsrc;
};

// ── Stop signaling ──────────────────────────────────────────────────────────

static std::mutex g_stop_mutex;
static std::condition_variable g_stop_cv;
static bool g_stop_requested = false;

static void request_stop()
{
    {
        std::lock_guard<std::mutex> lock(g_stop_mutex);
        g_stop_requested = true;
    }
    g_stop_cv.notify_all();
}

static void wait_for_stop_or_timeout(int seconds)
{
    std::unique_lock<std::mutex> lock(g_stop_mutex);
    g_stop_cv.wait_for(lock, std::chrono::seconds(seconds), [] { return g_stop_requested; });
}

static gboolean bus_callback(GstBus *, GstMessage *msg, gpointer)
{
    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_ERROR: {
        GError *err = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(msg, &err, &debug);
        std::cerr << "GStreamer error: " << (err ? err->message : "unknown") << std::endl;
        if (debug)
            std::cerr << "  Debug: " << debug << std::endl;
        g_clear_error(&err);
        g_free(debug);
        request_stop();
        break;
    }
    case GST_MESSAGE_EOS:
        std::cout << "End of stream" << std::endl;
        request_stop();
        break;
    default:
        break;
    }
    return TRUE;
}

static void add_bus_watch(GstElement *pipeline)
{
    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_add_watch(bus, bus_callback, nullptr);
    gst_object_unref(bus);
}

static bool link_vision_stream(GstElement *vision, GstPadTemplate *templ, const char *stream_id, GstElement *downstream)
{
    GstPad *src = gst_element_request_pad(vision, templ, stream_id, NULL);
    GstPad *sink = gst_element_get_static_pad(downstream, "sink");
    bool ok = (gst_pad_link(src, sink) == GST_PAD_LINK_OK);
    gst_object_unref(src);
    gst_object_unref(sink);
    return ok;
}

static bool parse_arguments(int argc, char *argv[], AppConfig &config)
{
    cxxopts::Options options("GStreamer vision+detection+encoder pipeline");
    options.add_options()("h,help", "Show this help")("t,timeout", "Time to run (seconds)",
                                                      cxxopts::value<int>()->default_value("60"))(
        "c,config-file-path", "Media library configuration path",
        cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH))(
        "o,host-ip", "Host IP address for UDP output", cxxopts::value<std::string>()->default_value(HOST_IP))(
        "s,switch-profile", "Profile to switch to mid-run",
        cxxopts::value<std::string>()->default_value(NO_PROFILE_SELECTED))(
        "d,switch-delay", "Seconds before switching profile", cxxopts::value<int>()->default_value("10"));

    auto result = options.parse(argc, argv);
    if (result.count("help"))
    {
        std::cout << options.help() << std::endl;
        return false;
    }
    if (!result.unmatched().empty())
    {
        for (const auto &arg : result.unmatched())
            std::cerr << "Error: Unrecognized option: " << arg << std::endl;
        return false;
    }

    config.timeout = result["timeout"].as<int>();
    config.config_path = result["config-file-path"].as<std::string>();
    config.host_ip = result["host-ip"].as<std::string>();
    config.switch_profile = result["switch-profile"].as<std::string>();
    config.switch_delay = result["switch-delay"].as<int>();
    return true;
}

static bool build_input_pipeline(const AppConfig &config, InputPipeline &input)
{
    input.pipeline = gst_pipeline_new(PIPELINE_NAME);

    input.vision = gst_element_factory_make("gsthailovision", "vision");
    if (!input.vision)
    {
        std::cerr << "Failed to create gsthailovision element" << std::endl;
        return false;
    }
    g_object_set(input.vision, "config-path", config.config_path.c_str(), NULL);

    GstElement *queue_in = gst_element_factory_make("queue", "queue_in");
    input.appsink = gst_element_factory_make("appsink", "appsink0");
    GstElement *queue_in1 = gst_element_factory_make("queue", "queue_in1");
    GstElement *encoder1 = gst_element_factory_make("gsthailoencoder", "encoder1");
    GstElement *queue_enc1 = gst_element_factory_make("queue", "queue_enc1");
    GstElement *rtppay1 = gst_element_factory_make("rtph264pay", "rtppay1");
    GstElement *capsfilter1 = gst_element_factory_make("capsfilter", "rtp_capsfilter1");
    GstElement *udpsink1 = gst_element_factory_make("udpsink", "udpsink1");
    if (!queue_in || !input.appsink || !queue_in1 || !encoder1 || !queue_enc1 || !rtppay1 || !capsfilter1 || !udpsink1)
    {
        std::cerr << "Failed to create input pipeline elements" << std::endl;
        return false;
    }

    std::string sink1_port = hailo_analytics::utils::port_from_stream_id(STREAM_ID_1);

    g_object_set(queue_in, "leaky", 0, "max-size-buffers", 5, "max-size-bytes", 0, "max-size-time", (guint64)0, NULL);
    g_object_set(queue_in1, "leaky", 0, "max-size-buffers", 5, "max-size-bytes", 0, "max-size-time", (guint64)0, NULL);
    g_object_set(queue_enc1, "leaky", 0, "max-size-buffers", 5, "max-size-bytes", 0, "max-size-time", (guint64)0, NULL);
    g_object_set(input.appsink, "emit-signals", FALSE, "max-buffers", 2, "drop", TRUE, "sync", FALSE, NULL);
    g_object_set(encoder1, "stream-id", STREAM_ID_1, NULL);

    GstCaps *rtp_caps1 = gst_caps_from_string("application/x-rtp,media=video,encoding-name=H264");
    g_object_set(capsfilter1, "caps", rtp_caps1, NULL);
    gst_caps_unref(rtp_caps1);

    g_object_set(udpsink1, "host", config.host_ip.c_str(), "port", std::stoi(sink1_port), "sync", FALSE, NULL);

    gst_bin_add_many(GST_BIN(input.pipeline), input.vision, queue_in, input.appsink, queue_in1, encoder1, queue_enc1,
                     rtppay1, capsfilter1, udpsink1, NULL);

    GstPadTemplate *vision_src_templ = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(input.vision), "%s");

    if (!link_vision_stream(input.vision, vision_src_templ, STREAM_ID_0, queue_in) ||
        !gst_element_link(queue_in, input.appsink))
    {
        std::cerr << "Failed to link vision sink0 → appsink" << std::endl;
        return false;
    }
    if (!link_vision_stream(input.vision, vision_src_templ, STREAM_ID_1, queue_in1) ||
        !gst_element_link_many(queue_in1, encoder1, queue_enc1, rtppay1, capsfilter1, udpsink1, NULL))
    {
        std::cerr << "Failed to link vision sink1 → encoder → udpsink" << std::endl;
        return false;
    }

    return true;
}

static bool build_output_pipeline(const AppConfig &config, OutputPipeline &output)
{
    output.pipeline = gst_pipeline_new(PIPELINE_NAME);

    output.appsrc = gst_element_factory_make("appsrc", "appsrc0");
    GstElement *encoder = gst_element_factory_make("gsthailoencoder", "encoder");
    GstElement *queue_out = gst_element_factory_make("queue", "queue_out");
    GstElement *rtppay = gst_element_factory_make("rtph264pay", "rtppay");
    GstElement *udpsink_elem = gst_element_factory_make("udpsink", "udpsink");
    if (!output.appsrc || !encoder || !queue_out || !rtppay || !udpsink_elem)
    {
        std::cerr << "Failed to create output pipeline elements" << std::endl;
        return false;
    }

    g_object_set(encoder, "stream-id", STREAM_ID_0, NULL);
    g_object_set(output.appsrc, "is-live", TRUE, "format", GST_FORMAT_TIME, NULL);
    g_object_set(queue_out, "leaky", 0, "max-size-buffers", 5, "max-size-bytes", 0, "max-size-time", (guint64)0, NULL);
    g_object_set(udpsink_elem, "host", config.host_ip.c_str(), "port", UDP_PORT, "sync", FALSE, NULL);

    GstCaps *rtp_caps = gst_caps_from_string("application/x-rtp,media=video,encoding-name=H264");
    GstElement *capsfilter = gst_element_factory_make("capsfilter", "rtp_capsfilter");
    g_object_set(capsfilter, "caps", rtp_caps, NULL);
    gst_caps_unref(rtp_caps);

    gst_bin_add_many(GST_BIN(output.pipeline), output.appsrc, encoder, queue_out, rtppay, capsfilter, udpsink_elem,
                     NULL);
    if (!gst_element_link_many(output.appsrc, encoder, queue_out, rtppay, capsfilter, udpsink_elem, NULL))
    {
        std::cerr << "Failed to link output pipeline" << std::endl;
        return false;
    }

    return true;
}

static hailo_analytics::pipeline::PipelinePtr build_analytics_pipeline(GstElement *appsink, GstElement *appsrc)
{
    using namespace hailo_analytics::pipeline;

    auto gst_source = sources::GstSourceStageBuild::create().set_stage_name("gst_source").buildptr();
    gst_source->add_appsink(STREAM_ID_0, appsink);

    hailo_analytics::analytics::tiling::tiling_detection_config_t tiling_cfg;
    namespace ai_models = hailo_analytics::analytics::ai_models;
    ai_models::apply_to(ai_models::YOLOV8N, tiling_cfg.detection_config);
    auto tiling_pipeline_status =
        hailo_analytics::analytics::tiling::generate_tiling_detection_pipeline(TILING_PIPELINE, tiling_cfg);
    if (!tiling_pipeline_status.has_value())
    {
        std::cerr << "Failed to create tiling detection pipeline" << std::endl;
        return nullptr;
    }
    auto tiling_pipeline = tiling_pipeline_status.value();

    auto overlay_stage =
        overlay::OverlayStageBuild::create().set_stage_name("overlay_stage").set_queue_size(3).buildptr();

    auto gst_output = sinks::GstSinkStageBuild::create()
                          .set_stage_name("gst_output")
                          .set_queue_size_opt(2)
                          .set_leaky_opt(true)
                          .buildptr();
    gst_output->configure(appsrc);

    return PipelineBuilder()
        .add_stage(gst_source, StageType::SOURCE)
        .add_stage(tiling_pipeline)
        .add_stage(overlay_stage)
        .add_stage(gst_output, StageType::SINK)
        .connect_frontend("gst_source", STREAM_ID_0, TILING_PIPELINE)
        .connect(TILING_PIPELINE, "overlay_stage")
        .connect("overlay_stage", "gst_output")
        .build("analytics_pipeline");
}

static bool start_pipelines(InputPipeline &input, OutputPipeline &output,
                            hailo_analytics::pipeline::PipelinePtr &analytics)
{
    std::cout << "Starting input pipeline..." << std::endl;
    if (gst_element_set_state(input.pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    {
        std::cerr << "Failed to start input pipeline" << std::endl;
        return false;
    }

    std::cout << "Starting analytics pipeline..." << std::endl;
    if (analytics->start() != hailo_analytics::pipeline::AppStatus::SUCCESS)
    {
        std::cerr << "Failed to start analytics pipeline" << std::endl;
        gst_element_set_state(input.pipeline, GST_STATE_NULL);
        return false;
    }

    std::cout << "Starting output pipeline..." << std::endl;
    if (gst_element_set_state(output.pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    {
        std::cerr << "Failed to start output pipeline" << std::endl;
        analytics->stop();
        gst_element_set_state(input.pipeline, GST_STATE_NULL);
        return false;
    }

    return true;
}

static void run_with_profile_switch(const AppConfig &config, GstElement *vision)
{
    std::cout << "Running for " << config.timeout << "s (UDP " << config.host_ip << ":" << UDP_PORT << ")..."
              << std::endl;

    if (config.switch_profile != NO_PROFILE_SELECTED)
    {
        std::cout << "Will switch to profile '" << config.switch_profile << "' after " << config.switch_delay << "s"
                  << std::endl;
        wait_for_stop_or_timeout(config.switch_delay);

        if (!g_stop_requested)
        {
            std::cout << "Switching profile to '" << config.switch_profile << "'..." << std::endl;
            g_object_set(vision, "profile-name", config.switch_profile.c_str(), NULL);

            int remaining = config.timeout - config.switch_delay;
            if (remaining > 0)
                wait_for_stop_or_timeout(remaining);
        }
    }
    else
    {
        wait_for_stop_or_timeout(config.timeout);
    }
}

static void stop_and_cleanup(InputPipeline &input, OutputPipeline &output,
                             hailo_analytics::pipeline::PipelinePtr &analytics)
{
    std::cout << "Stopping..." << std::endl;
    analytics->stop();
    gst_element_set_state(output.pipeline, GST_STATE_NULL);
    gst_element_set_state(input.pipeline, GST_STATE_NULL);
    gst_object_unref(output.pipeline);
    gst_object_unref(input.pipeline);
}

int main(int argc, char *argv[])
{
    AppConfig config;
    if (!parse_arguments(argc, argv, config))
        return 1;

    signal_utils::SignalHandler signal_handler(false);
    signal_handler.register_signal_handler([]([[maybe_unused]] int signal) {
        std::cout << "Stopping pipeline..." << std::endl;
        request_stop();
    });

    gst_init(&argc, &argv);

    InputPipeline input{};
    if (!build_input_pipeline(config, input))
        return 1;

    OutputPipeline output{};
    if (!build_output_pipeline(config, output))
        return 1;

    add_bus_watch(input.pipeline);
    add_bus_watch(output.pipeline);

    auto analytics = build_analytics_pipeline(input.appsink, output.appsrc);
    if (!analytics)
    {
        gst_object_unref(input.pipeline);
        gst_object_unref(output.pipeline);
        return 1;
    }

    if (!start_pipelines(input, output, analytics))
    {
        gst_object_unref(input.pipeline);
        gst_object_unref(output.pipeline);
        return 1;
    }

    run_with_profile_switch(config, input.vision);
    stop_and_cleanup(input, output, analytics);

    return 0;
}
