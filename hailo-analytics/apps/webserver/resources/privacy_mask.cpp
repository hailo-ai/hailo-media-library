#include "privacy_mask.hpp"
#include <iostream>
#include <cmath> // For sin, cos

using namespace webserver::resources;

PrivacyMaskResource::normalized_vertex::normalized_vertex(double x, double y) : x(x), y(y)
{
}

PrivacyMaskResource::normalized_vertex::normalized_vertex() : x(0), y(0)
{
}

std::string PrivacyMaskResource::name()
{
    return "privacy_mask";
}

ResourceType PrivacyMaskResource::get_type()
{
    return ResourceType::RESOURCE_PRIVACY_MASK;
}

std::map<std::string, PrivacyMaskResource::normalized_polygon> PrivacyMaskResource::get_privacy_masks()
{
    return m_privacy_masks;
}

PrivacyMaskResource::PrivacyMaskResource(std::shared_ptr<EventBus> event_bus,
                                         std::shared_ptr<webserver::resources::ConfigResourceBase> configs)
    : Resource(event_bus), m_privacy_masks{}, m_original_privacy_masks{}
{
    m_default_config = "{}";
    m_config = nlohmann::json::parse(m_default_config);
    subscribe_callback(EventType::PIPELINE_READY, EventPriority::EVENT_PRIORITY_HIGH,
                       [this, configs](ResourceStateChangeNotification /*notification*/) {
                           WEBSERVER_LOG_INFO("Received PIPELINE_READY notification");
                           this->initialize_from_config(configs);
                       });

    WEBSERVER_LOG_INFO("PrivacyMaskResource initialized with default values");

    subscribe_callback(EventType::CHANGE_FLIP, [this](ResourceStateChangeNotification notification) {
        WEBSERVER_LOG_INFO("Received CHANGE_FLIP notification");
        auto state = notification.getDirectResourceState<ProfileFlipState>();
        if (flip_string_map.find(state->value) == flip_string_map.end())
        {
            WEBSERVER_LOG_ERROR("Invalid flip value: {}", state->value);
            throw std::runtime_error("Invalid flip value");
        }
        m_flip = flip_string_map.at(state->value);
        adjust_privacy_masks();
    });
    subscribe_callback(EventType::CHANGE_ROTATION, [this](ResourceStateChangeNotification notification) {
        WEBSERVER_LOG_INFO("Received CHANGE_ROTATION notification");
        auto state = notification.getDirectResourceState<ProfileRotationState>();
        if (rotation_string_map.find(state->value) == rotation_string_map.end())
        {
            WEBSERVER_LOG_ERROR("Invalid rotation value: {}", state->value);
            throw std::runtime_error("Invalid rotation value");
        }
        m_rotation = rotation_string_map.at(state->value);
        adjust_privacy_masks();
    });
    subscribe_callback(EventType::CHANGE_RESOLUTION, [this](ResourceStateChangeNotification notification) {
        WEBSERVER_LOG_INFO("Received CHANGE_RESOLUTION notification");
        auto state = notification.getDirectResourceState<ProfileResolutionState>();
        m_frame.width = webserver::common::resolution_map.at(string_to_resolution(state->value)).first;
        m_frame.height = webserver::common::resolution_map.at(string_to_resolution(state->value)).second;
        WEBSERVER_LOG_INFO("New resolution: {}x{}", m_frame.width, m_frame.height);
        adjust_privacy_masks();
    });
}

void PrivacyMaskResource::initialize_from_config(std::shared_ptr<webserver::resources::ConfigResourceBase> configs)
{
    WEBSERVER_LOG_INFO("Initializing from config");
    nlohmann::json frontend_conf = configs->get_frontend_default_config();
    m_flip = flip_string_map.at(frontend_conf["flip"]["direction"]);
    m_rotation = rotation_string_map.at(frontend_conf["rotation"]["angle"]);
    m_frame.width = frontend_conf["input_video"]["resolution"]["width"];
    m_frame.height = frontend_conf["input_video"]["resolution"]["height"];
}

void PrivacyMaskResource::adjust_privacy_masks()
{
    m_privacy_masks.clear();
    for (auto &[key, original_polygon] : m_original_privacy_masks)
    {
        PrivacyMaskResource::normalized_polygon flip_rotated_polygon;
        flip_rotated_polygon.id = original_polygon.id;
        for (auto &vertex : original_polygon.vertices)
        {
            flip_rotated_polygon.vertices.push_back(flip_rotate_point(vertex));
        }
        m_privacy_masks[key] = flip_rotated_polygon;
    }

    for (auto &mask : m_config["masks"])
    {
        for (size_t i = 0; i < mask["Polygon"].size(); ++i)
        {
            mask["Polygon"][i]["x"] = m_privacy_masks[mask["id"].get<std::string>()].vertices[i].x;
            mask["Polygon"][i]["y"] = m_privacy_masks[mask["id"].get<std::string>()].vertices[i].y;
        }
    }

    on_resource_change(EventType::CHANGED_RESOURCE_PRIVACY_MASK, update_all_vertices_state());
}

nlohmann::json PrivacyMaskResource::flip_rotate_json(const nlohmann::json &j)
{
    WEBSERVER_LOG_INFO("Flipping and rotating json");
    nlohmann::json flipped_json = j;
    for (auto &mask : flipped_json["masks"])
    {
        for (size_t i = 0; i < mask["Polygon"].size(); ++i)
        {
            PrivacyMaskResource::normalized_vertex norm_v(mask["Polygon"][i]["x"].get<double>(),
                                                          mask["Polygon"][i]["y"].get<double>());
            mask["Polygon"][i]["x"] = flip_rotate_point(norm_v).x;
            mask["Polygon"][i]["y"] = flip_rotate_point(norm_v).y;
        }
    }
    return flipped_json;
}

void PrivacyMaskResource::reset_config()
{
    WEBSERVER_LOG_INFO("Resetting config");
    auto state = delete_masks_from_config(m_config);
    on_resource_change(EventType::CHANGED_RESOURCE_PRIVACY_MASK, state);
    m_config = nlohmann::json::parse(m_default_config);
    m_privacy_masks.clear();
    m_original_privacy_masks.clear();
}

PrivacyMaskResource::normalized_vertex PrivacyMaskResource::flip_rotate_point(
    const PrivacyMaskResource::normalized_vertex &p)
{
    WEBSERVER_LOG_INFO("Rotating + Flipping point ");

    auto original_point = p;
    PrivacyMaskResource::normalized_vertex rotated_point(0.0, 0.0);
    switch (m_rotation)
    {
    case ROTATION_ANGLE_0:
        rotated_point.x = original_point.x;
        rotated_point.y = original_point.y;
        break;
    case ROTATION_ANGLE_90:
        rotated_point.x = 1.0 - original_point.y;
        rotated_point.y = original_point.x;
        break;
    case ROTATION_ANGLE_180:
        rotated_point.x = 1.0 - original_point.x;
        rotated_point.y = 1.0 - original_point.y;
        break;
    case ROTATION_ANGLE_270:
        rotated_point.x = original_point.y;
        rotated_point.y = 1.0 - original_point.x;
        break;
    default:
        WEBSERVER_LOG_ERROR("Invalid rotation angle");
        break;
    }

    PrivacyMaskResource::normalized_vertex flipped_point(0.0, 0.0);

    switch (m_flip)
    {
    case FLIP_DIRECTION_NONE:
        flipped_point.x = rotated_point.x;
        flipped_point.y = rotated_point.y;
        break;
    case FLIP_DIRECTION_HORIZONTAL:
        flipped_point.x = 1.0 - rotated_point.x;
        flipped_point.y = rotated_point.y;
        break;
    case FLIP_DIRECTION_VERTICAL:
        flipped_point.x = rotated_point.x;
        flipped_point.y = 1.0 - rotated_point.y;
        break;
    case FLIP_DIRECTION_BOTH:
        flipped_point.x = 1.0 - rotated_point.x;
        flipped_point.y = 1.0 - rotated_point.y;
        break;
    default:
        WEBSERVER_LOG_ERROR("Invalid flip direction");
        break;
    };

    return flipped_point;
}
PrivacyMaskResource::normalized_vertex PrivacyMaskResource::reverse_flip_rotate_point(
    const PrivacyMaskResource::normalized_vertex &p)
{
    WEBSERVER_LOG_INFO("Reverse Rotating + Flipping point ");

    auto original_point = p;
    PrivacyMaskResource::normalized_vertex flipped_point(0, 0);

    switch (m_flip)
    {
    case FLIP_DIRECTION_NONE:
        flipped_point.x = original_point.x;
        flipped_point.y = original_point.y;
        break;
    case FLIP_DIRECTION_HORIZONTAL:
        flipped_point.x = 1.0 - original_point.x;
        flipped_point.y = original_point.y;
        break;
    case FLIP_DIRECTION_VERTICAL:
        flipped_point.x = original_point.x;
        flipped_point.y = 1.0 - original_point.y;
        break;
    case FLIP_DIRECTION_BOTH:
        flipped_point.x = 1.0 - original_point.x;
        flipped_point.y = 1.0 - original_point.y;
        break;
    default:
        WEBSERVER_LOG_ERROR("Invalid flip direction");
        break;
    };

    PrivacyMaskResource::normalized_vertex rotated_point(0.0, 0.0);

    switch (m_rotation)
    {
    case ROTATION_ANGLE_0:
        rotated_point.x = flipped_point.x;
        rotated_point.y = flipped_point.y;
        break;
    case ROTATION_ANGLE_90:
        rotated_point.x = flipped_point.y;
        rotated_point.y = 1.0 - flipped_point.x;
        break;
    case ROTATION_ANGLE_180:
        rotated_point.x = 1.0 - flipped_point.x;
        rotated_point.y = 1.0 - flipped_point.y;
        break;
    case ROTATION_ANGLE_270:
        rotated_point.x = 1.0 - flipped_point.y;
        rotated_point.y = flipped_point.x;
        break;
    default:
        WEBSERVER_LOG_ERROR("Invalid rotation angle");
        break;
    }

    return rotated_point;
}

std::shared_ptr<PrivacyMaskResource::PrivacyMaskResourceState> PrivacyMaskResource::update_all_vertices_state()
{
    WEBSERVER_LOG_INFO("Updating all vertices state");
    return this->parse_state(this->get_enabled_masks(), this->get_enabled_masks(), nlohmann::json::parse("{}"));
}

void PrivacyMaskResource::renable_masks()
{
    WEBSERVER_LOG_INFO("Re-enabling masks");
    auto state = std::make_shared<PrivacyMaskResource::PrivacyMaskResourceState>(
        m_privacy_masks, m_frame, m_rotation, this->get_enabled_masks(), std::vector<std::string>{},
        std::vector<std::string>{}, std::vector<std::string>{});
    on_resource_change(EventType::CHANGED_RESOURCE_PRIVACY_MASK, state);
}

std::shared_ptr<PrivacyMaskResource::PrivacyMaskResourceState> PrivacyMaskResource::parse_state(
    std::vector<std::string> current_enabled, std::vector<std::string> prev_enabled, nlohmann::json diff)
{
    WEBSERVER_LOG_INFO("Parsing state with current enabled masks: {} and previous enabled masks: {}",
                       current_enabled.size(), prev_enabled.size());

    std::optional<int> pixelization_size = m_config.contains("pixelization_size")
                                               ? std::optional<int>(m_config["pixelization_size"].get<int>())
                                               : std::nullopt;
    std::optional<rgb_color_t> color = std::nullopt;
    if (m_config.contains("color"))
    {
        auto color_json = m_config["color"];
        if (color_json.is_array() && color_json.size() == 3)
        {
            color = {
                color_json[0].get<uint8_t>(),
                color_json[1].get<uint8_t>(),
                color_json[2].get<uint8_t>(),
            };
        }
        else
        {
            WEBSERVER_LOG_ERROR("Invalid color format in config - ignoring");
        }
    }

    WEBSERVER_LOG_INFO("Parsed blur radius: {} and color: {}",
                       pixelization_size.has_value() ? std::to_string(pixelization_size.value()) : "null",
                       color.has_value() ? std::to_string(color->r) + ", " + std::to_string(color->g) + ", " +
                                               std::to_string(color->b)
                                         : "null");

    if (pixelization_size.has_value() && color.has_value())
    {
        throw std::invalid_argument("Both blur radius and color are set in the config - only one should be specified");
    }

    auto state = std::make_shared<PrivacyMaskResource::PrivacyMaskResourceState>(m_privacy_masks, m_frame, m_rotation,
                                                                                 pixelization_size, color);
    for (auto &mask : m_privacy_masks)
    {
        if (std::find(current_enabled.begin(), current_enabled.end(), mask.first) != current_enabled.end() &&
            std::find(prev_enabled.begin(), prev_enabled.end(), mask.first) == prev_enabled.end())
        {
            WEBSERVER_LOG_DEBUG("creating state that will enable mask: {}", mask.first);
            state->changed_to_enabled.push_back(mask.first);
        }
        if (std::find(current_enabled.begin(), current_enabled.end(), mask.first) == current_enabled.end() &&
            std::find(prev_enabled.begin(), prev_enabled.end(), mask.first) != prev_enabled.end())
        {

            WEBSERVER_LOG_DEBUG("creating state that will disabled mask: {}", mask.first);
            state->changed_to_disabled.push_back(mask.first);
        }
        if (std::find(current_enabled.begin(), current_enabled.end(), mask.first) != current_enabled.end() &&
            std::find(prev_enabled.begin(), prev_enabled.end(), mask.first) != prev_enabled.end())
        {
            // update all the polyagons that are still on the screen
            WEBSERVER_LOG_DEBUG("creating state that will update mask: {}", mask.first);
            state->polygon_to_update.push_back(mask.first);
        }
    }
    for (auto &entry : diff)
    {
        if (entry["op"] == "remove")
        {
            WEBSERVER_LOG_DEBUG("deleting from json polygon: {}", entry["path"].get<std::string>());
            state->polygon_to_delete.push_back(entry["path"].get<std::string>());
            m_privacy_masks.erase(entry["path"].get<std::string>());
            m_original_privacy_masks.erase(entry["path"].get<std::string>());
        }
    }
    return state;
}

std::vector<std::string> PrivacyMaskResource::get_enabled_masks()
{
    WEBSERVER_LOG_INFO("Getting enabled masks");
    std::vector<std::string> enabled;
    for (const auto &mask : m_config["masks"])
    {
        if (mask["status"].get<bool>() && m_config["global_enable"].get<bool>())
        {
            enabled.push_back(mask["id"].get<std::string>());
        }
    }
    return enabled;
}

void PrivacyMaskResource::parse_polygon(nlohmann::json j)
{
    try
    {
        WEBSERVER_LOG_INFO("Parsing polygon from JSON");
        for (const auto &mask : j["masks"])
        {
            PrivacyMaskResource::normalized_polygon flip_rotated_polygon, original_polygon;
            flip_rotated_polygon.id = mask["id"].get<std::string>();
            original_polygon.id = mask["id"].get<std::string>();
            if (mask["Polygon"].empty())
            {
                WEBSERVER_LOG_WARNING("Got polygon without points in privacy mask, skipping");
                continue;
            }
            for (const auto &point : mask["Polygon"])
            {
                PrivacyMaskResource::normalized_vertex norm_v(point["x"].get<double>(), point["y"].get<double>());

                // the polygon coordinates we got are adjusted to the flipped/rotated frame
                // calculate the normalized polygon by reversing the frame transformation
                flip_rotated_polygon.vertices.push_back(norm_v);
                original_polygon.vertices.push_back(reverse_flip_rotate_point(norm_v));
            }
            m_original_privacy_masks[original_polygon.id] = original_polygon;
            m_privacy_masks[flip_rotated_polygon.id] = flip_rotated_polygon;
        }
    }
    catch (const std::exception &e)
    {
        WEBSERVER_LOG_ERROR("Failed to parse json body for privacy mask, no change has been made: {}", e.what());
    }
}

std::shared_ptr<PrivacyMaskResource::PrivacyMaskResourceState> PrivacyMaskResource::delete_masks_from_config(
    nlohmann::json config)
{
    WEBSERVER_LOG_INFO("Deleting masks from config");
    std::shared_ptr<PrivacyMaskResource::PrivacyMaskResourceState> state =
        std::make_shared<PrivacyMaskResource::PrivacyMaskResourceState>(m_privacy_masks, m_frame, m_rotation);
    for (const auto &mask_to_del : config["masks"])
    {
        std::string id = mask_to_del["id"].get<std::string>();
        if (m_config.contains("masks"))
        {
            if (mask_to_del["status"].get<bool>() && m_config["global_enable"].get<bool>())
            {
                state->polygon_to_delete.push_back(id);
            }
            m_config["masks"].erase(
                std::remove_if(m_config["masks"].begin(), m_config["masks"].end(),
                               [id](const nlohmann::json &mask) { return mask["id"].get<std::string>() == id; }),
                m_config["masks"].end());
        }
    }
    return state;
}

void PrivacyMaskResource::http_register(HTTPServer &srv)
{
    WEBSERVER_LOG_INFO("Registering HTTP endpoints for PrivacyMaskResource");

    srv.Get("/privacy_mask", std::function<nlohmann::json()>([this]() {
                WEBSERVER_LOG_INFO("GET /privacy_mask called");
                return this->m_config;
            }));

    srv.Patch("/privacy_mask", [this](const nlohmann::json &partial_config) {
        WEBSERVER_LOG_INFO("PATCH /privacy_mask called");
        auto prev_enabled = this->get_enabled_masks();
        nlohmann::json diff = nlohmann::json::diff(m_config, partial_config);
        m_config.merge_patch(partial_config);
        this->parse_polygon(m_config);
        on_resource_change(EventType::CHANGED_RESOURCE_PRIVACY_MASK,
                           this->parse_state(this->get_enabled_masks(), prev_enabled, diff));
        return this->m_config;
    });

    srv.Put("/privacy_mask", [this](const nlohmann::json &config) {
        WEBSERVER_LOG_INFO("PUT /privacy_mask called");
        auto prev_enabled = this->get_enabled_masks();
        auto partial_config = nlohmann::json::diff(m_config, config);
        m_config = m_config.patch(partial_config);
        this->parse_polygon(m_config);
        on_resource_change(EventType::CHANGED_RESOURCE_PRIVACY_MASK,
                           this->parse_state(this->get_enabled_masks(), prev_enabled, partial_config));
        return this->m_config;
    });

    srv.Delete("/privacy_mask", [this](const nlohmann::json &config) {
        WEBSERVER_LOG_INFO("DELETE /privacy_mask called");
        auto state = delete_masks_from_config(config);
        on_resource_change(EventType::CHANGED_RESOURCE_PRIVACY_MASK, state);
        return this->m_config;
    });
}

polygon PrivacyMaskResource::PrivacyMaskResourceState::norm_to_absolut(
    const PrivacyMaskResource::normalized_polygon &norm_polygon, const PrivacyMaskResource::Resolution &frame,
    rotation_angle_t rotation)
{
    polygon poly;
    poly.id = norm_polygon.id;
    std::vector<vertex> vertices;
    auto height = frame.height;
    auto width = frame.width;
    if ((rotation == ROTATION_ANGLE_90 || rotation == ROTATION_ANGLE_270))
    {
        height = frame.width;
        width = frame.height;
    }
    for (const auto &v : norm_polygon.vertices)
    {
        if (v.x < 0 || v.x > 1 || v.y < 0 || v.y > 1)
        {
            WEBSERVER_LOG_ERROR("Invalid polygon vertex coordinates: ({}, {})", v.x, v.y);
            throw std::runtime_error("Invalid polygon vertex coordinates");
        }
        poly.vertices.push_back(vertex(v.x * width, v.y * height));
    }
    return poly;
}

PrivacyMaskResource::PrivacyMaskResourceState::PrivacyMaskResourceState(
    std::map<std::string, normalized_polygon> masks, Resolution frame, rotation_angle_t rotation,
    std::vector<std::string> changed_to_enabled, std::vector<std::string> changed_to_disabled,
    std::vector<std::string> polygon_to_update, std::vector<std::string> polygon_to_delete,
    std::optional<size_t> pixelization_size, std::optional<rgb_color_t> color)
    : changed_to_enabled(std::move(changed_to_enabled)), changed_to_disabled(std::move(changed_to_disabled)),
      polygon_to_update(std::move(polygon_to_update)), polygon_to_delete(std::move(polygon_to_delete)),
      pixelization_size(pixelization_size), color(color)
{
    populate_masks(masks, frame, rotation);
}

PrivacyMaskResource::PrivacyMaskResourceState::PrivacyMaskResourceState(std::map<std::string, normalized_polygon> masks,
                                                                        Resolution frame, rotation_angle_t rotation,
                                                                        std::optional<size_t> pixelization_size,
                                                                        std::optional<rgb_color_t> color)
    : changed_to_enabled{}, changed_to_disabled{}, polygon_to_update{}, polygon_to_delete{},
      pixelization_size(pixelization_size), color(color)
{
    populate_masks(masks, frame, rotation);
}

void PrivacyMaskResource::PrivacyMaskResourceState::populate_masks(
    const std::map<std::string, normalized_polygon> &masks, const Resolution &frame, rotation_angle_t rotation)
{
    for (const auto &[id, norm_polygon] : masks)
    {
        this->masks[id] = norm_to_absolut(norm_polygon, frame, rotation);
    }
}
