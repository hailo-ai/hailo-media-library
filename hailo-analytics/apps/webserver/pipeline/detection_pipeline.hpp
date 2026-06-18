#pragma once
#include <stddef.h>
#include <media_library/media_library.hpp>
#include <string>

#include "pipeline/pipeline.hpp"
#include "common/common.hpp"
#include "hailo_analytics/pipeline/sinks/rtp_converter_stage.hpp"
#include "resources/common/events_utils.hpp"
#include "resources/common/repository.hpp"

namespace webserver
{
namespace pipeline
{

// Detection pipeline implementation using the analytics API tiling detection generator
class DetectionPipeline : public BasePipeline
{
  public:
    DetectionPipeline(webserver::resources::ResourceRepository &resources, MediaLibraryPtr media_library,
                      RTPConverterStage &webrtc_stage, Architecture platform = Architecture::Hailo15H,
                      bool suppress_metadata_ws = false);

    virtual std::string pipeline_name() const override;
    void start() override;
    std::string get_profile_name_by_type(ProfileType type) const override;
    ProfileType get_profile_type_by_name(const std::string &name) const override;

  protected:
    void build_pipeline() override;
    void callback_handle_profile_switch(ResourceStateChangeNotification notif) override;

  private:
    size_t get_crop_every_x_frames() const;
    bool is_single_tile_mode() const;
    bool m_suppress_metadata_ws = false;
};

} // namespace pipeline
} // namespace webserver
