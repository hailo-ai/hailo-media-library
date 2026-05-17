#pragma once
#include "pipeline/pipeline.hpp"

namespace webserver
{
namespace pipeline
{

class ProfileManagerPipeline : public BasePipeline
{
  public:
    ProfileManagerPipeline(webserver::resources::ResourceRepository &resources, MediaLibraryPtr media_library,
                           RTPConverterStage &webrtc_stage, Architecture platform = Architecture::Hailo15H);

    virtual std::string pipeline_name() const override;
    ~ProfileManagerPipeline() override;
    void start() override;
    void stop() override;
    void stop(bool full_shutdown = true);
    std::string get_profile_name_by_type(ProfileType type) const override;
    ProfileType get_profile_type_by_name(const std::string &name) const override;
    void register_endpoints() override;
    void unregister_endpoints() override;

  protected:
    void build_pipeline() override;
    void update_fps(uint32_t fps, config_profile_t &profile_config) override;
    void update_resolution(const std::string &resolution, config_profile_t &profile_config) override;
    void update_rotation(const std::string &rotation, config_profile_t &profile_config) override;
    hailo_encoder_config_t get_encoder_config() override;
    void callback_handle_encoder(ResourceStateChangeNotification notif) override;

  private:
    bool is_jpeg_encoder() const;
};

} // namespace pipeline
} // namespace webserver
