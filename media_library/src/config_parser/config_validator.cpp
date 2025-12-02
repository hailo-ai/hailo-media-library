#include <filesystem>
#include <fstream>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <set>
#include "config_validator.hpp"
#include "media_library_logger.hpp"
#include "files_utils.hpp"
#include "env_vars.hpp"
#include "common.hpp"

#define MODULE_NAME LoggerType::Config

const std::string MACHINE_FILE_PATH = "/sys/devices/soc0/machine";
const std::string HAILO_15_IDENTIFIER = "Hailo-15";
const std::string HAILO_15L_IDENTIFIER = "Hailo-15L";
const std::string HAILO_15_PROFILE_IDENTIFIER = "hailo15h";
const std::string HAILO_15L_PROFILE_IDENTIFIER = "hailo15l";
const std::string METADATA_FIELD = "metadata";
const std::string CONTENT_HASH_FIELD = "content_hash";
const std::string ARCHITECTURE_FIELD = "architecture";

media_library_return ConfigValidator::get_architecture(std::string &architecture)
{
    std::ifstream file(MACHINE_FILE_PATH);
    if (!file.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open machine file: {}", MACHINE_FILE_PATH);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    std::string line;
    auto file_content_opt = files_utils::read_string_from_file(MACHINE_FILE_PATH);
    if (!file_content_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to read file: {}", MACHINE_FILE_PATH);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    line = file_content_opt.value();
    auto to_lower = [](const std::string &str) {
        std::string lower_str = str;
        std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);
        return lower_str;
    };

    std::string lower_line = to_lower(line);
    if (lower_line.find(to_lower(HAILO_15L_IDENTIFIER)) != std::string::npos)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Detected architecture: {}", HAILO_15L_IDENTIFIER);
        architecture = HAILO_15L_PROFILE_IDENTIFIER;
        return MEDIA_LIBRARY_SUCCESS;
    }
    else if (lower_line.find(to_lower(HAILO_15_IDENTIFIER)) != std::string::npos)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Detected architecture: {}", HAILO_15_IDENTIFIER);
        architecture = HAILO_15_PROFILE_IDENTIFIER;
        return MEDIA_LIBRARY_SUCCESS;
    }

    return MEDIA_LIBRARY_CONFIGURATION_ERROR;
}
media_library_return ConfigValidator::to_canonical_string(const nlohmann::json &j, std::string &result)
{
    try
    {
        if (j.is_number_float())
        {
            double v = j.get<double>();
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(8);
            oss << v;
            result = oss.str();
        }
        else
        {
            result = j.dump(-1, ' ', true);
        }
        return MEDIA_LIBRARY_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to convert JSON to canonical string: {}", e.what());
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
}

media_library_return ConfigValidator::hash_json_incrementally(const nlohmann::json &j, EVP_MD_CTX *ctx)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Hashing JSON incrementally");
    try
    {
        if (j.is_object())
        {
            // Hash the opening brace '{'
            if (EVP_DigestUpdate(ctx, "{", 1) != 1)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to hash opening brace");
                return MEDIA_LIBRARY_CONFIGURATION_ERROR;
            }
            // CRITICAL: Iterate in sorted order to ensure canonical object hashing
            std::vector<std::string> keys;
            for (auto it = j.begin(); it != j.end(); ++it)
            {
                keys.push_back(it.key());
            }
            std::sort(keys.begin(), keys.end());

            for (const auto &key : keys)
            {
                // 1. Hash the key (must include quotes for canonical JSON)
                std::string key_str = nlohmann::json(key).dump();
                if (EVP_DigestUpdate(ctx, key_str.c_str(), key_str.length()) != 1)
                {
                    LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to hash key: {}", key_str);
                    return MEDIA_LIBRARY_CONFIGURATION_ERROR;
                }

                // 2. Hash the colon separator
                if (EVP_DigestUpdate(ctx, ":", 1) != 1)
                {
                    LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to hash colon separator");
                    return MEDIA_LIBRARY_CONFIGURATION_ERROR;
                }

                // 3. Recursively hash the value
                if (hash_json_incrementally(j.at(key), ctx) != MEDIA_LIBRARY_SUCCESS)
                {
                    LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to hash value for key: {}", key);
                    return MEDIA_LIBRARY_CONFIGURATION_ERROR;
                }

                // 4. Hash the comma separator (except for the last element)
                if (&key != &keys.back())
                {
                    if (EVP_DigestUpdate(ctx, ",", 1) != 1)
                    {
                        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to hash comma separator");
                        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
                    }
                }
            }

            // Hash the closing brace '}'
            if (EVP_DigestUpdate(ctx, "}", 1) != 1)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to hash closing brace");
                return MEDIA_LIBRARY_CONFIGURATION_ERROR;
            }
        }
        else if (j.is_array())
        {
            if (EVP_DigestUpdate(ctx, "[", 1) != 1)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to hash opening bracket of array");
                return MEDIA_LIBRARY_CONFIGURATION_ERROR;
            }
            for (size_t i = 0; i < j.size(); ++i)
            {
                if (hash_json_incrementally(j[i], ctx) != MEDIA_LIBRARY_SUCCESS)
                {
                    LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to hash array element at index: {}", i);
                    return MEDIA_LIBRARY_CONFIGURATION_ERROR;
                }
                if (i < j.size() - 1)
                {
                    if (EVP_DigestUpdate(ctx, ",", 1) != 1)
                    {
                        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to hash comma separator in array");
                        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
                    }
                }
            }
            if (EVP_DigestUpdate(ctx, "]", 1) != 1)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to hash closing bracket of array");
                return MEDIA_LIBRARY_CONFIGURATION_ERROR;
            }
        }
        else
        {
            std::string canonical_val;
            if (to_canonical_string(j, canonical_val) != MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to convert value to canonical string");
                return MEDIA_LIBRARY_CONFIGURATION_ERROR;
            }
            if (EVP_DigestUpdate(ctx, canonical_val.c_str(), canonical_val.length()) != 1)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to hash value: {}", canonical_val);
                return MEDIA_LIBRARY_CONFIGURATION_ERROR;
            }
        }
        return MEDIA_LIBRARY_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Exception occurred during JSON hashing: {}", e.what());
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
}

media_library_return ConfigValidator::calculate_json_hash(std::string &hash, const nlohmann::json &json)
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create EVP_MD_CTX");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to initialize SHA-256 context");
        EVP_MD_CTX_free(ctx);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    hash_json_incrementally(json, ctx);

    unsigned char hash_output[EVP_MAX_MD_SIZE];
    unsigned int hash_length = 0;
    if (EVP_DigestFinal_ex(ctx, hash_output, &hash_length) != 1)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to finalize SHA-256 hash");
        EVP_MD_CTX_free(ctx);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < hash_length; i++)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash_output[i];
    }

    hash = oss.str();
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return ConfigValidator::validate_meta_data(const nlohmann::json &json)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Validating meta data in configuration JSON");
    if (is_env_variable_on(MEDIALIB_SKIP_METADATA_CONFIG_VALIDATION))
    {
        LOGGER__MODULE__WARN(
            MODULE_NAME,
            "Skipping metadata validation as environment variable 'MEDIALIB_SKIP_METADATA_CONFIG_VALIDATION' is set");
        return MEDIA_LIBRARY_SUCCESS;
    }
    if (!json.contains(METADATA_FIELD))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "\nThe configuration file is missing the 'metadata' field.\n"
                                           "Cant validate architecture and content hash validation\n"
                                           "To enforce this validation, please add the 'metadata' field to the JSON.");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    const auto &meta_data = json.at(METADATA_FIELD);
    if (!meta_data.is_object())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "'metadata' field is not an object");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    if (!meta_data.contains(ARCHITECTURE_FIELD))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "'metadata' does not contain 'architecture' field");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "'metadata' content: {}", meta_data.dump());
    const std::string configured_architecture = meta_data[ARCHITECTURE_FIELD].get<std::string>();
    std::string actual_architecture;
    media_library_return status = get_architecture(actual_architecture);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get actual architecture");
        return status;
    }
    if (configured_architecture != actual_architecture)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Configuration architecture '{}' does not match current architecture '{}'",
                              configured_architecture, actual_architecture);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Architecture validation successful: {}", actual_architecture);
    if (!meta_data.contains(CONTENT_HASH_FIELD))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "'metadata' does not contain 'content_hash' field");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    const std::string expected_hash = meta_data[CONTENT_HASH_FIELD].get<std::string>();
    nlohmann::json json_without_meta = json;
    json_without_meta.erase(METADATA_FIELD);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to normalize JSON for hash calculation");
        return status;
    }
    std::string hash;
    LOGGER__MODULE__TRACE(MODULE_NAME,
                          "Calculating hash for JSON without metadata: ", json_without_meta.dump(-1, ' ', true));
    media_library_return hash_status = calculate_json_hash(hash, json_without_meta);
    if (hash_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to calculate JSON hash");
        return hash_status;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Calculated content hash: {}", hash);
    if (hash != expected_hash)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Content hash mismatch. Expected: {}, Calculated: {}", expected_hash, hash);
        LOGGER__MODULE__ERROR(
            MODULE_NAME,
            "\nThe configuration file's content hash does not match the expected value.\n"
            "This may indicate that the configuration has been altered or corrupted.\n"
            "To enforce this validation, please ensure the 'content_hash' field in the 'metadata' section is correct.");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Content hash validation successful: {}", hash);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return ConfigValidator::validate_meta_data(const std::string &key,
                                                         const nlohmann::json &value_coresponding_to_key)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Checking if key: {} requires meta data validation", key);
    if (std::find(files_to_check_for_meta_data.begin(), files_to_check_for_meta_data.end(), key) ==
        files_to_check_for_meta_data.end())
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Key {} not found in meta data check list", key);
        return MEDIA_LIBRARY_SUCCESS;
    }
    LOGGER__MODULE__INFO(MODULE_NAME, "Validating meta data for key: {}", key);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Content: {}", value_coresponding_to_key.dump());
    return validate_meta_data(value_coresponding_to_key);
}
