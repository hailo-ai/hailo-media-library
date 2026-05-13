#pragma once
#include "pipeline/pipeline.hpp"
#include "clip_pipeline_ai.hpp"
#include "clip_pipeline_ai_defines.hpp"

namespace webserver
{
namespace pipeline
{

// CLIP pipeline implementation (textual search capabilities)
class ClipPipeline : public BasePipeline
{
  public:
    ClipPipeline(webserver::resources::ResourceRepository &resources, MediaLibraryPtr media_library,
                 RTPConverterStage &webrtc_stage, Architecture platform = Architecture::Hailo15H);
    static bool is_supported(webserver::resources::ResourceRepository &resources);
    static const ClipAppConfig &get_clip_config();
    virtual std::string pipeline_name() const override;
    void start() override;
    void stop() override;
    void init(ProfileType profile_type) override;
    void register_endpoints() override;
    void unregister_endpoints() override;
    std::string get_profile_name_by_type(ProfileType type) const override;
    ProfileType get_profile_type_by_name(const std::string &name) const override;

  protected:
    void build_pipeline() override;

  private:
    std::shared_ptr<ClipAppConfig> m_clip_app_config;
    std::shared_ptr<ClipVideoPipeline> m_app;
    std::vector<app::ImageData> m_images; // Thumbnail gallery functionality

    bool processEmbedding(const app::EmbeddingInfo &embedding_info);
    tl::expected<void, app::ImageError> addImage(const std::string &jpeg_path, const std::string &description,
                                                 int64_t timestamp, float score);
    void clearAllImages();

    void register_config_embedded_refresh_endpoint();
    void register_config_video_playback_total_length_endpoint();
    void register_networks_endpoint();
    void register_embedding_endpoint();
    void register_video_thumbnail_clicked_endpoint();
    void register_video_segments_download_endpoint();
    void register_video_thumbnail_stop_endpoint();
    void register_storage_status_endpoint();
};

} // namespace pipeline
} // namespace webserver
