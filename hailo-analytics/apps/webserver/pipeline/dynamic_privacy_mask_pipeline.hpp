#pragma once
#include <atomic>
#include <memory>
#include "pipeline/pipeline.hpp"

namespace webserver
{
namespace pipeline
{
// Dynamic Privacy Mask pipeline implementation (privacy masking with segmentation)
class DynamicPrivacyMaskPipeline : public BasePipeline
{
  public:
    DynamicPrivacyMaskPipeline(webserver::resources::ResourceRepository &resources, MediaLibrary &media_library,
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
    std::shared_ptr<std::atomic<int>> m_shared_max_detections;
};

} // namespace pipeline
} // namespace webserver
