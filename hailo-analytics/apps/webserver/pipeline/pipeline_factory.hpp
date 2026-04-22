#pragma once

#include "hailo_analytics/pipeline/core/stage.hpp"
#include "pipeline.hpp"
#include "resources/common/repository.hpp"
#include "common/common.hpp"
#include <memory>
#include <mutex>

using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline::overlay;
using namespace hailo_analytics::pipeline::routing;
using namespace hailo_analytics::pipeline;

namespace webserver::pipeline
{
class PipelineFactory
{
  public:
    PipelineFactory(webserver::resources::ResourceRepository &resources, Architecture platform,
                    const pipeline_t &initial_pipeline_type = pipeline_t::Basic);
    ~PipelineFactory();
    BasePipeline *get_current_pipeline();
    pipeline_t get_current_pipeline_type();
    std::vector<pipeline_t> get_supported_pipeline_types() const;
    hailo_analytics::pipeline::AppStatus set_override_persistent_settings(bool value);

  private:
    void handle_pipeline_change_event(ResourceStateChangeNotification notification);
    std::unique_ptr<BasePipeline> create_pipeline(const pipeline_t &pipeline_type);
    AppStatus switch_pipeline(const pipeline_t &pipeline_type, bool start_pipeline = true);
    void register_endpoints();

  private:
    webserver::resources::ResourceRepository &m_resources;
    Architecture m_platform;
    pipeline_t m_current_pipeline_type;
    std::vector<pipeline_t> m_supported_pipelines;
    std::shared_ptr<MediaLibrary> m_media_library;
    std::unique_ptr<BasePipeline> m_current_pipeline;
    mutable std::mutex m_pipeline_mutex;
    std::unique_ptr<RTPConverterStage> m_webrtc_stage;
};

} // namespace webserver::pipeline
