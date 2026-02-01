// general includes
#include <queue>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <tl/expected.hpp>
#include <cxxopts/cxxopts.hpp>

// medialibrary includes
#include "media_library/signal_utils.hpp"

// infra includes
#include "hailo_analytics/analytics/tiling.hpp"
#include "hailo_analytics/analytics/dynamic_privacy_mask.hpp"
#include "hailo_analytics/analytics/vision.hpp"
#include "hailo_analytics/analytics/analytic_metadata_sender.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/ai/lightweight_tracker_stage.hpp"
#include "hailo_analytics/pipeline/routing/callback_stage.hpp"
#include "hailo_analytics/pipeline/ai/analytics_db_stage.hpp"
#include "hailo_analytics/pipeline/cropping/sync_aggregator_stage.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// defines
#define VISION_PIPELINE "vision_pipeline"
#define TILING_PIPELINE "tiling_pipeline"
#define LIGHTWEIGHT_TRACKER_PIPELINE "lightweight_tracker_pipeline"
#define DETECTION_LIMITER_PIPELINE "detection_limiter_pipeline"
#define DPM_PIPELINE "dpm_pipeline"
#define ANALYTICS_DB_STAGE "analytics_db_stage"
#define SYNC_AGGREGATOR_STAGE "sync_aggregator_stage"
#define ANALYTIC_META_SENDER_PIPELINE "analytic_metadata_sender_pipeline"
#define APP_NAME "dynamic_privacy_mask_app"
#define HOST_IP "10.0.0.2"
#define VISION_SINK "sink0"
#define AI_SINK "sink2"
#define NO_PROFILE_SELECTED ""
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/case_studies/dynamic_privacy_mask_medialib_config.json"
#define ANALYTICS_DATA_ID "semantic_segmentation"

// Detection AI Params
#define YOLO_HEF_FILE "/home/root/apps/ai_example_app/resources/hailo_yolov8n_384_640.hef"
// Detection Postprocess Params
#define YOLO_POST_SO "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so"
#define YOLO_FUNC_NAME "hailo_yolov8n"
#define YOLO_POST_CONF "/home/root/apps/webserver/resources/configs/yolov5_personface.json"

// Segmentation HEF Param
#define DPM_SEG_HEF_FILE "/home/root/apps/dynamic_privacy_mask/resources/linknet_mbv1_ss_dpm_128.hef"
#define SEGMENTED_LABELS_DEFAULT "person,vehicle"
#define MAX_DETECTION_SEGMENTATION 35

enum class ArgumentType
{
    Help,
    PrintFPS,
    Timeout,
    Config,
    Profile,
    HostIP,
    HefPath,
    SegmentLabels,
    MaxDetections,
    Error
};

void print_help(const cxxopts::Options &options)
{
    std::cout << options.help() << std::endl;
}

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
    ("e,hef-path", "Segmentation HEF file path", 
        cxxopts::value<std::string>()->default_value(DPM_SEG_HEF_FILE))
    ("s,segment-labels", "Comma-separated list of labels to segment (e.g., 'person,face,vehicle')", 
        cxxopts::value<std::string>()->default_value(SEGMENTED_LABELS_DEFAULT))
    ("n,max-detections", "Maximum number of detections to process per frame", 
        cxxopts::value<int>()->default_value(std::to_string(MAX_DETECTION_SEGMENTATION)));
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

    if (result.count("print-fps"))
    {
        arguments.push_back(ArgumentType::PrintFPS);
    }

    if (result.count("timeout"))
    {
        arguments.push_back(ArgumentType::Timeout);
    }

    if (result.count("config-file-path"))
    {
        arguments.push_back(ArgumentType::Config);
    }

    if (result.count("profile"))
    {
        arguments.push_back(ArgumentType::Profile);
    }

    if (result.count("host-ip"))
    {
        arguments.push_back(ArgumentType::HostIP);
    }

    if (result.count("hef-path"))
    {
        arguments.push_back(ArgumentType::HefPath);
    }

    if (result.count("segment-labels"))
    {
        arguments.push_back(ArgumentType::SegmentLabels);
    }

    if (result.count("max-detections"))
    {
        arguments.push_back(ArgumentType::MaxDetections);
    }

    // Handle unrecognized options
    for (const auto &unrecognized : result.unmatched())
    {
        std::cerr << "Error: Unrecognized option or argument: " << unrecognized << std::endl;
        return {ArgumentType::Error};
    }

    return arguments;
}

struct AppResources
{
    std::shared_ptr<MediaLibrary> media_library;
    hailo_analytics::pipeline::PipelinePtr pipeline;
    bool print_fps;
    std::string medialib_config_path;
    std::string profile_name;
    std::string host_ip = HOST_IP;
    std::string hef_path = DPM_SEG_HEF_FILE;
    int mask_size = 128;
    std::string segment_labels = "person,vehicle";
    int max_detections_per_frame = 35;
};

std::string read_string_from_file(const char *file_path)
{
    std::ifstream file_to_read;
    file_to_read.open(file_path);
    if (!file_to_read.is_open())
        throw std::runtime_error(std::string("config path (") + file_path + ") is not valid");
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)), std::istreambuf_iterator<char>());
    file_to_read.close();
    std::cout << "Read config from file: " << file_path << std::endl;
    return file_string;
}

/**
 * @brief Configure the media library for the application.
 *
 * This function initializes the media library with configuration from file.
 *
 * @param app_resources Shared pointer to the application's resources.
 */
void configure_media_library(std::shared_ptr<AppResources> app_resources)
{
    std::string medialib_config_string = read_string_from_file(app_resources->medialib_config_path.c_str());
    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library" << std::endl;
        throw std::runtime_error("Failed to create media library");
    }
    app_resources->media_library = media_lib_expected.value();
    if (app_resources->media_library->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        throw std::runtime_error("Failed to initialize media library");
    }
    if (app_resources->profile_name != NO_PROFILE_SELECTED)
    {
        app_resources->media_library->set_profile(app_resources->profile_name);
    }
}

/**
 * @brief Create and configure the application's processing pipeline.
 *
 * This function sets up the application's processing pipeline using the analytics generators:
 * - Vision pipeline for output encoding and streaming
 * - Tiling detection pipeline for object detection
 * - Dynamic privacy mask pipeline for segmentation of detected objects
 * - Analytic metadata sender for sending results
 *
 * @param app_resources Shared pointer to the application's resources, which includes the pipeline object.
 */
void create_pipeline(std::shared_ptr<AppResources> app_resources)
{
    // Get output streams from frontend
    auto output_streams = app_resources->media_library->m_frontend->get_outputs_streams();
    if (!output_streams.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get stream ids");
        throw std::runtime_error("Failed to get stream ids");
    }

    // Generate vision pipeline (frontend + encoders + UDP outputs)
    auto vision_config = hailo_analytics::analytics::vision::base_vision_config(output_streams.value());
    vision_config.outputs.erase(AI_SINK);
    vision_config.outputs[VISION_SINK].udp_config.host = app_resources->host_ip;
    // We erase the AI_SINK to override the vision pipeline from automatically generating
    // and connecting frontend outputs to encoders for the AI sink
    auto vision_pipeline_status = hailo_analytics::analytics::vision::generate_vision_pipeline(
        app_resources->media_library, VISION_PIPELINE, vision_config);
    if (!vision_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create vision pipeline");
        throw std::runtime_error("Failed to create vision pipeline");
    }
    hailo_analytics::pipeline::PipelinePtr vision_pipeline = vision_pipeline_status.value();

    // Get the input resolution from frontend for dynamic configuration
    int bbox_crop_input_width = 0;
    int bbox_crop_input_height = 0;
    int tiling_input_width = 0;
    int tiling_input_height = 0;

    for (const auto &stream : output_streams.value())
    {
        if (stream.id == VISION_SINK)
        {
            bbox_crop_input_width = stream.width;
            bbox_crop_input_height = stream.height;
        }
        else if (stream.id == AI_SINK)
        {
            tiling_input_width = stream.width;
            tiling_input_height = stream.height;
        }
    }

    if (bbox_crop_input_width == 0 || bbox_crop_input_height == 0)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get vision input resolution from frontend");
        throw std::runtime_error("Failed to get vision input resolution from frontend");
    }

    if (tiling_input_width == 0 || tiling_input_height == 0)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get AI pipeline resolution from frontend");
        throw std::runtime_error("Failed to get AI pipeline resolution from frontend");
    }

    // Generate tiling detection pipeline (tiling + detection AI + postprocess + aggregation)
    hailo_analytics::analytics::tiling::tiling_detection_config_t tiling_detection_config;
    tiling_detection_config.detection_config.ai_config.hef_path = YOLO_HEF_FILE;
    tiling_detection_config.detection_config.ai_config.output_pool_size = 100;
    tiling_detection_config.detection_config.post_config.so_path = YOLO_POST_SO;
    tiling_detection_config.detection_config.post_config.function_name = YOLO_FUNC_NAME;
    tiling_detection_config.detection_config.post_config.config_path = YOLO_POST_CONF;
    tiling_detection_config.tiling_config.crop_every_x_frames = 1;

    // Set dynamic tiling dimensions from frontend
    tiling_detection_config.tiling_config.input_width = tiling_input_width;
    tiling_detection_config.tiling_config.input_height = tiling_input_height;

    auto tiling_pipeline_status = hailo_analytics::analytics::tiling::generate_tiling_detection_pipeline(
        TILING_PIPELINE, tiling_detection_config);
    if (!tiling_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create tiling pipeline");
        throw std::runtime_error("Failed to create tiling pipeline");
    }
    hailo_analytics::pipeline::PipelinePtr tiling_pipeline = tiling_pipeline_status.value();

    // Lightweight Tracker Stage
    std::shared_ptr<hailo_analytics::pipeline::ai::LightweightTrackerStage> tracker_stage =
        hailo_analytics::pipeline::ai::LightweightTrackerStageBuild::create()
            .set_stage_name(LIGHTWEIGHT_TRACKER_PIPELINE)
            .set_queue_size_opt(1)
            .set_leaky_opt(false)
            .set_trace_opt(false)
            .set_classification_ids({1, 2, 3, 4})
            .set_add_tracking_id(false)
            .set_grace_period(4)
            .set_smooth_alpha(0.5f)
            .set_weighted_average_decay(0.4f)
            .set_copy_nested_objects(false, 1)
            .set_copy_nested_objects(true, 2)
            .buildptr();

    // Parse segment labels for the limiter (same as DPM config)
    std::vector<std::string> segment_labels_vec;
    if (!app_resources->segment_labels.empty())
    {
        std::stringstream ss(app_resources->segment_labels);
        std::string token;
        while (std::getline(ss, token, ','))
        {
            token.erase(0, token.find_first_not_of(" \\t"));
            token.erase(token.find_last_not_of(" \\t") + 1);
            if (!token.empty())
            {
                segment_labels_vec.push_back(token);
            }
        }
    }

    // Add detection limiter callback to limit the number of detections per frame
    hailo_analytics::pipeline::PipelinePtr limiter_pipeline = nullptr;
    int max_detections = app_resources->max_detections_per_frame;
    auto limiter_callback = [max_detections, segment_labels_vec](hailo_analytics::pipeline::BufferPtr data) {
        auto roi = data->get_roi();
        if (!roi)
            return;

        auto all_detections = hailo_common::get_hailo_detections(roi);
        std::vector<HailoDetectionPtr> relevant_detections;

        std::map<std::string, size_t> label_counts;
        for (auto detection : all_detections)
        {
            std::string label = detection->get_label();
            label_counts[label]++;

            if (std::find(segment_labels_vec.begin(), segment_labels_vec.end(), label) != segment_labels_vec.end())
            {
                relevant_detections.push_back(detection);
            }
        }

        size_t removed_count = 0;
        if (relevant_detections.size() > static_cast<size_t>(max_detections))
        {
            std::vector<HailoDetectionPtr> detections_to_remove;
            auto it = relevant_detections.begin();
            std::advance(it, max_detections);
            for (; it != relevant_detections.end(); ++it)
            {
                detections_to_remove.push_back(*it);
            }

            removed_count = detections_to_remove.size();
            for (auto detection : detections_to_remove)
            {
                roi->remove_object(detection);
            }
        }

        std::string label_summary;
        for (const auto &[label, count] : label_counts)
        {
            if (!label_summary.empty())
                label_summary += ", ";
            label_summary += label + ":" + std::to_string(count);
        }

        HAILO_ANALYTICS_LOG_TRACE("[DPM] Total detections: {}, Passed to segmentation: {}, Removed: {}, Labels: [{}]",
                                  all_detections.size(), relevant_detections.size() - removed_count, removed_count,
                                  label_summary);
    };

    // Create callback stage for detection limiting
    auto limiter_stage = hailo_analytics::pipeline::routing::CallbackStageBuild::create()
                             .set_stage_name("detection_limiter")
                             .set_queue_size_opt(5)
                             .set_leaky_opt(false)
                             .buildptr();
    limiter_stage->set_callback(limiter_callback);

    hailo_analytics::pipeline::PipelineBuilder limiter_builder;
    limiter_builder.add_stage(limiter_stage);
    limiter_pipeline = limiter_builder.build(DETECTION_LIMITER_PIPELINE, true);
    limiter_pipeline->set_in_stage(limiter_stage);
    limiter_pipeline->set_out_stage(limiter_stage);

    // Generate dynamic privacy mask pipeline (bbox crop + segmentation AI + aggregation)
    hailo_analytics::analytics::dynamic_privacy_mask::bbox_crop_segmentation_config_t dpm_config;
    dpm_config.segmentation_config.ai_config.hef_path = app_resources->hef_path;

    // Configure bbox crop input and output dimensions
    // BBox crop receives images from AI_SINK (through tiling pipeline), so use AI_SINK dimensions
    dpm_config.bbox_crop_config.input_width = tiling_input_width;
    dpm_config.bbox_crop_config.input_height = tiling_input_height;
    dpm_config.bbox_crop_config.output_width = app_resources->mask_size;
    dpm_config.bbox_crop_config.output_height = app_resources->mask_size;

    // Use the same segment labels that were parsed earlier for the limiter
    dpm_config.bbox_crop_config.labels = segment_labels_vec;

    auto dpm_pipeline_status = hailo_analytics::analytics::dynamic_privacy_mask::generate_dynamic_privacy_mask_pipeline(
        DPM_PIPELINE, dpm_config);
    if (!dpm_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create dynamic privacy mask pipeline");
        throw std::runtime_error("Failed to create dynamic privacy mask pipeline");
    }
    hailo_analytics::pipeline::PipelinePtr dpm_pipeline = dpm_pipeline_status.value();

    // Generate analytics database stage for semantic segmentation
    // This stores segmentation results for the privacy mask system to query
    std::shared_ptr<hailo_analytics::pipeline::ai::AnalyticsDBStage> analytics_db_stage =
        hailo_analytics::pipeline::ai::AnalyticsDBStageBuild::create()
            .set_stage_name(ANALYTICS_DB_STAGE)
            .set_analytics_data_id(ANALYTICS_DATA_ID)
            .set_type(AnalyticsType::SEMANTIC_SEGMENTATION)
            .set_queue_size(3)
            .set_leaky_opt(true)
            .buildptr();

    // Generate sync aggregator stage to synchronize vision encoder with analytics DB
    std::shared_ptr<hailo_analytics::pipeline::cropping::SyncAggregatorStage> sync_aggregator_stage =
        hailo_analytics::pipeline::cropping::SyncAggregatorStageBuild::create()
            .set_stage_name(SYNC_AGGREGATOR_STAGE)
            .set_main_inlet_name(VISION_PIPELINE)
            .set_main_queue_size(3)
            .set_main_leaky(false)
            .set_sub_inlet_name(ANALYTICS_DB_STAGE)
            .set_sub_queue_size(3)
            .set_sub_leaky(true)
            .set_timeout_opt(std::chrono::milliseconds(100))
            .set_trace_opt(true)
            .buildptr();

    // Generate analytic metadata sender pipeline
    auto analytic_metadata_sender_pipeline_status =
        hailo_analytics::analytics::analytic_metadata_sender::generate_analytic_metadata_sender_pipeline(
            ANALYTIC_META_SENDER_PIPELINE);
    if (!analytic_metadata_sender_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create analytic metadata sender pipeline");
        throw std::runtime_error("Failed to create analytic metadata sender pipeline");
    }
    hailo_analytics::pipeline::PipelinePtr analytic_metadata_sender_pipeline =
        analytic_metadata_sender_pipeline_status.value();

    hailo_analytics::pipeline::PipelineBuilder pip_builder;
    pip_builder.add_stage(vision_pipeline, hailo_analytics::pipeline::StageType::SOURCE).add_stage(tiling_pipeline);
    // Add detection limiter pipeline
    pip_builder.add_stage(limiter_pipeline);

    pip_builder.add_stage(dpm_pipeline)
        .add_stage(analytics_db_stage)
        .add_stage(sync_aggregator_stage)
        .add_stage(analytic_metadata_sender_pipeline, hailo_analytics::pipeline::StageType::SINK);

    // Connect pipelines
    pip_builder.connect_frontend(VISION_PIPELINE, AI_SINK, TILING_PIPELINE);

    pip_builder.connect(TILING_PIPELINE, DETECTION_LIMITER_PIPELINE);
    pip_builder.connect(DETECTION_LIMITER_PIPELINE, DPM_PIPELINE);
    pip_builder.connect(DPM_PIPELINE, ANALYTICS_DB_STAGE);
    pip_builder.connect_frontend(VISION_PIPELINE, VISION_SINK, SYNC_AGGREGATOR_STAGE);
    pip_builder.connect(ANALYTICS_DB_STAGE, SYNC_AGGREGATOR_STAGE);
    pip_builder.connect(SYNC_AGGREGATOR_STAGE, ANALYTIC_META_SENDER_PIPELINE);

    app_resources->pipeline = pip_builder.build(APP_NAME, true);
}

/**
 * @brief Main function to initialize and run the application.
 *
 * This function sets up the application resources, registers a signal handler for SIGINT,
 * parses user arguments, configures the media library, creates the pipeline,
 * starts the pipeline, waits for a specified timeout, and then stops the pipeline.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return int Exit status of the application.
 */
std::mutex g_stop_mutex;
std::condition_variable g_stop_cv;
int main(int argc, char *argv[])
{
    // App resources
    std::shared_ptr<AppResources> app_resources = std::make_shared<AppResources>();
    app_resources->medialib_config_path = MEDIALIB_CONFIG_PATH;

    // register signal SIGINT and signal handler
    signal_utils::SignalHandler signal_handler(false);
    signal_handler.register_signal_handler([]([[maybe_unused]] int signal) {
        std::cout << "Stopping Pipeline..." << std::endl;
        HAILO_ANALYTICS_LOG_INFO("Stopping Pipeline...");
        g_stop_cv.notify_all();
    });

    // Parse user arguments
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
        case ArgumentType::HefPath:
            app_resources->hef_path = result["hef-path"].as<std::string>();
            break;
        case ArgumentType::SegmentLabels:
            app_resources->segment_labels = result["segment-labels"].as<std::string>();
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

    // Configure media library
    configure_media_library(app_resources);

    // Create pipeline and stages
    create_pipeline(app_resources);

    // Start pipeline
    std::cout << "Starting." << std::endl;
    HAILO_ANALYTICS_LOG_INFO("Starting.");
    app_resources->pipeline->start();

    HAILO_ANALYTICS_LOG_INFO("Started playing for {} seconds.", timeout);

    // Wait for either timeout or signal
    std::unique_lock<std::mutex> lk(g_stop_mutex);
    g_stop_cv.wait_for(lk, std::chrono::seconds(timeout));

    // Stop pipeline
    std::cout << "Stopping." << std::endl;
    HAILO_ANALYTICS_LOG_INFO("Stopping.");
    app_resources->pipeline->stop();
    return 0;
}
