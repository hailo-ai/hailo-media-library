#pragma once
#include "common/resources.hpp"
#include "media_library/privacy_mask_types.hpp"
#include "media_library/privacy_mask.hpp"
#include "media_library/media_library_types.hpp"
#include "configs.hpp"

using namespace privacy_mask_types;

namespace webserver
{
namespace resources
{
class PrivacyMaskResource : public Resource
{

  private:
    struct normalized_vertex
    {
        double x, y;
        normalized_vertex(double x, double y);
        normalized_vertex();
    };
    struct normalized_polygon
    {
        std::string id;
        std::vector<normalized_vertex> vertices;
    };
    struct Resolution
    {
        uint32_t width;
        uint32_t height;
    };
    std::map<std::string, normalized_polygon> m_privacy_masks;
    std::map<std::string, normalized_polygon> m_original_privacy_masks;
    Resolution m_frame;
    flip_direction_t m_flip;
    rotation_angle_t m_rotation;
    std::vector<std::string> get_enabled_masks();
    void initialize_from_config(std::shared_ptr<webserver::resources::ConfigResourceBase> configs);
    void parse_polygon(nlohmann::json j);
    void reset_config() override;
    normalized_vertex flip_rotate_point(const normalized_vertex &p);
    normalized_vertex reverse_flip_rotate_point(const normalized_vertex &p);
    void adjust_privacy_masks();
    nlohmann::json flip_rotate_json(const nlohmann::json &j);

  public:
    class PrivacyMaskResourceState : public ResourceState
    {
      public:
        std::map<std::string, polygon> masks;
        std::vector<std::string> changed_to_enabled;
        std::vector<std::string> changed_to_disabled;
        std::vector<std::string> polygon_to_update;
        std::vector<std::string> polygon_to_delete;
        std::optional<size_t> pixelization_size;
        std::optional<rgb_color_t> color;

        PrivacyMaskResourceState(std::map<std::string, normalized_polygon> masks, Resolution frame,
                                 rotation_angle_t rotation, std::vector<std::string> changed_to_enabled,
                                 std::vector<std::string> changed_to_disabled,
                                 std::vector<std::string> polygon_to_update, std::vector<std::string> polygon_to_delete,
                                 std::optional<size_t> pixelization_size = std::nullopt,
                                 std::optional<rgb_color_t> color = std::nullopt);

        PrivacyMaskResourceState(std::map<std::string, normalized_polygon> masks, Resolution frame,
                                 rotation_angle_t rotation, std::optional<size_t> pixelization_size = std::nullopt,
                                 std::optional<rgb_color_t> color = std::nullopt);

      private:
        polygon norm_to_absolut(const normalized_polygon &norm_polygon, const Resolution &frame,
                                rotation_angle_t rotation);
        void populate_masks(const std::map<std::string, normalized_polygon> &masks, const Resolution &frame,
                            rotation_angle_t rotation);
    };

    PrivacyMaskResource(std::shared_ptr<EventBus> event_bus,
                        std::shared_ptr<webserver::resources::ConfigResourceBase> configs);
    void http_register(HTTPServer &srv) override;
    std::string name() override;
    ResourceType get_type() override;
    std::map<std::string, normalized_polygon> get_privacy_masks();
    void renable_masks();

  private:
    std::shared_ptr<PrivacyMaskResourceState> parse_state(std::vector<std::string> current_enabled,
                                                          std::vector<std::string> prev_enabled, nlohmann::json diff);
    std::shared_ptr<PrivacyMaskResourceState> update_all_vertices_state();
    std::shared_ptr<webserver::resources::PrivacyMaskResource::PrivacyMaskResourceState> delete_masks_from_config(
        nlohmann::json config);
};
} // namespace resources
} // namespace webserver
