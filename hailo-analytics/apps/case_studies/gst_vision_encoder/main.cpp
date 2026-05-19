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
 * @brief GStreamer pipeline using gsthailovision + gsthailoencoder with profile switching.
 *
 * Example pipeline (2 sinks):
 *
 *   gst-launch-1.0 gsthailovision name=v config-path=/path/to/medialib_config.json \
 *     v.sink0 ! queue ! gsthailoencoder stream-id=sink0 ! rtph264pay ! udpsink host=10.0.0.2 port=5000 \
 *     v.sink1 ! queue ! gsthailoencoder stream-id=sink1 ! rtph264pay ! udpsink host=10.0.0.2 port=5002
 */

#include <gst/gst.h>
#include <nlohmann/json.hpp>
#include <cxxopts/cxxopts.hpp>
#include <glib-object.h>
#include <glib.h>
#include <gst/gstparse.h>
#include <stddef.h>
#include <condition_variable>
#include <fstream> // IWYU pragma: keep
#include "media_library/cloexec_fstream.hpp"
#include <iostream>
#include <mutex>
#include <sstream> // IWYU pragma: keep
#include <string>
#include <vector>
#include <chrono>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <utility>

#include "media_library/signal_utils.hpp"
#include "hailo_analytics/utils/stream_utils.hpp"

#define HOST_IP "10.0.0.2"
#define NO_PROFILE_SELECTED ""
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/gst_example_medialib_config.json"

static std::string read_file(const std::string &path)
{
    cloexec::ifstream file(path);
    if (!file.is_open())
        return {};
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static std::string get_default_profile_config_path(const nlohmann::json &root)
{
    std::string default_profile = root.value("default_profile", "");
    for (const auto &profile : root["profiles"])
    {
        if (profile.value("name", "") == default_profile)
            return profile.value("config_file", "");
    }
    return {};
}

// Reads the default profile's config_file to extract encoded_output_streams[].stream_id values.
// These IDs determine the pad names on gsthailovision and the stream-id property on gsthailoencoder.
static std::vector<std::string> get_stream_ids_from_config(const std::string &config)
{
    auto root = nlohmann::json::parse(config, nullptr, false);
    if (root.is_discarded())
        return {};

    std::string profile_config_path = get_default_profile_config_path(root);
    if (profile_config_path.empty())
        return {};

    cloexec::ifstream profile_file(profile_config_path);
    if (!profile_file.is_open())
        return {};

    auto profile_json = nlohmann::json::parse(profile_file, nullptr, false);
    if (profile_json.is_discarded())
        return {};

    std::vector<std::string> stream_ids;
    for (const auto &stream : profile_json["encoded_output_streams"])
        stream_ids.push_back(stream.value("stream_id", ""));
    return stream_ids;
}

static std::vector<std::string> get_all_profile_names(const std::string &config)
{
    auto root = nlohmann::json::parse(config, nullptr, false);
    if (root.is_discarded() || !root.contains("profiles"))
        return {};
    std::vector<std::string> names;
    for (const auto &profile : root["profiles"])
        names.push_back(profile.value("name", ""));
    return names;
}

static std::string build_pipeline_string(const std::string &config_path, const std::vector<std::string> &stream_ids,
                                         const std::string &host)
{
    std::ostringstream pipeline_builder;
    pipeline_builder << "gsthailovision name=v config-path=" << config_path;

    for (const auto &id : stream_ids)
    {
        std::string port = hailo_analytics::utils::port_from_stream_id(id);

        pipeline_builder << " v." << id << " !"
                         << " queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 !"
                         << " gsthailoencoder stream-id=" << id << " !"
                         << " queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 !"
                         << " rtph264pay ! application/x-rtp,media=video,encoding-name=H264 !"
                         << " udpsink host=" << host << " port=" << port << " sync=false";
    }

    return pipeline_builder.str();
}

enum class ArgumentType
{
    Help,
    Timeout,
    Config,
    SwitchProfile,
    HostIP,
    Error
};

void print_help(const cxxopts::Options &options)
{
    std::cout << options.help() << std::endl;
}

cxxopts::Options build_arg_parser()
{
    // clang-format off
    cxxopts::Options options("GStreamer vision+encoder pipeline");
    options.add_options()
    ("h,help", "Show this help")
    ("t,timeout", "Time to run (seconds)",
        cxxopts::value<int>()->default_value("60"))
    ("c,config-file-path", "Media library configuration path",
        cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH))
    ("s,switch-profile", "Profile to switch to at halfway point",
        cxxopts::value<std::string>()->default_value(NO_PROFILE_SELECTED))
    ("o,host-ip", "Host IP address for UDP output",
        cxxopts::value<std::string>()->default_value(HOST_IP));
    // clang-format on

    return options;
}

std::vector<ArgumentType> handle_arguments(const cxxopts::ParseResult &result, const cxxopts::Options &options)
{
    std::vector<ArgumentType> arguments;

    if (result.count("help"))
    {
        print_help(options);
        arguments.push_back(ArgumentType::Help);
    }

    if (result.count("timeout"))
    {
        arguments.push_back(ArgumentType::Timeout);
    }

    if (result.count("config-file-path"))
    {
        arguments.push_back(ArgumentType::Config);
    }

    if (result.count("switch-profile"))
    {
        arguments.push_back(ArgumentType::SwitchProfile);
    }

    if (result.count("host-ip"))
    {
        arguments.push_back(ArgumentType::HostIP);
    }

    for (const auto &unrecognized : result.unmatched())
    {
        std::cerr << "Error: Unrecognized option or argument: " << unrecognized << std::endl;
        return {ArgumentType::Error};
    }

    return arguments;
}

struct AppResources
{
    std::string medialib_config_path = MEDIALIB_CONFIG_PATH;
    std::string switch_profile;
    std::string host_ip = HOST_IP;
    int timeout = 60;
};

static std::mutex g_stop_mutex;
static std::condition_variable g_stop_cv;
static bool g_stop_requested = false;

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
        {
            std::lock_guard<std::mutex> lock(g_stop_mutex);
            g_stop_requested = true;
        }
        g_stop_cv.notify_all();
        break;
    }
    case GST_MESSAGE_EOS:
        std::cout << "End of stream" << std::endl;
        {
            std::lock_guard<std::mutex> lock(g_stop_mutex);
            g_stop_requested = true;
        }
        g_stop_cv.notify_all();
        break;
    default:
        break;
    }
    return TRUE;
}

int main(int argc, char *argv[])
{
    auto app_resources = std::make_shared<AppResources>();

    signal_utils::SignalHandler signal_handler(false);
    signal_handler.register_signal_handler([]([[maybe_unused]] int signal) {
        std::cout << "Stopping Pipeline..." << std::endl;
        {
            std::lock_guard<std::mutex> lock(g_stop_mutex);
            g_stop_requested = true;
        }
        g_stop_cv.notify_all();
    });

    cxxopts::Options options = build_arg_parser();
    auto result = options.parse(argc, argv);
    std::vector<ArgumentType> argument_handling_results = handle_arguments(result, options);
    app_resources->timeout = result["timeout"].as<int>();

    for (ArgumentType argument : argument_handling_results)
    {
        switch (argument)
        {
        case ArgumentType::Help:
            return 0;
        case ArgumentType::Timeout:
            break;
        case ArgumentType::Config:
            app_resources->medialib_config_path = result["config-file-path"].as<std::string>();
            break;
        case ArgumentType::SwitchProfile:
            app_resources->switch_profile = result["switch-profile"].as<std::string>();
            break;
        case ArgumentType::HostIP:
            app_resources->host_ip = result["host-ip"].as<std::string>();
            break;
        case ArgumentType::Error:
            return 1;
        }
    }

    std::string config_string = read_file(app_resources->medialib_config_path);
    if (config_string.empty())
    {
        std::cerr << "Failed to read config: " << app_resources->medialib_config_path << std::endl;
        return 1;
    }

    std::vector<std::string> stream_ids = get_stream_ids_from_config(config_string);
    if (stream_ids.empty())
    {
        std::cerr << "No stream IDs found in config" << std::endl;
        return 1;
    }

    std::cout << "Streams from config:" << std::endl;
    for (const auto &id : stream_ids)
        std::cout << "  - " << id << std::endl;

    std::vector<std::string> available_profiles = get_all_profile_names(config_string);
    std::cout << "Available profiles:" << std::endl;
    for (const auto &name : available_profiles)
        std::cout << "  - " << name << std::endl;

    gst_init(&argc, &argv);

    std::string pipeline_str =
        build_pipeline_string(app_resources->medialib_config_path, stream_ids, app_resources->host_ip);
    std::cout << "Pipeline: " << pipeline_str << std::endl;

    GError *parse_error = nullptr;
    GstElement *pipeline = gst_parse_launch(pipeline_str.c_str(), &parse_error);
    if (!pipeline || parse_error)
    {
        std::cerr << "Failed to create pipeline: " << (parse_error ? parse_error->message : "unknown") << std::endl;
        if (parse_error)
            g_error_free(parse_error);
        return 1;
    }

    // Need a reference to the vision element for runtime profile switching
    GstElement *vision = gst_bin_get_by_name(GST_BIN(pipeline), "v");
    if (!vision)
    {
        std::cerr << "Failed to find gsthailovision element in pipeline" << std::endl;
        gst_object_unref(pipeline);
        return 1;
    }

    std::cout << "UDP output streams:" << std::endl;
    for (const auto &id : stream_ids)
    {
        std::string port = hailo_analytics::utils::port_from_stream_id(id);
        std::cout << "  - " << id << " -> " << app_resources->host_ip << ":" << port << std::endl;
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
        gst_object_unref(pipeline);
        return 1;
    }

    // Run for half the timeout, then switch profiles (if switch-profile given), then run the rest
    int half_timeout = app_resources->timeout / 2;

    std::cout << "Pipeline running for " << app_resources->timeout << " seconds..." << std::endl;

    {
        std::unique_lock<std::mutex> lock(g_stop_mutex);
        g_stop_cv.wait_for(lock, std::chrono::seconds(half_timeout), [] { return g_stop_requested; });
    }

    // Switch profile at the halfway point by setting the "profile-name" property on gsthailovision
    if (!g_stop_requested && !app_resources->switch_profile.empty())
    {
        std::cout << std::endl;
        std::cout << "=== Switching to profile: " << app_resources->switch_profile << " ===" << std::endl;
        std::cout << std::endl;
        g_object_set(vision, "profile-name", app_resources->switch_profile.c_str(), NULL);

        std::cout << "Profile switched successfully." << std::endl;
    }

    if (!g_stop_requested)
    {
        std::unique_lock<std::mutex> lock(g_stop_mutex);
        g_stop_cv.wait_for(lock, std::chrono::seconds(app_resources->timeout - half_timeout),
                           [] { return g_stop_requested; });
    }

    std::cout << "Stopping pipeline..." << std::endl;
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(vision);
    gst_object_unref(pipeline);

    std::cout << "Done." << std::endl;
    return 0;
}
