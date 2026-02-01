// filepath: /home/nitzano/GitRepositories/tappas/apps/h15/native/webserver/pipeline/pipeline_factory.hpp
#pragma once

#include "pipeline.hpp"
#include "resources/common/event_bus.hpp"
#include "resources/common/repository.hpp"
#include "common/common.hpp"
#include <memory>
#include <string>
#include <mutex>

using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline::overlay;
using namespace hailo_analytics::pipeline::routing;

namespace webserver::pipeline
{
class PipelineFactory
{
  public:
    PipelineFactory(WebserverResourceRepository resources, Architecture platform,
                    const pipeline_t &initial_pipeline_type = pipeline_t::Basic);
    ~PipelineFactory();
    std::shared_ptr<BasePipeline> get_current_pipeline();
    pipeline_t get_current_pipeline_type();
    std::vector<pipeline_t> get_supported_pipeline_types() const;

  private:
    void handle_pipeline_change_event(ResourceStateChangeNotification notification);
    std::shared_ptr<BasePipeline> create_pipeline(const pipeline_t &pipeline_type);
    AppStatus switch_pipeline(const pipeline_t &pipeline_type, bool start_pipeline = true);
    void register_endpoints();

  private:
    WebserverResourceRepository m_resources;
    Architecture m_platform;
    pipeline_t m_current_pipeline_type;
    std::vector<pipeline_t> m_supported_pipelines;
    std::shared_ptr<BasePipeline> m_current_pipeline;
    std::shared_ptr<MediaLibrary> m_media_library;
    mutable std::mutex m_pipeline_mutex;
    std::shared_ptr<RTPConverterStage> m_webrtc_stage;
};

} // namespace webserver::pipeline
