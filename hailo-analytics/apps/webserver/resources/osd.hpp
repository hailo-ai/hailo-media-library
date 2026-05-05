#pragma once
#include "common/resources.hpp"
#include "osd.hpp"
#include "configs.hpp"

namespace webserver
{
namespace resources
{
class OsdResource : public Resource
{
  public:
    template <typename T> class OsdResourceConfig
    {
        static_assert(std::is_base_of<osd::Overlay, T>::value, "T must be derived from BaseTextOverlay");

      private:
        bool enabled;
        T overlay;

      public:
        OsdResourceConfig(bool enabled, nlohmann::json params) : enabled(enabled)
        {
            osd::from_json(params, overlay);
        }
        T get_overlay()
        {
            return overlay;
        }
        bool get_enabled()
        {
            return enabled;
        }
    };

    class OsdResourceState : public ResourceState
    {
      public:
        std::vector<OsdResourceConfig<osd::TextOverlay>> text_overlays;
        std::vector<OsdResourceConfig<osd::ImageOverlay>> image_overlays;
        std::vector<OsdResourceConfig<osd::DateTimeOverlay>> datetime_overlays;
        std::string osd_config;
        OsdResourceState(nlohmann::json config);
    };
    OsdResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<ConfigResourceBase> configs);
    void http_register(HTTPServer &srv) override;
    std::string name() override;
    ResourceType get_type() override;
    nlohmann::json get_encoder_osd_config();
    std::vector<std::string> get_overlays_to_delete(nlohmann::json previouse_config, nlohmann::json new_config);

  private:
    void reset_config() override;
    nlohmann::json map_paths(nlohmann::json config);
    nlohmann::json unmap_paths(nlohmann::json config);
    int relative_font_size_to_absolut(float font_size, uint32_t width);
    float absolut_font_size_to_relative(int font_size, uint32_t width);
    nlohmann::json relative_font_size_to_absolut(nlohmann::json osd_entry, uint32_t width);
    nlohmann::json absolut_font_size_to_relative(nlohmann::json osd_entry, uint32_t width);
    nlohmann::json map_overlays(nlohmann::json config, std::function<nlohmann::json(nlohmann::json)> transform_osd);
    std::vector<std::string> get_all_overlays_ids();
    void update_osds(uint32_t new_width, uint32_t new_height);
    void register_get_osd_endpoint(HTTPServer &srv);
    void register_get_formats_endpoint(HTTPServer &srv);
    void register_get_images_endpoint(HTTPServer &srv);
    void register_patch_osd_endpoint(HTTPServer &srv);
    void register_put_osd_endpoint(HTTPServer &srv);
    void register_upload_endpoint(HTTPServer &srv);
    bool m_initialize;

    struct resolution_conf_t
    {
        uint32_t width;
        uint32_t height;
        rotation_angle_t rotation;
    };

    resolution_conf_t m_resolution_conf;
    rotation_angle_t m_default_rotation;
};
} // namespace resources
} // namespace webserver
