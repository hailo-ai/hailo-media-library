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
 * @file gst_vision_encoder_example.cpp
 * @brief GStreamer pipeline example using gsthailovision + gsthailoencoder.
 *
 * Pipeline layout (2 streams):
 *
 *   gsthailovision name=v config-path=<config.json>
 *     v.sink0 ! queue ! gsthailoencoder stream-id=sink0 ! fakesink
 *     v.sink1 ! queue ! gsthailoencoder stream-id=sink1 ! h264parse config-interval=1 ! filesink
 * location=/tmp/stream1.h264
 *
 * Runs in 3 phases:
 *   Phase 1: Record with default profile settings (loaded automatically)
 *   Phase 2: Override encoder bitrate via override-profile
 *   Phase 3: Switch to alternate profile (if available)
 */

#include <gst/gst.h>
#include <nlohmann/json.hpp>
#include <glib-object.h>
#include <glib.h>
#include <gst/gstparse.h>
#include <stddef.h>
#include <stdint.h>
#include <condition_variable>
#include <fstream> // IWYU pragma: keep
#include "media_library/cloexec_fstream.hpp"
#include <iostream>
#include <mutex>
#include <sstream> // IWYU pragma: keep
#include <string>
#include <variant>
#include <vector>
#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <iterator>
#include <map>
#include <utility>

#include "media_library/media_library_types.hpp"
#include "media_library/signal_utils.hpp"
#include "encoder_config_types.hpp"

static constexpr const char *DEFAULT_CONFIG_PATH = "/etc/imaging/cfg/medialib_configs/gst_example_medialib_config.json";
static constexpr const char *OUTPUT_PATH = "/tmp/stream1.h264";
static constexpr const char *STREAM_ID_FAKE = "sink0";
static constexpr const char *STREAM_ID = "sink1";
static constexpr uint32_t OVERRIDE_TARGET_BITRATE = 10000000; // 10 Mbps
static constexpr int DEFAULT_TIMEOUT_SECONDS = 30;

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

static std::string read_file(const std::string &path)
{
    cloexec::ifstream file(path);
    if (!file.is_open())
        return {};
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static std::vector<std::string> get_all_profile_names(const std::string &config_string)
{
    auto root = nlohmann::json::parse(config_string, nullptr, false);
    if (root.is_discarded() || !root.contains("profiles"))
        return {};
    std::vector<std::string> names;
    for (const auto &profile : root["profiles"])
        names.push_back(profile.value("name", ""));
    return names;
}

static std::string build_pipeline_string(const std::string &config_path)
{
    std::ostringstream pipeline_builder;
    pipeline_builder << "gsthailovision name=v config-path=" << config_path << " v." << STREAM_ID_FAKE
                     << " ! queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0"
                     << " ! gsthailoencoder stream-id=" << STREAM_ID_FAKE << " ! fakesink"
                     << " v." << STREAM_ID << " ! queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0"
                     << " ! gsthailoencoder stream-id=" << STREAM_ID << " ! h264parse config-interval=1"
                     << " ! filesink location=" << OUTPUT_PATH << " sync=false";
    return pipeline_builder.str();
}

static gboolean bus_callback(GstBus *, GstMessage *msg, gpointer)
{
    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_ERROR: {
        GError *err = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(msg, &err, &debug);
        std::cerr << "ERROR: " << (err ? err->message : "unknown") << std::endl;
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

// Returns a config_profile_t* that the caller must delete.
static config_profile_t *get_current_profile(GstElement *vision)
{
    config_profile_t *profile = nullptr;
    g_object_get(vision, "current-profile", &profile, NULL);
    return profile;
}

static void print_profile_encoder_summary(const config_profile_t &profile)
{
    std::cout << "  Profile: " << profile.name << std::endl;
    for (const auto &[stream_id, stream_config] : profile.encoded_output_streams)
    {
        const encoder_config_t &enc_config = stream_config.encoding;
        if (std::holds_alternative<hailo_encoder_config_t>(enc_config))
        {
            const auto &hailo_enc = std::get<hailo_encoder_config_t>(enc_config);
            std::cout << "  Stream '" << stream_id
                      << "': " << (hailo_enc.output_stream.codec == CODEC_TYPE_H264 ? "H.264" : "H.265")
                      << " bitrate=" << hailo_enc.rate_control.bitrate.target_bitrate << std::endl;
        }
        else if (std::holds_alternative<jpeg_encoder_config_t>(enc_config))
        {
            const auto &jpeg_enc = std::get<jpeg_encoder_config_t>(enc_config);
            std::cout << "  Stream '" << stream_id << "': JPEG quality=" << jpeg_enc.quality << std::endl;
        }
    }
}

static bool override_encoder_bitrate(GstElement *vision, uint32_t new_bitrate)
{
    config_profile_t *current = get_current_profile(vision);
    if (!current)
    {
        std::cerr << "Failed to read current profile" << std::endl;
        return false;
    }

    std::cout << "Current encoder settings:" << std::endl;
    print_profile_encoder_summary(*current);

    config_profile_t override_profile = *current;
    for (auto &[stream_id, stream_config] : override_profile.encoded_output_streams)
    {
        encoder_config_t &enc_config = stream_config.encoding;
        if (!std::holds_alternative<hailo_encoder_config_t>(enc_config))
            continue;

        auto &hailo_enc = std::get<hailo_encoder_config_t>(enc_config);
        std::cout << "Overriding stream '" << stream_id
                  << "' bitrate: " << hailo_enc.rate_control.bitrate.target_bitrate << " -> " << new_bitrate
                  << std::endl;
        hailo_enc.rate_control.bitrate.target_bitrate = new_bitrate;
    }

    g_object_set(vision, "override-profile", &override_profile, NULL);
    std::cout << "Override applied." << std::endl;

    delete current;
    return true;
}

static std::vector<std::string> load_and_print_profiles()
{
    std::string config_string = read_file(DEFAULT_CONFIG_PATH);
    if (config_string.empty())
    {
        std::cerr << "Failed to read config: " << DEFAULT_CONFIG_PATH << std::endl;
        return {};
    }

    std::vector<std::string> profiles = get_all_profile_names(config_string);
    std::cout << "Available profiles:" << std::endl;
    for (const auto &name : profiles)
        std::cout << "  " << name << std::endl;
    return profiles;
}

static GstElement *create_pipeline(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    std::string pipeline_str = build_pipeline_string(DEFAULT_CONFIG_PATH);
    std::cout << "Pipeline: " << pipeline_str << std::endl;

    GError *parse_error = nullptr;
    GstElement *pipeline = gst_parse_launch(pipeline_str.c_str(), &parse_error);
    if (!pipeline || parse_error)
    {
        std::cerr << "Failed to create pipeline: " << (parse_error ? parse_error->message : "unknown") << std::endl;
        if (parse_error)
            g_error_free(parse_error);
        return nullptr;
    }
    return pipeline;
}

static GstElement *start_pipeline(GstElement *pipeline)
{
    GstElement *vision = gst_bin_get_by_name(GST_BIN(pipeline), "v");
    if (!vision)
    {
        std::cerr << "Failed to find gsthailovision element" << std::endl;
        return nullptr;
    }

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_add_watch(bus, bus_callback, nullptr);
    gst_object_unref(bus);

    std::cout << "Starting pipeline..." << std::endl;
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        std::cerr << "Failed to start pipeline" << std::endl;
        gst_object_unref(vision);
        return nullptr;
    }
    return vision;
}

static void run_phase_initial_recording(int duration)
{
    std::cout << "Output file: " << OUTPUT_PATH << std::endl;
    std::cout << std::endl << "--- Phase 1: Recording with default profile settings ---" << std::endl;
    wait_for_stop_or_timeout(duration);
}

static void run_phase_override_bitrate(GstElement *vision, int duration)
{
    if (g_stop_requested)
        return;

    std::cout << std::endl
              << "--- Phase 2: Overriding encoder bitrate to " << OVERRIDE_TARGET_BITRATE << " ---" << std::endl;
    override_encoder_bitrate(vision, OVERRIDE_TARGET_BITRATE);
    wait_for_stop_or_timeout(duration);
}

static void run_phase_switch_profile(GstElement *vision, const std::string &from_profile, const std::string &to_profile,
                                     int duration)
{
    if (g_stop_requested || to_profile.empty())
    {
        if (!g_stop_requested && duration > 0)
            wait_for_stop_or_timeout(duration);
        return;
    }

    std::cout << std::endl
              << "--- Phase 3: Switching profile: " << from_profile << " -> " << to_profile << " ---" << std::endl;
    g_object_set(vision, "profile-name", to_profile.c_str(), NULL);
    std::cout << "Profile switched." << std::endl;

    config_profile_t *new_profile = get_current_profile(vision);
    if (new_profile)
    {
        std::cout << "New encoder settings:" << std::endl;
        print_profile_encoder_summary(*new_profile);
        delete new_profile;
    }

    if (duration > 0)
        wait_for_stop_or_timeout(duration);
}

int main(int argc, char *argv[])
{
    signal_utils::SignalHandler signal_handler(false);
    signal_handler.register_signal_handler([](int) {
        std::cout << "Stopping pipeline..." << std::endl;
        request_stop();
    });

    std::vector<std::string> available_profiles = load_and_print_profiles();
    if (available_profiles.empty())
        return 1;

    GstElement *pipeline = create_pipeline(argc, argv);
    if (!pipeline)
        return 1;

    GstElement *vision = start_pipeline(pipeline);
    if (!vision)
    {
        gst_object_unref(pipeline);
        return 1;
    }

    std::string default_profile = available_profiles[0];
    std::string alternate_profile = available_profiles.size() > 1 ? available_profiles[1] : "";

    int phase_duration = std::max(DEFAULT_TIMEOUT_SECONDS / 3, 1);
    int remaining_duration = DEFAULT_TIMEOUT_SECONDS - (phase_duration * 2);
    std::cout << "Running for " << DEFAULT_TIMEOUT_SECONDS << " seconds (" << phase_duration << "s per phase)..."
              << std::endl;

    run_phase_initial_recording(phase_duration);
    run_phase_override_bitrate(vision, phase_duration);
    run_phase_switch_profile(vision, default_profile, alternate_profile, remaining_duration);

    std::cout << std::endl << "Stopping pipeline..." << std::endl;
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(vision);
    gst_object_unref(pipeline);

    std::cout << "Done." << std::endl;
    return 0;
}
