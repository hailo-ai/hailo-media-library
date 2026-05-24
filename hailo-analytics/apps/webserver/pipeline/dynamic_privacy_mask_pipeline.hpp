#pragma once
#include <media_library/media_library.hpp>
#include <memory>
#include <string>
#include <vector>

#include "pipeline/pipeline.hpp"
#include "hailo_analytics/analytics/dpm_analytics.hpp"
#include "hailo_analytics/pipeline/cropping/bbox_crop_stage.hpp"
#include "common/common.hpp"
#include "hailo_analytics/pipeline/sinks/rtp_converter_stage.hpp"
#include "resources/common/events_utils.hpp"
#include "resources/common/repository.hpp"

namespace webserver
{
namespace pipeline
{
// Dynamic Privacy Mask pipeline implementation (privacy masking with segmentation)
class DynamicPrivacyMaskPipeline : public BasePipeline
{
  public:
    DynamicPrivacyMaskPipeline(webserver::resources::ResourceRepository &resources, MediaLibraryPtr media_library,
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
    void apply_label_set(std::vector<std::string> labels);

    std::shared_ptr<hailo_analytics::analytics::dpm_analytics::DetectorLabelFilter> m_detector_filter;
    std::shared_ptr<hailo_analytics::pipeline::cropping::BBoxCropStage> m_segmentor;
    std::vector<std::string> m_user_labels;
};

} // namespace pipeline
} // namespace webserver
