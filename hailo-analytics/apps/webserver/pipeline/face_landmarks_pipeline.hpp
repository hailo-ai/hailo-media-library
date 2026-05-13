#pragma once
#include "pipeline/pipeline.hpp"

namespace webserver
{
namespace pipeline
{

// Face Landmarks pipeline implementation (multi-stage person/face detection and landmarks)
class FaceLandmarksPipeline : public BasePipeline
{
  public:
    FaceLandmarksPipeline(webserver::resources::ResourceRepository &resources, MediaLibraryPtr media_library,
                          RTPConverterStage &webrtc_stage, Architecture platform = Architecture::Hailo15H);
    static bool is_supported(webserver::resources::ResourceRepository &resources);
    virtual std::string pipeline_name() const override;
    void start() override;
    std::string get_profile_name_by_type(ProfileType type) const override;
    ProfileType get_profile_type_by_name(const std::string &name) const override;

  protected:
    void build_pipeline() override;
};

} // namespace pipeline
} // namespace webserver
