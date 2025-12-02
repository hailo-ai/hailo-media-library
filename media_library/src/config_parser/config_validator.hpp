#pragma once
#include <nlohmann/json-schema.hpp>
#include <openssl/evp.h>
#include "media_library_types.hpp"

class ConfigValidator
{
  public:
    media_library_return validate_meta_data(const nlohmann::json &json);
    media_library_return validate_meta_data(const std::string &key, const nlohmann::json &value_coresponding_to_key);

  private:
    media_library_return get_architecture(std::string &architecture);
    media_library_return calculate_json_hash(std::string &hash, const nlohmann::json &json);
    media_library_return to_canonical_string(const nlohmann::json &j, std::string &result);
    media_library_return hash_json_incrementally(const nlohmann::json &j, EVP_MD_CTX *ctx);
    std::array<std::string, 8> files_to_check_for_meta_data = {
        "sensor_config", "application_settings",   "stabilizer_settings", "iq_settings", "encoding", "osd",
        "masking",       "sensor_calibration_file"};
};
