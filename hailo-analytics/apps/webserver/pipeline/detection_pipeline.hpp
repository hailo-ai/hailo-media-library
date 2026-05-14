#pragma once
#include "pipeline/pipeline.hpp"
#include "hailo_analytics/pipeline/routing/valve_stage.hpp"

namespace webserver
{
namespace pipeline
{

// Detection pipeline implementation using the analytics API tiling detection generator
class DetectionPipeline : public BasePipeline
{
  public:
    DetectionPipeline(webserver::resources::ResourceRepository &resources, MediaLibraryPtr media_library,
                      RTPConverterStage &webrtc_stage, Architecture platform = Architecture::Hailo15H);

    virtual std::string pipeline_name() const override;
    void start() override;
    std::string get_profile_name_by_type(ProfileType type) const override;
    ProfileType get_profile_type_by_name(const std::string &name) const override;

  protected:
    void build_pipeline() override;
    void register_endpoints() override;
    void unregister_endpoints() override;
    void callback_handle_profile_switch(ResourceStateChangeNotification notif) override;

  private:
    size_t get_crop_every_x_frames() const;
    bool is_single_tile_mode() const;
    std::shared_ptr<hailo_analytics::pipeline::routing::ValveStage> m_ws_sender_valve;
    bool m_ws_sender_enabled = false;
};

} // namespace pipeline
} // namespace webserver
