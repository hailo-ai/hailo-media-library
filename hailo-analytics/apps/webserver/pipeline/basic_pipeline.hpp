#pragma once
#include "pipeline/pipeline.hpp"

namespace webserver
{
namespace pipeline
{

// Basic pipeline implementation (simple streaming without AI)
class BasicPipeline : public BasePipeline
{
  public:
    BasicPipeline(webserver::resources::ResourceRepository &resources, MediaLibraryPtr media_library,
                  RTPConverterStage &webrtc_stage, Architecture platform = Architecture::Hailo15H);

    virtual std::string pipeline_name() const override;
    void start() override;
    std::string get_profile_name_by_type(ProfileType type) const override;
    ProfileType get_profile_type_by_name(const std::string &name) const override;

  protected:
    void build_pipeline() override;
};

} // namespace pipeline
} // namespace webserver
