#include "resources.hpp"
#include <iostream>

webserver::resources::OsdResource::OsdResource() : Resource()
{
    m_type = ResourceType::RESOURCE_OSD;
    m_default_config = R"(
    {
        "image": [
            {
                "id": "example_image",
                "image_path": "/home/root/apps/detection/resources/configs/osd_hailo_static_image.png",
                "width": 0.2,
                "height": 0.13,
                "x": 0.76,
                "y": 0.3,
                "z-index": 1,
                "angle": 0,
                "rotation_policy": "CENTER"
            }
        ],
        "dateTime": [
            {
                "id": "example_datetime",
                "font_size": 2,
                "line_thickness": 3,
                "rgb": [
                    255,
                    0,
                    0
                ],
                "x": 0.1,
                "y": 0.7,
                "z-index": 3,
                "angle": 0,
                "rotation_policy": "CENTER"
            }
        ],
        "text": [
            {
                "id": "example_text1",
                "label": "HailoAI",
                "font_size": 100,
                "line_thickness": 3,
                "rgb": [
                    255,
                    255,
                    255
                ],
                "x": 0.7,
                "y": 0.05,
                "z-index": 2,
                "font_path": "/usr/share/fonts/ttf/LiberationMono-Regular.ttf",
                "angle": 0,
                "rotation_policy": "CENTER"
            },
            {
                "id": "example_text2",
                "label": "camera name",
                "font_size": 100,
                "line_thickness": 3,
                "rgb": [
                    102,
                    0,
                    51
                ],
                "x": 0.05,
                "y": 0.1,
                "z-index": 1,
                "font_path": "/usr/share/fonts/ttf/LiberationMono-Regular.ttf",
                "angle": 0,
                "rotation_policy": "CENTER"
            }
        ]
    })";
    m_config = nlohmann::json::parse(m_default_config);
}

void webserver::resources::OsdResource::http_register(std::shared_ptr<httplib::Server> srv)
{
    srv->Get("/osd", [this](const httplib::Request &, httplib::Response &res)
             { res.set_content(this->to_string(), "application/json"); });

    srv->Patch("/osd", [this](const httplib::Request &req, httplib::Response &res)
               {
                    auto partial_config = nlohmann::json::parse(req.body);
                    m_config.merge_patch(partial_config);
                    res.set_content(this->to_string(), "application/json");
                    on_resource_change(std::make_shared<webserver::resources::ResourceState>(ConfigResourceState(this->to_string()))); });

    srv->Put("/osd", [this](const httplib::Request &req, httplib::Response &res)
             {
                auto config = nlohmann::json::parse(req.body);
                auto partial_config = nlohmann::json::diff(m_config, config);
                m_config = m_config.patch(partial_config);
                res.set_content(this->to_string(), "application/json");
                on_resource_change(std::make_shared<webserver::resources::ResourceState>(ConfigResourceState(this->to_string()))); });
}