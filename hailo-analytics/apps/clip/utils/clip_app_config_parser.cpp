#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>

#define RYML_SINGLE_HDR_DEFINE_NOW
#include "clip_app_config_parser.hpp"

const ClipAppConfig::TextEncoder *ClipAppConfig::get_text_encoder_from_id(const std::string network_id)
{
    for (const auto &text_encoder : text_encoder_support_list)
    {
        if (text_encoder.network_id == network_id)
            return &text_encoder;
    }

    return nullptr;
}

bool ClipAppConfig::validate() const
{
    if (text_encoder_support_list.empty())
    {
        std::cerr << "Error: No text encoder configurations found" << std::endl;
        return false;
    }

    for (const auto &text_encoder : text_encoder_support_list)
    {
        if (text_encoder.network_id.empty() || text_encoder.network_name.empty())
        {
            std::cerr << "Error: Text encoder configuration missing required fields" << std::endl;
            return false;
        }
        if (text_encoder.network_embedding_size <= 0)
        {
            std::cerr << "Error: Invalid embedding size for text encoder " << text_encoder.network_id << std::endl;
            return false;
        }
    }

    return true;
}

std::string ClipAppConfigParser::get_string_value(const ryml::ConstNodeRef &node, const std::string &key,
                                                  const std::string &defaultValue)
{
    if (node.has_child(ryml::to_csubstr(key.c_str())))
    {
        ryml::csubstr value;
        node[ryml::to_csubstr(key.c_str())] >> value;
        return std::string(value.data(), value.size());
    }
    return defaultValue;
}

float ClipAppConfigParser::get_float_value(const ryml::ConstNodeRef &node, const std::string &key, float defaultValue)
{
    if (node.has_child(ryml::to_csubstr(key.c_str())))
    {
        float value;
        node[ryml::to_csubstr(key.c_str())] >> value;
        return value;
    }
    return defaultValue;
}

int ClipAppConfigParser::get_int_value(const ryml::ConstNodeRef &node, const std::string &key, int defaultValue)
{
    if (node.has_child(ryml::to_csubstr(key.c_str())))
    {
        int value;
        node[ryml::to_csubstr(key.c_str())] >> value;
        return value;
    }
    return defaultValue;
}

bool ClipAppConfigParser::get_bool_value(const ryml::ConstNodeRef &node, const std::string &key, bool defaultValue)
{
    if (node.has_child(ryml::to_csubstr(key.c_str())))
    {
        bool value;
        node[ryml::to_csubstr(key.c_str())] >> value;
        return value;
    }
    return defaultValue;
}

void ClipAppConfigParser::parse_network_configs(const ryml::ConstNodeRef &root)
{
    if (!root.has_child("text_encoders"))
    {
        std::cout << "No 'text_encoders' section found in config, using defaults" << std::endl;
        return;
    }

    const auto text_encoders_node = root["text_encoders"];
    if (!text_encoders_node.is_seq())
    {
        throw std::runtime_error("'text_encoders' must be a sequence");
    }

    std::vector<ClipAppConfig::TextEncoder> text_encoder_support_list;
    for (const auto &text_encoder_node : text_encoders_node)
    {
        ClipAppConfig::TextEncoder text_encoder;

        text_encoder.network_name = get_string_value(text_encoder_node, "name");
        text_encoder.network_id = get_string_value(text_encoder_node, "id");
        text_encoder.network_text_enc_onnx_file_path = get_string_value(text_encoder_node, "onnx_path");
        text_encoder.network_embedding_size = get_int_value(text_encoder_node, "embedding_size");
        text_encoder.network_context_length = get_int_value(text_encoder_node, "context_length");
        text_encoder.tokenizer_path = get_string_value(text_encoder_node, "tokenizer_path");
        text_encoder.embedding_lookup_path = get_string_value(text_encoder_node, "embedding_lookup_path");
        text_encoder.projection_weights_path = get_string_value(text_encoder_node, "projection_weights_path");
        text_encoder.projection_bias_path = get_string_value(text_encoder_node, "projection_bias_path");
        text_encoder.hef_file_path = get_string_value(text_encoder_node, "hef_file_path");

        if (text_encoder.network_name.empty() || text_encoder.network_id.empty())
        {
            throw std::runtime_error("Text encoder configuration missing required 'name' or 'id'");
        }

        text_encoder_support_list.push_back(text_encoder);
    }
    m_config.text_encoder_support_list.swap(text_encoder_support_list);
}

void ClipAppConfigParser::parse_hailort_device_config(const ryml::ConstNodeRef &root)
{
    if (!root.has_child("hailortdevice"))
    {
        std::cout << "No 'hailortdevice' section found in config, using defaults" << std::endl;
        return;
    }
    const auto hailort_node = root["hailortdevice"];
    m_config.hailort_device_config.device_id =
        get_string_value(hailort_node, "device_id", m_config.hailort_device_config.device_id);
}

void ClipAppConfigParser::parse_storage_config(const ryml::ConstNodeRef &root)
{
    if (!root.has_child("storage"))
    {
        std::cout << "No 'storage' section found in config, using defaults" << std::endl;
        return;
    }

    const auto storage_node = root["storage"];
    m_config.storage_config.mount_location =
        get_string_value(storage_node, "mount_location", m_config.storage_config.mount_location);
    m_config.storage_config.root_directory =
        get_string_value(storage_node, "root_directory", m_config.storage_config.root_directory);
    m_config.storage_config.database_directory =
        get_string_value(storage_node, "database_directory", m_config.storage_config.database_directory);
    m_config.storage_config.faissdb_directory =
        get_string_value(storage_node, "faissdb_directory", m_config.storage_config.faissdb_directory);
    m_config.storage_config.thumbnail_directory =
        get_string_value(storage_node, "thumbnail_directory", m_config.storage_config.thumbnail_directory);
    m_config.storage_config.video_directory =
        get_string_value(storage_node, "video_directory", m_config.storage_config.video_directory);
    m_config.storage_config.low_disk_threshold_percent =
        get_int_value(storage_node, "low_disk_threshold_percent",
                      static_cast<int>(m_config.storage_config.low_disk_threshold_percent));
    m_config.storage_config.check_interval_seconds =
        get_int_value(storage_node, "check_interval_seconds", m_config.storage_config.check_interval_seconds);
}

void ClipAppConfigParser::parse_server_config(const ryml::ConstNodeRef &root)
{
    if (!root.has_child("server"))
    {
        std::cout << "No 'server' section found in config, using defaults" << std::endl;
        return;
    }

    const auto server_node = root["server"];
    m_config.server_info.host = get_string_value(server_node, "host", m_config.server_info.host);
    m_config.server_info.port = get_int_value(server_node, "port", m_config.server_info.port);
}

void ClipAppConfigParser::parse_query_defaults_config(const ryml::ConstNodeRef &root)
{
    if (!root.has_child("query_defaults"))
    {
        std::cout << "No 'query_defaults' section found in config, using defaults" << std::endl;
        return;
    }

    const auto query_defaults_node = root["query_defaults"];
    m_config.query_defaults.score_threshold = get_float_value(query_defaults_node, "score_threshold", 0.4);
    m_config.query_defaults.max_query = get_int_value(query_defaults_node, "max_query", 100);
    m_config.query_defaults.remove_duplicate_within_sec =
        get_int_value(query_defaults_node, "remove_duplicate_within_sec", 60);
    m_config.query_defaults.default_negative_prompts.clear();
    m_config.query_defaults.default_negative_prompts = {"a photo of a person"};
    // if (query_defaults_node.has_child("negative_prompts") && query_defaults_node["negative_prompts"].is_seq())
    // {
    //     for (const auto &prompt_node : query_defaults_node["negative_prompts"])
    //     {
    //         std::string prompt = get_string_value(prompt_node, "", "");
    //         if (!prompt.empty())
    //         {
    //             m_config.query_defaults.default_negative_prompts.push_back(prompt);
    //         }
    //     }
    // }
}

void ClipAppConfigParser::parse_frontend_source_from_file_config(const ryml::ConstNodeRef &root)
{
    if (!root.has_child("frontend_source_from_file"))
    {
        std::cout << "No 'frontend_source_from_file' section found in config, using defaults" << std::endl;
        return;
    }

    const auto source_node = root["frontend_source_from_file"];
    m_config.frontend_source_from_file.enabled =
        get_bool_value(source_node, "enabled", m_config.frontend_source_from_file.enabled);
    m_config.frontend_source_from_file.file_path =
        get_string_value(source_node, "file_path", m_config.frontend_source_from_file.file_path);
}

void ClipAppConfigParser::parse_image_encoders_config(const ryml::ConstNodeRef &root)
{
    if (!root.has_child("image_encoders"))
    {
        std::cout << "No 'image_encoders' section found in config, using defaults" << std::endl;
        return;
    }

    const auto encoders_node = root["image_encoders"];
    m_config.clip_image_encoders.image_encoder_input_width = get_int_value(encoders_node, "image_size_width", 0);
    m_config.clip_image_encoders.image_encoder_input_height = get_int_value(encoders_node, "image_size_height", 0);

    if (!encoders_node.has_child("encoders") || !encoders_node["encoders"].is_seq())
    {
        std::cout << "No 'encoders' section found or it is not a sequence, using defaults" << std::endl;
        return;
    }

    for (const auto &encoder_node : encoders_node["encoders"])
    {
        ClipAppConfig::ImageEncoders::encoder encoder;
        encoder.name = get_string_value(encoder_node, "name");
        encoder.id = get_string_value(encoder_node, "id");
        encoder.hef_path = get_string_value(encoder_node, "hef_path");
        encoder.embedding_size = get_int_value(encoder_node, "embedding_size");
        encoder.postprocess_file = get_string_value(encoder_node, "postprocess_file");
        encoder.postprocess_function_name = get_string_value(encoder_node, "postprocess_function_name");
        encoder.enabled = get_bool_value(encoder_node, "enabled", true);

        if (encoder.name.empty() || encoder.id.empty())
        {
            throw std::runtime_error("Encoder configuration missing required 'name' or 'id'");
        }

        m_config.clip_image_encoders.encoders.push_back(encoder);
    }
}

void ClipAppConfigParser::parse_faiss_config(const ryml::ConstNodeRef &root)
{
    if (!root.has_child("faiss_config"))
    {
        std::cout << "No 'faiss_config' section found in config, using defaults" << std::endl;
        return;
    }
    const auto faiss_node = root["faiss_config"];
    if (faiss_node.has_child("random_data"))
    {
        m_config.faiss_config.random_data.enabled = get_bool_value(faiss_node["random_data"], "enabled", false);
        m_config.faiss_config.random_data.num_vectors = get_int_value(faiss_node["random_data"], "num_vectors", 0);
    }
    else
    {
        std::cout << "No 'random_data' section found in 'faiss_config', using defaults" << std::endl;
    }
}

void ClipAppConfigParser::parse_pipeline_config(const ryml::ConstNodeRef &root)
{
    if (!root.has_child("pipeline_config"))
    {
        std::cout << "No 'pipeline_config' section found in config, using defaults" << std::endl;
        return;
    }

    const auto pipeline_node = root["pipeline_config"];
    if (pipeline_node.has_child("clip_quality_check_stage"))
    {
        m_config.pipeline_config.clip_quality_check_stage.enabled =
            get_bool_value(pipeline_node["clip_quality_check_stage"], "enabled", true);
    }
    else
    {
        std::cout << "No 'clip_quality_check_stage' section found in 'pipeline_config', using defaults" << std::endl;
    }

    if (pipeline_node.has_child("tracker_traffic_ctrl_stage"))
    {
        m_config.pipeline_config.tracker_traffic_ctrl_stage.unclassified_fps_to_block =
            get_int_value(pipeline_node["tracker_traffic_ctrl_stage"], "unclassified_fps_to_block", 30);
        m_config.pipeline_config.tracker_traffic_ctrl_stage.max_objects_per_second =
            get_int_value(pipeline_node["tracker_traffic_ctrl_stage"], "max_objects_per_second", 15);
    }
    else
    {
        std::cout << "No 'tracker_traffic_ctrl_stage' section found in 'pipeline_config', using defaults" << std::endl;
    }

    if (pipeline_node.has_child("video_storage_stage"))
    {
        m_config.pipeline_config.video_storage_stage.enabled =
            get_bool_value(pipeline_node["video_storage_stage"], "enabled", true);
        m_config.pipeline_config.video_storage_stage.video_segment_duration_seconds =
            get_int_value(pipeline_node["video_storage_stage"], "video_segment_duration_seconds", 6);
    }
    else
    {
        std::cout << "No 'video_storage_stage' section found in 'pipeline_config', using defaults" << std::endl;
    }
}

bool ClipAppConfigParser::parse_from_file(const std::string &configPath)
{
    try
    {
        // Read file content
        std::ifstream file(configPath);
        if (!file.is_open())
        {
            std::cerr << "Error: Cannot open config file: " << configPath << std::endl;
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string yamlContent = buffer.str();
        file.close();

        return parse_from_string(yamlContent);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error parsing config file: " << e.what() << std::endl;
        return false;
    }
}

bool ClipAppConfigParser::parse_from_string(const std::string &yamlContent)
{
    try
    {
        ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(yamlContent.c_str()));
        ryml::ConstNodeRef root = tree.rootref();

        // Parse different sections
        parse_network_configs(root);
        parse_hailort_device_config(root);
        parse_storage_config(root);
        parse_server_config(root);
        parse_frontend_source_from_file_config(root);
        parse_image_encoders_config(root);
        parse_faiss_config(root);
        parse_pipeline_config(root);
        parse_query_defaults_config(root);

        // Validate configuration
        if (!m_config.validate())
        {
            std::cerr << "Configuration validation failed" << std::endl;
            return false;
        }

        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error parsing YAML: " << e.what() << std::endl;
        return false;
    }
}

const ClipAppConfig &ClipAppConfigParser::get_config() const
{
    return m_config;
}

ClipAppConfig &ClipAppConfigParser::get_config()
{
    return m_config;
}
