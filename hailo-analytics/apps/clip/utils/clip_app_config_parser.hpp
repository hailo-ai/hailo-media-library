#pragma once

#include <string>
#include <vector>

#include "rapidyaml-0.9.0.hpp"

struct ClipAppConfig
{
    struct QueryDefaults
    {
        float score_threshold;
        int max_query;
        int remove_duplicate_within_sec;
    } query_defaults;

    struct FrontEndSourceFromFile
    {
        bool enabled = false;
        std::string file_path;
    } frontend_source_from_file;

    struct TextEncoder
    {
        std::string network_name;
        std::string network_id;
        std::string network_text_enc_onnx_file_path;
        int network_embedding_size;
        int network_context_length;
        std::string tokenizer_path;
        std::string embedding_lookup_path;
        std::string projection_weights_path;
        std::string projection_bias_path;
        std::string hef_file_path;

        // Default constructor
        TextEncoder() : network_embedding_size(0)
        {
        }
    };

    // Text encoder support list with defaults
    std::vector<TextEncoder> text_encoder_support_list;

    const TextEncoder *get_text_encoder_from_id(const std::string network_id);

    struct HailortDeviceConfig
    {
        std::string device_id = "device0"; // Default to device0

    } hailort_device_config;

    // Storage directories configuration
    struct StorageConfiguration
    {
        std::string mount_location = "/var/volatile/";
        std::string root_directory = "storage";
        std::string database_directory = "database";
        std::string faissdb_directory = "faissdb";
        std::string thumbnail_directory = "thumbnails";
        std::string video_directory = "videos";
        float low_disk_threshold_percent = 10.0f; // Default low disk space threshold
        uint check_interval_seconds = 5;          // Default check interval in seconds
    } storage_config;

    // Clip image encoder configuration
    struct ImageEncoders
    {

        struct encoder
        {
            std::string name;
            std::string id;
            std::string hef_path;
            int embedding_size;
            std::string postprocess_file;
            std::string postprocess_function_name;
            bool enabled;
        };

        int image_encoder_input_width;
        int image_encoder_input_height;
        std::vector<encoder> encoders;

    } clip_image_encoders;

    // Server configuration
    struct ServerInfo
    {
        std::string host = "localhost";
        int port = 80;
    } server_info;

    // FAISS index configuration
    struct FaissConfig
    {
        struct RandomDataConfig
        {
            bool enabled = false; // Enable random data generation for testing
            int num_vectors = 0;  // Number of random vectors to generate if enabled
        } random_data;

    } faiss_config;

    struct PipelineConfig
    {
        struct ClipQualityCheckStage
        {
            bool enabled = true; // Enable or disable the quality check stage
        } clip_quality_check_stage;

        struct TrackerTrafficCtrlStage
        {
            size_t unclassified_fps_to_block =
                30; // Number of unclassified frames to block before allowing a detection frame to pass
            size_t max_objects_per_second = 15; // Maximum objects allowed to pass per second (0 means no limit)
        } tracker_traffic_ctrl_stage;

        struct VideoStorageStage
        {
            bool enabled = true;                    // Enable or disable video storage stage
            int video_segment_duration_seconds = 6; // Maximum video duration in seconds to store per file
        } video_storage_stage;

        struct FullFrameIndexingStage
        {
            bool enabled = false;          // Disabled by default
            float interval_seconds = 5.0f; // How often to index a full frame
        } full_frame_indexing_stage;

    } pipeline_config;

    // Method to validate configuration
    bool validate() const;
};

class ClipAppConfigParser
{
  private:
    ClipAppConfig m_config;

    // Helper method to safely get string value from YAML node
    std::string get_string_value(const ryml::ConstNodeRef &node, const std::string &key,
                                 const std::string &defaultValue = "");

    // Helper method to safely get float value from YAML node
    float get_float_value(const ryml::ConstNodeRef &node, const std::string &key, float defaultValue = 0.0f);

    // Helper method to safely get integer value from YAML node
    int get_int_value(const ryml::ConstNodeRef &node, const std::string &key, int defaultValue = 0);

    // Helper method to safely get boolean value from YAML node
    bool get_bool_value(const ryml::ConstNodeRef &node, const std::string &key, bool defaultValue = false);

    // Parse network configurations
    void parse_network_configs(const ryml::ConstNodeRef &root);

    // Parse HailoRT device configuration
    void parse_hailort_device_config(const ryml::ConstNodeRef &root);

    // Parse storage configuration
    void parse_storage_config(const ryml::ConstNodeRef &root);

    // Parse server configurations
    void parse_server_config(const ryml::ConstNodeRef &root);

    // Parse image encoders configuration
    void parse_image_encoders_config(const ryml::ConstNodeRef &root);

    // Helper method to parse FAISS index configuration
    void parse_faiss_config(const ryml::ConstNodeRef &root);

    void parse_pipeline_config(const ryml::ConstNodeRef &root);

    void parse_frontend_source_from_file_config(const ryml::ConstNodeRef &root);

    void parse_query_defaults_config(const ryml::ConstNodeRef &root);

  public:
    // Constructor
    ClipAppConfigParser() = default;

    // Parse configuration from YAML file
    bool parse_from_file(const std::string &configPath);

    // Parse configuration from YAML string
    bool parse_from_string(const std::string &yamlContent);

    // Get the parsed configuration
    const ClipAppConfig &get_config() const;

    // Get mutable reference to config (for runtime modifications)
    ClipAppConfig &get_config();
};
