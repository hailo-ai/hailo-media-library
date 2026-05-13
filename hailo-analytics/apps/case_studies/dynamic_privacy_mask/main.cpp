#include "dpm_common.hpp"

enum class ArgumentType
{
    Help,
    PrintFPS,
    Timeout,
    Config,
    Profile,
    HostIP,
    UdpPort,
    ZmqPort,
    HefPath,
    SegmentLabels,
    MaxDetections,
    Error
};

cxxopts::Options build_arg_parser()
{
    // clang-format off
    cxxopts::Options options("Dynamic Privacy Mask app");
    options.add_options()
    ("h,help", "Show this help")
    ("t,timeout", "Time to run",
        cxxopts::value<int>()->default_value("60"))
    ("p,print-fps", "Print FPS",
        cxxopts::value<bool>()->default_value("false"))
    ("c,config-file-path", "Media library configuration path",
        cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH))
    ("a,profile", "Profile name",
        cxxopts::value<std::string>()->default_value(NO_PROFILE_SELECTED))
    ("o,host-ip", "Host IP address for UDP output",
        cxxopts::value<std::string>()->default_value(HOST_IP))
    ("u,udp-port", "UDP output port (default: 5000)",
        cxxopts::value<std::string>()->default_value(""))
    ("z,zmq-port", "ZMQ publisher port (default: 7000)",
        cxxopts::value<std::string>()->default_value(""))
    ("e,hef-path", "Segmentation HEF file path",
        cxxopts::value<std::string>()->default_value(std::string(DEFAULT_DPM_SEG_HEF)))
    ("s,segment-labels", "Comma-separated list of labels to segment (e.g., 'person,face,vehicle')",
        cxxopts::value<std::string>()->default_value(SEGMENTED_LABELS_DEFAULT))
    ("n,max-detections", "Maximum number of detections to process per frame",
        cxxopts::value<int>()->default_value(std::to_string(default_max_detections())));
    // clang-format on

    return options;
}

std::vector<ArgumentType> handle_arguments(const cxxopts::ParseResult &result, const cxxopts::Options &options)
{
    std::vector<ArgumentType> arguments;

    if (result.count("help"))
    {
        std::cout << options.help() << std::endl;
        arguments.push_back(ArgumentType::Help);
    }

    if (result.count("print-fps"))
        arguments.push_back(ArgumentType::PrintFPS);
    if (result.count("timeout"))
        arguments.push_back(ArgumentType::Timeout);
    if (result.count("config-file-path"))
        arguments.push_back(ArgumentType::Config);
    if (result.count("profile"))
        arguments.push_back(ArgumentType::Profile);
    if (result.count("host-ip"))
        arguments.push_back(ArgumentType::HostIP);
    if (result.count("udp-port") && !result["udp-port"].as<std::string>().empty())
        arguments.push_back(ArgumentType::UdpPort);
    if (result.count("zmq-port") && !result["zmq-port"].as<std::string>().empty())
        arguments.push_back(ArgumentType::ZmqPort);
    if (result.count("hef-path"))
        arguments.push_back(ArgumentType::HefPath);
    if (result.count("segment-labels"))
        arguments.push_back(ArgumentType::SegmentLabels);
    if (result.count("max-detections"))
        arguments.push_back(ArgumentType::MaxDetections);

    for (const auto &unrecognized : result.unmatched())
    {
        std::cerr << "Error: Unrecognized option or argument: " << unrecognized << std::endl;
        return {ArgumentType::Error};
    }

    return arguments;
}

void create_pipeline(std::shared_ptr<AppResources> app_resources)
{
    auto output_streams = app_resources->media_library->get_frontend_output_streams();
    if (!output_streams.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get stream ids");
        throw std::runtime_error("Failed to get stream ids");
    }

    auto vision_pipeline = create_vision_pipeline(app_resources, output_streams.value());
    assemble_dpm_pipeline(app_resources, vision_pipeline);
}

std::mutex g_stop_mutex;
std::condition_variable g_stop_cv;
int main(int argc, char *argv[])
{
    std::shared_ptr<AppResources> app_resources = std::make_shared<AppResources>();
    app_resources->medialib_config_path = MEDIALIB_CONFIG_PATH;
    app_resources->segment_labels = parse_segment_labels(SEGMENTED_LABELS_DEFAULT);

    signal_utils::SignalHandler signal_handler(false);
    signal_handler.register_signal_handler([](int /*signal*/) {
        std::cout << "Stopping Pipeline..." << std::endl;
        HAILO_ANALYTICS_LOG_INFO("Stopping Pipeline...");
        g_stop_cv.notify_all();
    });

    cxxopts::Options options = build_arg_parser();
    auto result = options.parse(argc, argv);
    std::vector<ArgumentType> argument_handling_results = handle_arguments(result, options);
    int timeout = result["timeout"].as<int>();

    for (ArgumentType argument : argument_handling_results)
    {
        switch (argument)
        {
        case ArgumentType::Help:
            return 0;
        case ArgumentType::Timeout:
            break;
        case ArgumentType::PrintFPS:
            app_resources->print_fps = true;
            break;
        case ArgumentType::Config:
            app_resources->medialib_config_path = result["config-file-path"].as<std::string>();
            break;
        case ArgumentType::Profile:
            app_resources->profile_name = result["profile"].as<std::string>();
            break;
        case ArgumentType::HostIP:
            app_resources->host_ip = result["host-ip"].as<std::string>();
            break;
        case ArgumentType::UdpPort:
            app_resources->udp_port = result["udp-port"].as<std::string>();
            break;
        case ArgumentType::ZmqPort:
            app_resources->zmq_port = result["zmq-port"].as<std::string>();
            break;
        case ArgumentType::HefPath:
            app_resources->hef_path = result["hef-path"].as<std::string>();
            break;
        case ArgumentType::SegmentLabels:
            app_resources->segment_labels = parse_segment_labels(result["segment-labels"].as<std::string>());
            break;
        case ArgumentType::MaxDetections:
            app_resources->max_detections_per_frame = result["max-detections"].as<int>();
            if (app_resources->max_detections_per_frame < 1)
            {
                std::cerr << "Error: --max-detections must be at least 1" << std::endl;
                return 1;
            }
            break;
        case ArgumentType::Error:
            return 1;
        }
    }

    std::string config_string = read_string_from_file(app_resources->medialib_config_path.c_str());
    configure_media_library(app_resources, config_string);
    create_pipeline(app_resources);

    std::cout << "Starting." << std::endl;
    HAILO_ANALYTICS_LOG_INFO("Starting.");
    app_resources->pipeline->start();

    HAILO_ANALYTICS_LOG_INFO("Started playing for {} seconds.", timeout);
    std::unique_lock<std::mutex> lk(g_stop_mutex);
    g_stop_cv.wait_for(lk, std::chrono::seconds(timeout));

    std::cout << "Stopping." << std::endl;
    HAILO_ANALYTICS_LOG_INFO("Stopping.");
    app_resources->pipeline->stop();
    return 0;
}
