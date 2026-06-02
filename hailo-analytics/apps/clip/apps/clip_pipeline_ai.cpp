#include "clip_pipeline_ai.hpp"

#include <media_library/frontend.hpp>
#include <media_library/media_library_types.hpp>
#include <unordered_map>

#include "hailo_analytics/analytics/ai_models_config.hpp"
#include "pipeline/thumb_storage_stage.hpp"
#include "pipeline/faiss_storage_stage.hpp"
#include "pipeline/cache_stage.hpp"
#include "pipeline/clip_image_preprocess.hpp"
#include "pipeline/video_storage_stage.hpp"
#include "pipeline/full_frame_bbox_injector_stage.hpp"
#include "database_manager.hpp"
#include "hailo_analytics/pipeline/ai/analytics_db_stage.hpp"
#include "hailo_postprocess_tools/labels/hailo_yolov8n.hpp"
#include "media_library/analytics_db.hpp"
#include "service/query_service/query_service_ext.hpp"
#include "service/query_service/clip_text_encoder.hpp"
#include "streaming/webrtc_streamer_ext.hpp"
#include "service/player_service_ext.hpp"
#include "service/storage_monitor_service_ext.hpp"
#include "service/storage_cleanup_service_ext.hpp"
#include "service/storage_cleanup_strategy.hpp"
#include "service/app_control_service_ext.hpp"
#include "clip_pipeline_ai_defines.hpp"
#include "common_utils.hpp"
#include "database.hpp"
#include "faiss_partitioned.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "sql_factory.hpp"

namespace
{
constexpr const char *DETECTIONS_DATA_ID = "detections";
constexpr const char *DETECTIONS_DB_STAGE = "detections_db";

void register_detections_db_config(int width, int height)
{
    detection_analytics_config_t detection_config;
    detection_config.analytics_data_id = DETECTIONS_DATA_ID;
    detection_config.scaling_mode = ScalingMode::STRETCH;
    detection_config.width = width;
    detection_config.height = height;
    detection_config.original_width_ratio = width;
    detection_config.original_height_ratio = height;
    detection_config.max_entries = 100;
    for (const auto &[id, name] : ::common::hailo_yolov8n)
        detection_config.labels.push_back({.label = name, .id = id});

    application_analytics_config_t application_config;
    application_config.detection_analytics_config[DETECTIONS_DATA_ID] = detection_config;
    AnalyticsDB::instance().add_configuration(application_config);
}
} // namespace

ClipAppCustomData::ClipAppCustomData(
    const ClipAppConfig::ImageEncoders &encoders, const ClipAppConfig::PipelineConfig &pipeline_config,
    const ClipAppConfig::HailortDeviceConfig &hailort_device_config,
    const ClipAppConfig::StorageConfiguration &storage_config,
    const std::vector<ClipAppConfig::TextEncoder> &clip_text_encoder_support_list,
    const ClipAppConfig::FaissConfig &faiss_test_config,
    std::shared_ptr<hailo_analytics::pipeline::Stage> main_sink_output_stage,
    std::shared_ptr<hailo_analytics::pipeline::sinks::RTPConverterStage::RTPReceiver> query_playback_rtp_receiver,
    bool skip_detections_overlays_drawing)
    : m_clip_image_encoders(encoders), m_pipeline_config(pipeline_config),
      m_hailort_device_config(hailort_device_config), m_storage_config(storage_config),
      m_clip_text_encoder_support_list(clip_text_encoder_support_list), m_faiss_test_config(faiss_test_config),
      m_main_sink_output_stage(main_sink_output_stage), m_query_playback_rtp_receiver(query_playback_rtp_receiver),
      m_skip_detections_overlays_drawing(skip_detections_overlays_drawing)
{
}

const char *ClipAppCustomData::type_name() const
{
    return "ClipAppCustomData";
}

ClipVideoPipeline::~ClipVideoPipeline()
{
    m_udp_outputs.clear();
}

bool ClipVideoPipeline::is_supported(const ClipAppConfig::StorageConfiguration &storage_config)
{
    // Check system memory requirement if saving clip data to memory
    if (storage_config.mount_location.find(app::paths::clip_storage_mount_point) != std::string::npos)
    {
        if (SystemUtils::getTotalMemoryGB() < app::storage::save_to_memory_min_gb)
        {
            std::cout << "Warning: Your system memory is less than 3GB. "
                         "The application by default saved clip data to memory and require at least 3GB, "
                         "You can still use clip app on this system but you will need to change the clip data "
                         "storage path to SD card, SD card minimum requirement is A2 class, for instruction "
                         "please follow README.rst from this app"
                      << std::endl;
            return false;
        }
    }
    return true;
}

hailo_analytics::analytics::app_constructor::CamAppReturnCode ClipVideoPipeline::register_app_extensions(
    std::shared_ptr<hailo_analytics::analytics::app_constructor::UserDataBase> user_data)
{
    auto app_custom_data = std::dynamic_pointer_cast<ClipAppCustomData>(user_data);
    if (!app_custom_data)
    {
        std::cerr << "ClipAppCustomData is not set in ClipVideoPipeline, its expected in this app" << std::endl;
        HAILO_ANALYTICS_LOG_ERROR(
            "{} failed: ClipAppCustomData is not set in ClipVideoPipeline, its expected in this app", __func__);
        return hailo_analytics::analytics::app_constructor::CamAppReturnCode::APP_EXTENSION_REGITRATION_FAILED;
    }
    m_app_custom_data = app_custom_data;

    /*
        Register and initialize/configure StorageMonitorService as app extensions
    */
    auto storage_config = StorageMonitorServiceExt::Config{
        .mount_location = app_custom_data->m_storage_config.mount_location,
        .root_directory = app_custom_data->m_storage_config.root_directory,
        .database_directory = app_custom_data->m_storage_config.database_directory,
        .faissdb_directory = app_custom_data->m_storage_config.faissdb_directory,
        .thumbnail_directory = app_custom_data->m_storage_config.thumbnail_directory,
        .video_directory = app_custom_data->m_storage_config.video_directory,
        .low_disk_threshold_percent = app_custom_data->m_storage_config.low_disk_threshold_percent,
        .check_interval_seconds = app_custom_data->m_storage_config.check_interval_seconds};

    register_extension(std::make_shared<StorageMonitorServiceExt>());
    auto storage_monitor_service_ext = get_extension<StorageMonitorServiceExt>();

    // Configure and start storage monitor service
    auto storage_result = storage_monitor_service_ext->configure(storage_config);
    if (!storage_result)
    {
        std::cerr << "Failed to configure StorageMonitorServiceExt:" << static_cast<int>(storage_result.error())
                  << std::endl;
        HAILO_ANALYTICS_LOG_ERROR("{} failed: Failed to configure StorageMonitorServiceExt: {}", __func__,
                                  static_cast<int>(storage_result.error()));
        return hailo_analytics::analytics::app_constructor::CamAppReturnCode::APP_EXTENSION_REGITRATION_FAILED;
    }

    storage_monitor_service_ext->start();

    /*
        Initialize and Create our database manager
    */
    std::string sql_db_file_path = FileSysUtils::join_path_and_file_name(
        storage_monitor_service_ext->get_sqldatabase_directory().value(), app::storage::clip_database_file);
    DatabaseManagerConfig database_config(sql_db_file_path,
                                          storage_monitor_service_ext->get_faissdb_directory().value());

    database_config.add_all_common_sql_factories();

    for (auto image_encoder : app_custom_data->m_clip_image_encoders.encoders)
    {
        database_config.add_faiss_factory(image_encoder.id, image_encoder.embedding_size);
    }

    // Initialize the manager
    auto db_manager_result = DatabaseManagerHelper::initialize(database_config);
    if (!db_manager_result)
    {
        std::cerr << "Failed to initialize DatabaseManager: " << db_manager_result.error().message << std::endl;
        HAILO_ANALYTICS_LOG_ERROR("{} failed: Failed to initialize DatabaseManager: {}", __func__,
                                  db_manager_result.error().message);
    }

    // Create/Initialize other misc configs
    faiss_index_misc_config(app_custom_data);

    /*
        Register and initialize/configure StorageCleanupService as app extensions
    */
    register_extension(std::make_shared<StorageCleanupServiceExt>());
    auto storage_cleanup_service_ext = get_extension<StorageCleanupServiceExt>();

    // Create dedicated cleanup database connections so the cleanup thread
    // doesn't share sqlite3 connections with pipeline stage threads to improve concurrency access
    static const std::string FAISS_CLEANUP_RW = "faiss_cleanup_rw";
    static const std::string THUMBNAIL_CLEANUP_RW = "thumbnail_cleanup_rw";
    static const std::string VIDEO_CLEANUP_RW = "video_cleanup_rw";

    auto faiss_cleanup_result = SqlDatabaseQuickAccess::get_or_create_database(
        FAISS_CLEANUP_RW,
        DatabaseConfig(DatabaseConfig::FAISS_TABLE, sql_db_file_path, Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE));
    auto thumb_cleanup_result = SqlDatabaseQuickAccess::get_or_create_database(
        THUMBNAIL_CLEANUP_RW, DatabaseConfig(DatabaseConfig::THUMBNAIL_TABLE, sql_db_file_path,
                                             Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE));
    auto video_cleanup_result = SqlDatabaseQuickAccess::get_or_create_database(
        VIDEO_CLEANUP_RW,
        DatabaseConfig(DatabaseConfig::VIDEO_TABLE, sql_db_file_path, Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE));

    if (!faiss_cleanup_result || !thumb_cleanup_result || !video_cleanup_result)
    {
        HAILO_ANALYTICS_LOG_ERROR("{} failed: Failed to create dedicated cleanup database connections", __func__);
        return CamAppReturnCode::APP_EXTENSION_REGITRATION_FAILED;
    }

    // Initialize Storage CleanupService with dedicated cleanup connections
    auto cleanup_db_config =
        StorageCleanupServiceExt::DatabaseConfig(FAISS_CLEANUP_RW, THUMBNAIL_CLEANUP_RW, VIDEO_CLEANUP_RW);

    auto storage_cleanup_result = storage_cleanup_service_ext->initialize(
        std::make_unique<FaissShardFirstCleanupStrategy>(10.0f), cleanup_db_config);
    if (!storage_cleanup_result)
    {
        std::cerr << "Failed to initialize StorageCleanupServiceExt: " << storage_cleanup_result.error() << std::endl;
        HAILO_ANALYTICS_LOG_ERROR("{} failed: Failed to initialize StorageCleanupServiceExt: {}", __func__,
                                  storage_cleanup_result.error());
        return CamAppReturnCode::APP_EXTENSION_REGITRATION_FAILED;
    }

    // Register StorageCleanupService as listener to StorageMonitorService
    storage_monitor_service_ext->add_listener(storage_cleanup_service_ext);

    if (m_app_custom_data->m_main_sink_output_stage == nullptr)
    {
        // Register and initialize/configure WebRTC streamer as app extensions if no main output stage is provided
        register_extension(std::make_shared<WebRTCStreamerExt>());
    }

    /*
        Register and initialize/configure  ClipQueryServiceExt as app extensions
    */
    register_extension(std::make_shared<ClipQueryServiceExt>());
    auto query_service_ext = get_extension<ClipQueryServiceExt>();

    // Create ClipTextEncoder and add to query service
    std::vector<ClipTextEncoder::TextEncoderConfig> text_encoder_config;
    for (const auto &text_encoder : app_custom_data->m_clip_text_encoder_support_list)
    {
        ClipTextEncoder::TextEncoderConfig config(
            app_custom_data->m_hailort_device_config.device_id, text_encoder.tokenizer_path, text_encoder.network_id,
            text_encoder.embedding_lookup_path, text_encoder.projection_weights_path, text_encoder.projection_bias_path,
            text_encoder.hef_file_path, text_encoder.network_embedding_size);

        text_encoder_config.push_back(config);
    }

    int text_encoder_batch_size = 1;
    std::shared_ptr<ClipTextEncoder> clip_text_encoder =
        std::make_shared<ClipTextEncoder>(text_encoder_config, text_encoder_batch_size);
    auto text_encoder_result = clip_text_encoder->initialize();
    if (!text_encoder_result)
    {
        std::cerr << "Failed to initialize ClipTextEncoder: " << static_cast<int>(text_encoder_result.error())
                  << std::endl;
        HAILO_ANALYTICS_LOG_ERROR("{} failed: Failed to initialize ClipTextEncoder: {}", __func__,
                                  static_cast<int>(text_encoder_result.error()));
        return CamAppReturnCode::APP_EXTENSION_REGITRATION_FAILED;
    }

    // DB Config with read-only access connection for query service
    auto query_db_config = ClipQueryServiceExt::DatabaseConfig(
        DatabaseManagerHelper::get_faiss_table_factory_name(Database::SQLITE_ACCESS_OPEN_READ_ONLY).value(),
        DatabaseManagerHelper::get_thumbnail_table_factory_name(Database::SQLITE_ACCESS_OPEN_READ_ONLY).value(),
        DatabaseManagerHelper::get_video_table_factory_name(Database::SQLITE_ACCESS_OPEN_READ_ONLY).value());

    auto result = query_service_ext->configure(sql_db_file_path, query_db_config, clip_text_encoder);
    if (!result)
    {
        std::cerr << "clip_query_service configure failed: " << result.error() << std::endl;
        HAILO_ANALYTICS_LOG_ERROR("{} failed: clip_query_service configure failed: {}", __func__, result.error());
        return CamAppReturnCode::APP_EXTENSION_REGITRATION_FAILED;
    }

    /*
        Register and initialize/configure  VideoStreamingService as app extensions
    */
    auto video_streaming_service = VideoStreamingServiceExt::create(m_app_custom_data->m_query_playback_rtp_receiver);
    if (!video_streaming_service)
    {
        std::cerr << "Failed to create VideoStreamingService" << std::endl;
        HAILO_ANALYTICS_LOG_ERROR("{} failed: Failed to create VideoStreamingService", __func__);
        return CamAppReturnCode::APP_EXTENSION_REGITRATION_FAILED;
    }
    register_extension(video_streaming_service);

    /* Register App control service as app extensions */
    register_extension(std::make_shared<AppControlServiceExt>());

    return CamAppReturnCode::SUCCESS;
}

std::string ClipVideoPipeline::default_media_config() const
{
    return app::paths::medialib_config;
}

std::string ClipVideoPipeline::main_stream_encoder_id(const MediaStageComponents &components) const
{
    std::string encoder_stream_id;
    for (const auto &encoder_stage_data : components.m_encoder_stages)
    {
        if (encoder_stage_data.first == app::stream_id::highres)
        {
            encoder_stream_id = encoder_stage_data.first;
            break;
        }
    }

    return encoder_stream_id;
}

std::string ClipVideoPipeline::main_stream_frontend_output_id(const MediaStageComponents &components) const
{
    std::string output_stream_id;
    auto frontend_output_streams = components.m_frontend_stage->get_outputs_streams();
    for (auto output_stream : frontend_output_streams.value())
    {
        if (output_stream.id == app::stream_id::highres)
        {
            output_stream_id = output_stream.id;
            break;
        }
    }

    return output_stream_id;
}

tl::expected<PipelinePtr, CamAppReturnCode> ClipVideoPipeline::build_pipeline(const MediaStageComponents &components)
{
    show_component_info(components);

    auto app_custom_data = std::dynamic_pointer_cast<ClipAppCustomData>(components.m_user_data);
    if (!app_custom_data)
    {
        std::cerr << "ClipAppCustomData is not set in ClipVideoPipeline" << std::endl;
        HAILO_ANALYTICS_LOG_ERROR("{} failed: ClipAppCustomData is not set in ClipVideoPipeline", __func__);
        return tl::unexpected(CamAppReturnCode::FAILED);
    }

    // Check to make sure the clip image network input size is valid
    int clip_network_input_width = app_custom_data->m_clip_image_encoders.image_encoder_input_width;
    int clip_network_input_height = app_custom_data->m_clip_image_encoders.image_encoder_input_height;
    if (clip_network_input_width <= 0 || clip_network_input_height <= 0)
    {
        std::cerr << "Invalid CLIP image encoder input size: " << clip_network_input_width << "x"
                  << clip_network_input_height << std::endl;
        HAILO_ANALYTICS_LOG_ERROR("{} failed: Invalid CLIP image encoder input size: {}x{}", __func__,
                                  clip_network_input_width, clip_network_input_height);
        return tl::unexpected(CamAppReturnCode::FAILED);
    }
    std::cout << "CLIP image encoder input size: " << clip_network_input_width << "x" << clip_network_input_height
              << std::endl;

    PipelineBuilder pip_builder;

    // Get the input resolution from frontend
    auto streams = components.m_frontend_stage->get_outputs_streams();

    int bbox_crop_input_width = 0;
    int bbox_crop_input_height = 0;
    if (streams.has_value())
    {
        for (const auto &stream : streams.value())
        {
            std::cout << "Frontend output stream id: " << stream.id << ", resolution: " << stream.width << "x"
                      << stream.height << std::endl;

            if (stream.id == app::stream_id::highres)
            {
                bbox_crop_input_width = stream.width;
                bbox_crop_input_height = stream.height;
                break;
            }
        }
    }
    if (bbox_crop_input_width == 0 || bbox_crop_input_height == 0)
    {
        std::cerr << "Failed to get input resolution from frontend for stream id " << app::stream_id::stream_ai
                  << std::endl;
        HAILO_ANALYTICS_LOG_ERROR("{} failed: Failed to get input resolution from frontend for stream id {}", __func__,
                                  app::stream_id::stream_ai);
        return tl::unexpected(CamAppReturnCode::FAILED);
    }

    // Get The storage monitor service extension which we will use to get the storage directories
    auto storage_monitor_service_ext = get_extension<StorageMonitorServiceExt>();

    /*
        First Step - We create the necessary stage and add it to the pipeline
    */

    std::string frontend_stage_name = components.m_frontend_stage->get_name();

    pip_builder.add_stage(components.m_frontend_stage, StageType::SOURCE);
    // clang-format off
    // Vision Pipeline VGA Stages
    /*
        +-------+    +--------+    +------------+    +---------+    +-----------------------+    +-------+    +---------+
        |  VGA  | -> | VGA Tee| -> | aggregator | -> | overlay | -> | OSD/Mask/Jpeg Encoder | -> | Cache | -> | Storage |
        +-------+    +--------+    +------------+    +---------+    +-----------------------+    +-------+    +---------+
        +==============+_______________/                                   \     +-------+          ^
        | ai detection |                                                    \--> |  UDP? |          |
        |     Tee      |                                                         +-------+     +==========+
        +==============+                                                                       |    AI    |
                                                                                               | Best Shot|
                                                                                               +==========+
    */
    // clang-format on
    {
        std::shared_ptr<TeeStage> vga_tee_stage = TeeStageBuild::create()
                                                      .set_stage_name(app::stage::vga_tee)
                                                      .set_queue_size(5)
                                                      .set_leaky_opt(true)
                                                      .buildptr();

        std::shared_ptr<SyncAggregatorStage> vga_agg_stage = SyncAggregatorStageBuild::create()
                                                                 .set_stage_name(app::stage::vga_aggregator)
                                                                 .set_static_subframes_opt(1)
                                                                 .set_main_inlet_name(app::stage::vga_tee)
                                                                 .set_main_queue_size(6)
                                                                 .set_main_leaky(false)
                                                                 .set_sub_inlet_name(app::stage::detection_tee_out)
                                                                 .set_sub_queue_size(5)
                                                                 .set_sub_leaky(true)
                                                                 .set_multiscale_opt(false)
                                                                 .set_iou_threshold_opt(0.3)
                                                                 .set_border_threshold_opt(0.1)
                                                                 .set_timeout_opt(std::chrono::milliseconds(300))
                                                                 .buildptr();

        std::shared_ptr<OverlayStage> overlay_stage =
            OverlayStageBuild::create()
                .set_stage_name(app::stage::vga_overlay)
                .set_skip_opt(m_app_custom_data->m_skip_detections_overlays_drawing)
                .set_queue_size(3)
                .set_leaky_opt(false)
                .buildptr();

        std::shared_ptr<CacheStage> cache_stage = CacheStageBuild::create()
                                                      .set_stage_name(app::stage::thumbnail_cache)
                                                      .set_queue_size(5)
                                                      .set_cache_size(30)
                                                      .buildptr();

        auto thumb_sql_db_name =
            DatabaseManagerHelper::get_thumbnail_table_factory_name(Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);
        std::shared_ptr<ThumStorageStage> thumb_storage_stage =
            ThumStorageStageBuild::create()
                .set_stage_name(app::stage::thumbnail_storage)
                .set_queue_size_opt(10)
                .set_database_source(ThumStorageStage::DB_SOURCE_FROM_FACTORY)
                .set_database_source_data(thumb_sql_db_name.value())
                .set_thumbnail_path(storage_monitor_service_ext->get_thumbnail_directory().value())
                .set_thumbnail_file_prefix(app::storage::thumbnail_prefix)
                .buildptr();

        auto it_enc_vga = components.m_encoder_stages.find(app::stream_id::stream_vga);
        if (it_enc_vga == components.m_encoder_stages.end())
        {
            HAILO_ANALYTICS_LOG_ERROR("{} failed: Cannot find encoder stage for stream id {}", __func__,
                                      app::stream_id::stream_vga);
            return tl::unexpected(CamAppReturnCode::FAILED);
        }

        pip_builder.add_stage(vga_tee_stage);
        pip_builder.add_stage(vga_agg_stage);
        pip_builder.add_stage(overlay_stage);
        pip_builder.add_stage(it_enc_vga->second.encoder_stage_ptr, StageType::SINK);
        pip_builder.add_stage(cache_stage);
        pip_builder.add_stage(thumb_storage_stage, StageType::SINK);
    }

    // Vision Pipeline 4K Stages
    std::string main_4k_output_stage_name;
    {

        std::shared_ptr<OverlayStage> main_4k_overlay_stage =
            OverlayStageBuild::create()
                .set_stage_name(app::stage::main_4k_overlay)
                .set_skip_opt(m_app_custom_data->m_skip_detections_overlays_drawing)
                .set_queue_size(3)
                .set_leaky_opt(false)
                .buildptr();

        auto it_enc_4k = components.m_encoder_stages.find(app::stream_id::highres);
        if (it_enc_4k == components.m_encoder_stages.end())
        {
            HAILO_ANALYTICS_LOG_ERROR("{} failed: Cannot find encoder stage for stream id {}", __func__,
                                      app::stream_id::highres);
            return tl::unexpected(CamAppReturnCode::FAILED);
        }

        auto video_sql_db_name =
            DatabaseManagerHelper::get_video_table_factory_name(Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);
        auto segment_video_sec = app_custom_data->m_pipeline_config.video_storage_stage.video_segment_duration_seconds;
        auto enable_video_storage = app_custom_data->m_pipeline_config.video_storage_stage.enabled;
        auto always_record_video = app_custom_data->m_pipeline_config.full_frame_indexing_stage.enabled;
        std::shared_ptr<VideoStorageStage> mkv_storage_stage =
            VideoStorageStageBuild::create()
                .set_stage_name(app::stage::main_mkv_storage)
                .set_database_source(VideoStorageStage::DB_SOURCE_FROM_FACTORY)
                .set_database_source_data(video_sql_db_name.value())
                .set_video_path(storage_monitor_service_ext->get_video_directory().value())
                .set_video_file_prefix(app::storage::video_segment_prefix)
                .set_video_segment_duration(segment_video_sec)
                .set_enable(enable_video_storage)
                .set_always_record(always_record_video)
                .set_queue_size(4)
                .set_leaky_opt(true)
                .buildptr();

#if ENABLE_4K_UDP_OUTPUT
        std::string udp_name = "udp_" + std::string(app::stream_id::highres);
        std::shared_ptr<UdpStage> main_4k_udp_stage =
            UdpStageBuild::create().set_stage_name(app::stage::main_4k_udp).set_leaky_opt(false).buildptr();
        m_udp_outputs[udp_name] = main_4k_udp_stage;
        AppStatus udp_config_status =
            main_4k_udp_stage->configure(app::net::host_ip, std::to_string(app::net::udp_port_4k), EncodingType::H264);
        if (udp_config_status != AppStatus::SUCCESS)
        {
            std::cerr << "Failed to configure udp " << udp_name << std::endl;
            HAILO_ANALYTICS_LOG_ERROR("{} failed: Failed to configure udp {}", __func__, udp_name);
            return tl::unexpected(CamAppReturnCode::FAILED);
        }
#endif
        std::shared_ptr<hailo_analytics::pipeline::ThreadedStage> main_4k_output_stage;
        if (m_app_custom_data->m_main_sink_output_stage == nullptr)
        {
            std::shared_ptr<RTPConverterStage> main_4k_webrtc_stage;
            auto webrtcStreamer_ext = get_extension<WebRTCStreamerExt>();
            if (!webrtcStreamer_ext)
            {
                std::cerr << "WebRTCStreamerExt extension is required but cannot be found" << std::endl;
                HAILO_ANALYTICS_LOG_ERROR("{} failed: WebRTCStreamerExt extension is required but cannot be found",
                                          __func__);
                return tl::unexpected(CamAppReturnCode::FAILED);
            }

            main_4k_webrtc_stage = RTPConverterStageBuild::create()
                                       .set_stage_name(app::stage::main_4k_webrtc)
                                       .set_rtp_receiver(webrtcStreamer_ext)
                                       .set_session_name(app::stage::main_4k_webrtc)
                                       .set_queue_size_opt(3)
                                       .set_leaky_opt(true)
                                       .buildptr();
            main_4k_webrtc_stage->configure(EncodingType::H264);
            main_4k_output_stage =
                std::static_pointer_cast<hailo_analytics::pipeline::ThreadedStage>(main_4k_webrtc_stage);
            main_4k_output_stage_name = app::stage::main_4k_webrtc;
        }
        else
        {
            main_4k_output_stage = std::dynamic_pointer_cast<hailo_analytics::pipeline::ThreadedStage>(
                m_app_custom_data->m_main_sink_output_stage);
            main_4k_output_stage_name = main_4k_output_stage->get_name();
        }

        pip_builder.add_stage(main_4k_overlay_stage);
        pip_builder.add_stage(it_enc_4k->second.encoder_stage_ptr, StageType::SINK);
        pip_builder.add_stage(mkv_storage_stage, StageType::SINK);
#if ENABLE_4K_UDP_OUTPUT
        pip_builder.add_stage(main_4k_udp_stage, StageType::SINK);
#endif
        pip_builder.add_stage(main_4k_output_stage, StageType::SINK);
    }
    // AI Pipeline tiling detection Stages
    {
        std::shared_ptr<TilingCropStage> tilling_stage = TilingCropStageBuild::create()
                                                             .set_stage_name(app::stage::detection_tiling)
                                                             .set_output_pool_size(50)
                                                             .set_input_width(app::tiling::input.width)
                                                             .set_input_height(app::tiling::input.height)
                                                             .set_output_width(app::tiling::output.width)
                                                             .set_output_height(app::tiling::output.height)
                                                             .set_main_sub_name(app::stage::tiling_aggregator)
                                                             .set_sub_sub_name(app::stage::detection_infer)
                                                             .set_bbox_tiles(app::tiling::tiles)
                                                             .set_queue_size(5)
                                                             .set_leaky_opt(true)
                                                             .set_pool_mode_opt(StagePoolMode::BLOCKING)
                                                             .buildptr();

        namespace ai_models = hailo_analytics::analytics::ai_models;
        std::shared_ptr<HailortAsyncStage> detection_infer_stage =
            HailortAsyncStageBuild::create()
                .set_stage_name(app::stage::detection_infer)
                .set_hef_path(ai_models::resolve_hef(ai_models::YOLOV8N.hef_relative))
                .set_queue_size(5)
                .set_output_pool_size(50)
                .set_group_id(app_custom_data->m_hailort_device_config.device_id)
                .set_batch_size(5)
                .set_job_limit(10)
                .set_scheduler_threshold_opt(5)
                .set_dynamic_threshold_opt(false)
                .set_scheduler_timeout_opt(std::chrono::milliseconds(100))
                .set_pool_mode_opt(StagePoolMode::BLOCKING)
                .buildptr();

        std::shared_ptr<PostprocessStage> detection_post_stage =
            PostprocessStageBuild::create()
                .set_stage_name(app::stage::detection_post)
                .set_so_path(ai_models::resolve_post_so(ai_models::YOLOV8N.post_so_relative))
                .set_function_name_opt(std::string(ai_models::YOLOV8N.post_function_name))
                .set_config_path_opt(ai_models::resolve_config(ai_models::YOLOV8N.post_config_relative))
                .set_queue_size_opt(5)
                .set_leaky_opt(false)
                .buildptr();

        std::shared_ptr<AggregatorStage> tiling_agg_stage = AggregatorStageBuild::create()
                                                                .set_stage_name(app::stage::tiling_aggregator)
                                                                .set_static_subframes_opt(5)
                                                                .set_main_inlet_name(app::stage::detection_tiling)
                                                                .set_main_queue_size(2)
                                                                .set_main_leaky(false)
                                                                .set_sub_inlet_name(app::stage::detection_post)
                                                                .set_sub_queue_size(5)
                                                                .set_sub_leaky(false)
                                                                .set_multiscale_opt(true)
                                                                .set_iou_threshold_opt(0.3)
                                                                .set_border_threshold_opt(0.1)
                                                                .buildptr();

        std::shared_ptr<TeeStage> main_4k_tee_stage = TeeStageBuild::create()
                                                          .set_stage_name(app::stage::main_4k_tee)
                                                          .set_queue_size(5)
                                                          .set_leaky_opt(true)
                                                          .buildptr();

        std::shared_ptr<SyncAggregatorStage> main_4k_agg_stage = SyncAggregatorStageBuild::create()
                                                                     .set_stage_name(app::stage::main_4k_aggregator)
                                                                     .set_static_subframes_opt(1)
                                                                     .set_main_inlet_name(app::stage::main_4k_tee)
                                                                     .set_main_queue_size(6)
                                                                     .set_main_leaky(false)
                                                                     .set_sub_inlet_name(app::stage::tiling_aggregator)
                                                                     .set_sub_queue_size(3)
                                                                     .set_sub_leaky(true)
                                                                     .set_multiscale_opt(false)
                                                                     .set_iou_threshold_opt(0.3)
                                                                     .set_border_threshold_opt(0.1)
                                                                     .set_timeout_opt(std::chrono::milliseconds(300))
                                                                     .buildptr();

        std::shared_ptr<LightweightTrackerStage> tracker_stage =
            LightweightTrackerStageBuild::create()
                .set_stage_name(app::stage::tracker_light)
                .set_queue_size_opt(1)
                .set_leaky_opt(false)
                .set_classification_ids({static_cast<int>(app::classes::detection_id::person),
                                         static_cast<int>(app::classes::detection_id::vehicle)})
                .set_block_non_tracked_classification_id(true)
                .set_add_tracking_id(true)
                .set_grace_period(2)
                .set_smooth_alpha(0.5f)
                .set_weighted_average_decay(0.4f)
                .set_copy_nested_objects(true, static_cast<int>(app::classes::detection_id::person))
                .buildptr();

        std::shared_ptr<TeeStage> detection_tee_stage = TeeStageBuild::create()
                                                            .set_stage_name(app::stage::detection_tee_out)
                                                            .set_queue_size(5)
                                                            .set_leaky_opt(false)
                                                            .buildptr();

        register_detections_db_config(app::tiling::input.width, app::tiling::input.height);
        auto detections_db_stage = hailo_analytics::pipeline::ai::AnalyticsDBStageBuild::create()
                                       .set_stage_name(DETECTIONS_DB_STAGE)
                                       .set_queue_size(3)
                                       .set_leaky_opt(true)
                                       .set_analytics_data_id(DETECTIONS_DATA_ID)
                                       .set_type(AnalyticsType::DETECTION)
                                       .buildptr();

        pip_builder.add_stage(tilling_stage);
        pip_builder.add_stage(detection_infer_stage);
        pip_builder.add_stage(detection_post_stage);
        pip_builder.add_stage(tiling_agg_stage);
        pip_builder.add_stage(main_4k_tee_stage);
        pip_builder.add_stage(main_4k_agg_stage);
        pip_builder.add_stage(tracker_stage);
        pip_builder.add_stage(detection_tee_stage);
        pip_builder.add_stage(detections_db_stage, hailo_analytics::pipeline::StageType::SINK);
    }
    // AI Pipeline Clip embedding Stages
    {
        auto unclassified_fps_block =
            app_custom_data->m_pipeline_config.tracker_traffic_ctrl_stage.unclassified_fps_to_block;
        auto max_objects_per_second =
            app_custom_data->m_pipeline_config.tracker_traffic_ctrl_stage.max_objects_per_second;
        std::shared_ptr<TrackerTrafficCtrlStage> tracker_traffic_ctrl_stage =
            TrackerTrafficCtrlStageBuild::create()
                .set_stage_name(app::stage::tracker_traffic_ctrl)
                .set_block_untracked_obj(true)
                .set_unclassified_fps_to_block(
                    unclassified_fps_block) // Let a detection frame pass every x amount of unclassified frame
                .set_tracked_max_objects_per_second(max_objects_per_second)
                .set_queue_size_opt(2)
                .set_leaky_opt(false)
                .buildptr();

        std::vector<std::string> bbox_crop_labels;
        bbox_crop_labels.push_back(app::classes::clip_crop_target_label_person);
        bbox_crop_labels.push_back(app::classes::clip_crop_target_label_vehicle);

        std::shared_ptr<FullFrameBBoxInjectorStage> full_frame_injector = nullptr;
        if (app_custom_data->m_pipeline_config.full_frame_indexing_stage.enabled)
        {
            bbox_crop_labels.push_back(app::classes::clip_crop_target_label_scene);

            full_frame_injector =
                FullFrameBBoxInjectorStageBuild::create()
                    .set_stage_name(app::stage::full_frame_bbox_injector)
                    .set_full_frame_class_name(app::classes::clip_crop_target_label_scene)
                    .set_interval_seconds(app_custom_data->m_pipeline_config.full_frame_indexing_stage.interval_seconds)
                    .set_queue_size(2)
                    .set_leaky_opt(false)
                    .buildptr();
            pip_builder.add_stage(full_frame_injector);
        }

        std::shared_ptr<BBoxCropStage> clip_crop_stage =
            BBoxCropStageBuild::create()
                .set_stage_name(app::stage::clip_crop)
                .set_output_pool_size(20)
                .set_input_width(bbox_crop_input_width)
                .set_input_height(bbox_crop_input_height)
                .set_output_width(clip_network_input_width)
                .set_output_height(clip_network_input_height)
                .set_main_sub_name("N/A") // We do not need to send to main subscriber
                .set_sub_sub_name(app::stage::clip_image_preprocess_check)
                .set_labels(bbox_crop_labels)
                .set_queue_size(5)
                .set_leaky_opt(true)
                .set_pool_mode_opt(StagePoolMode::LEAKY)
                .buildptr();

        std::shared_ptr<ClipImagePreprocess> clip_image_preprocess_stage =
            ClipImagePreprocessBuild::create()
                .set_stage_name(app::stage::clip_image_preprocess_check)
                .set_enable(app_custom_data->m_pipeline_config.clip_quality_check_stage.enabled)
                .set_queue_size(15)
                .buildptr();

        auto faiss_sql_db_name =
            DatabaseManagerHelper::get_faiss_table_factory_name(Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);
        std::shared_ptr<FaissStorageStage> faiss_storage_stage =
            FaissStorageStageBuild::create()
                .set_stage_name(app::stage::faiss_storage)
                .set_faiss_index_source(FaissStorageStage::IDX_SOURCE_FROM_USER_META)
                .set_db_source(FaissStorageStage::DB_SOURCE_FROM_FACTORY)
                .set_db_factory_name(faiss_sql_db_name.value())
                .set_queue_size_opt(15)
                .buildptr();
        pip_builder.add_stage(tracker_traffic_ctrl_stage);
        pip_builder.add_stage(clip_crop_stage);
        pip_builder.add_stage(clip_image_preprocess_stage);
        pip_builder.add_stage(faiss_storage_stage, StageType::SINK);

        std::shared_ptr<TeeStage> clip_tee_stage = TeeStageBuild::create()
                                                       .set_stage_name(app::stage::clip_tee)
                                                       .set_queue_size(5)
                                                       .set_leaky_opt(false)
                                                       .buildptr();
        pip_builder.add_stage(clip_tee_stage);

        for (const auto &encoder : app_custom_data->m_clip_image_encoders.encoders)
        {
            if (!encoder.enabled)
            {
                std::cout << "Skipping disabled encoder: " << encoder.id << std::endl;
                continue; // Skip disabled encoders
            }

            std::cout << "Adding CLIP encoder: " << encoder.id << std::endl;
            std::cout << "HEF Path: " << encoder.hef_path << std::endl;
            std::cout << "Postprocess File: " << encoder.postprocess_file << std::endl;
            std::cout << "Postprocess Function Name: " << encoder.postprocess_function_name << std::endl;

            std::shared_ptr<HailortAsyncStage> clip_infer_stage =
                HailortAsyncStageBuild::create()
                    .set_stage_name(encoder.id)
                    .set_hef_path(encoder.hef_path)
                    .set_queue_size(15)
                    .set_output_pool_size(30)
                    .set_group_id(app_custom_data->m_hailort_device_config.device_id)
                    .set_batch_size(6)
                    .set_job_limit(6)
                    .set_scheduler_threshold_opt(4)
                    .set_dynamic_threshold_opt(false)
                    .set_scheduler_timeout_opt(std::chrono::milliseconds(100))
                    .buildptr();

            std::shared_ptr<PostprocessStage> clip_post_stage =
                PostprocessStageBuild::create()
                    .set_stage_name(encoder.id + "_post")
                    .set_so_path(encoder.postprocess_file)
                    .set_function_name_opt(encoder.postprocess_function_name)
                    .set_config_path_opt("")
                    .set_queue_size_opt(30)
                    .set_leaky_opt(false)
                    .buildptr();

            pip_builder.add_stage(clip_infer_stage);
            pip_builder.add_stage(clip_post_stage);
        }
    }

    /*
        Second Step - We now connect all the stages
    */
    // Vision Pipeline VGA Connects
    {
        pip_builder.connect_frontend(frontend_stage_name, app::stream_id::stream_vga, app::stage::vga_tee);

        pip_builder.connect(app::stage::vga_tee, app::stage::vga_aggregator);

        pip_builder.connect(app::stage::detection_tee_out, app::stage::vga_aggregator);

        pip_builder.connect(app::stage::vga_aggregator, app::stage::vga_overlay);

        pip_builder.connect(app::stage::vga_overlay, app::stream_id::stream_vga);

        pip_builder.connect(app::stream_id::stream_vga, app::stage::thumbnail_cache);

        pip_builder.connect(app::stage::thumbnail_cache, app::stage::thumbnail_storage);
    }
    // Vision Pipeline 4K Connects
    {
        pip_builder.connect(app::stage::detection_tee_out, app::stage::main_4k_overlay);

        pip_builder.connect(app::stage::main_4k_overlay, app::stream_id::highres);

        pip_builder.connect(app::stream_id::highres, app::stage::main_mkv_storage);

#if ENABLE_4K_UDP_OUTPUT
        pip_builder.connect(app::stream_id::highres, app::stage::main_4k_udp);
#endif
        pip_builder.connect(app::stream_id::highres, main_4k_output_stage_name);
    }
    // AI Pipeline tiling detection Connects
    {
        pip_builder.connect_frontend(frontend_stage_name, app::stream_id::highres, app::stage::main_4k_tee);

        pip_builder.connect(app::stage::main_4k_tee, app::stage::main_4k_aggregator);

        pip_builder.connect(app::stage::main_4k_aggregator, app::stage::tracker_light);

        pip_builder.connect(app::stage::tracker_light, app::stage::detection_tee_out);

        pip_builder.connect(app::stage::detection_tee_out, DETECTIONS_DB_STAGE);

        pip_builder.connect_frontend(frontend_stage_name, app::stream_id::stream_ai, app::stage::detection_tiling);

        pip_builder.connect(app::stage::detection_tiling, app::stage::tiling_aggregator);

        pip_builder.connect(app::stage::detection_tiling, app::stage::detection_infer);

        pip_builder.connect(app::stage::detection_infer, app::stage::detection_post);

        pip_builder.connect(app::stage::detection_post, app::stage::tiling_aggregator);

        pip_builder.connect(app::stage::tiling_aggregator, app::stage::main_4k_aggregator);
    }
    // AI Pipeline Clip embedding Connects
    {
        pip_builder.connect(app::stage::detection_tee_out, app::stage::tracker_traffic_ctrl);

        if (app_custom_data->m_pipeline_config.full_frame_indexing_stage.enabled)
        {
            pip_builder.connect(app::stage::tracker_traffic_ctrl, app::stage::full_frame_bbox_injector);
            pip_builder.connect(app::stage::full_frame_bbox_injector, app::stage::clip_crop);
        }
        else
        {
            pip_builder.connect(app::stage::tracker_traffic_ctrl, app::stage::clip_crop);
        }

        pip_builder.connect(app::stage::clip_crop, app::stage::clip_image_preprocess_check);

        pip_builder.connect(app::stage::clip_image_preprocess_check, app::stage::thumbnail_cache);

        pip_builder.connect(app::stage::clip_image_preprocess_check, app::stage::main_mkv_storage);

        pip_builder.connect(app::stage::clip_image_preprocess_check, app::stage::clip_tee);

        // Connect CLIP infer and post process stage
        for (const auto &encoder : app_custom_data->m_clip_image_encoders.encoders)
        {
            if (!encoder.enabled)
            {
                continue; // Skip disabled encoders
            }
            std::string clip_infer_stage_name = encoder.id;
            std::string clip_post_stage_name = encoder.id + "_post";

            pip_builder.connect(app::stage::clip_tee, clip_infer_stage_name);

            pip_builder.connect(clip_infer_stage_name, clip_post_stage_name);

            pip_builder.connect(clip_post_stage_name, app::stage::faiss_storage);
        }
    }

    // Third Step - Finally we build the pipeline and returns it.
    return pip_builder.build("ClipVideoPipeline");
}

std::string ClipVideoPipeline::get_udp_stage_name_contain(std::string &contains)
{
    std::string udp_full_stage_name;
    for (auto udp : m_udp_outputs)
    {
        if (udp.first.find(contains) != std::string::npos)
        {
            udp_full_stage_name = udp.first;
            break;
        }
    }

    return udp_full_stage_name;
}

void ClipVideoPipeline::show_component_info(const MediaStageComponents &components)
{
    {
        std::string frontend_stage_name = components.m_frontend_stage->get_name();
        std::cout << "frontend stage name: " << frontend_stage_name << std::endl;

        auto frontend_output_streams = components.m_frontend_stage->get_outputs_streams();
        for (auto output_stream : frontend_output_streams.value())
        {
            std::cout << "frontend output stream_id: " << output_stream.id << " Resolution: " << output_stream.width
                      << "X" << output_stream.height << std::endl;
        }

        for (const auto &encoder_stage : components.m_encoder_stages)
        {
            std::cout << "encoder stage name: " << encoder_stage.first << std::endl;
        }
    }
}

void ClipVideoPipeline::faiss_index_misc_config(const std::shared_ptr<ClipAppCustomData> &app_custom_data)
{
    if (app_custom_data->m_faiss_test_config.random_data.enabled &&
        app_custom_data->m_faiss_test_config.random_data.num_vectors > 0)
    {
        for (const auto &image_encoder : app_custom_data->m_clip_image_encoders.encoders)
        {
            auto index_result = DatabaseManagerHelper::get_faiss_index_by_name(image_encoder.id);
            if (!index_result)
            {
                std::cerr << "Failed to get FAISS index for image encoder: " << image_encoder.id << std::endl;
                HAILO_ANALYTICS_LOG_ERROR("{} failed: Failed to get FAISS index for image encoder: {}", __func__,
                                          image_encoder.id);
                return;
            }
            auto faiss_db = index_result.value();
            if (faiss_db->generate_random_embeddings(app_custom_data->m_faiss_test_config.random_data.num_vectors))
            {
                std::cout << "Generated random data for FAISS index: " << image_encoder.id << " with "
                          << app_custom_data->m_faiss_test_config.random_data.num_vectors << " vectors." << std::endl;
            }
            else
            {
                std::cerr << "Failed to generate random data for FAISS index: " << image_encoder.id << std::endl;
                HAILO_ANALYTICS_LOG_ERROR("{} failed: Failed to generate random data for FAISS index: {}", __func__,
                                          image_encoder.id);
            }
        }
    }
}
