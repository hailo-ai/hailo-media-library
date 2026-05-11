#pragma once
#include "common/resources.hpp"
#include "media_library/media_library_types.hpp"
#include <shared_mutex>

// JSON serialization for media_library_throttling_state_t
NLOHMANN_JSON_SERIALIZE_ENUM(media_library_throttling_state_t,
                             {{media_library_throttling_state_t::THROTTLING_STATE_UNINITIALIZED, "uninitialized"},
                              {media_library_throttling_state_t::THROTTLING_STATE_FULL_PERFORMANCE, "full_performance"},
                              {media_library_throttling_state_t::THROTTLING_STATE_COOLING, "cooling"},
                              {media_library_throttling_state_t::THROTTLING_STATE_S0, "throttling_s0"},
                              {media_library_throttling_state_t::THROTTLING_STATE_S1, "throttling_s1"},
                              {media_library_throttling_state_t::THROTTLING_STATE_S2, "throttling_s2"},
                              {media_library_throttling_state_t::THROTTLING_STATE_S3, "throttling_s3"},
                              {media_library_throttling_state_t::THROTTLING_STATE_S4, "throttling_s4"}})

namespace webserver
{
namespace resources
{
class ConfigResourceBase : public Resource
{
  protected:
    nlohmann::json m_frontend_default_config;
    nlohmann::json m_encoder_osd_default_config;
    std::string m_encoder_name;

  public:
    ConfigResourceBase(std::shared_ptr<EventBus> event_bus);
    virtual ~ConfigResourceBase() = default;
    std::string name() override;
    ResourceType get_type() override;
    nlohmann::json get_frontend_default_config();
    nlohmann::json get_encoder_default_config();
    nlohmann::json get_osd_default_config();
    nlohmann::json get_osd_and_encoder_default_config();
    nlohmann::json get_hdr_default_config();
    nlohmann::json get_denoise_default_config();
    nlohmann::json get_isp_default_config();
};

class ConfigResourceMedialib : public ConfigResourceBase
{
  private:
    ProfileType m_current_profile_type;
    std::string m_current_profile_name;
    std::string m_default_profile_name;
    media_library_throttling_state_t m_current_throttling_state;
    nlohmann::json m_profile;
    nlohmann::json m_medialib_config;
    config_profile_t m_current_profile;
    std::vector<ProfileType> m_supported_profiles;
    bool m_is_hdm_mode = false;
    bool gyro_exist = false;
    mutable std::shared_mutex m_config_mutex;

    tl::expected<nlohmann::json, std::string> enable_gyro_if_exist(nlohmann::json profile);
    tl::expected<nlohmann::json, std::string> extract_frontend_config();
    tl::expected<nlohmann::json, std::string> extract_encoder_config();
    tl::expected<void, std::string> extract_profile_data(const std::string &profile_name);
    bool check_lowlight_bayer_is_hdm() const;

    nlohmann::json build_profile_response() const;

    void reset_config() override;

  public:
    ConfigResourceMedialib(std::shared_ptr<EventBus> event_bus, std::string config_path);
    ~ConfigResourceMedialib() = default;
    std::string name() override;
    void http_register(HTTPServer &srv) override;
    nlohmann::json get_current_profile();
    nlohmann::json get_current_medialib_config();

    void update_profile();
    tl::expected<nlohmann::json, std::string> get_profile(const nlohmann::json &profile_name);
    tl::expected<nlohmann::json, std::string> load_config_from_file(const std::string &file_path);
};
}; // namespace resources
} // namespace webserver
