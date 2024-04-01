#include "resources.hpp"
#include <iostream>

webserver::resources::FrontendResource::FrontendResource(std::shared_ptr<webserver::resources::AiResource> ai_res) : Resource()
{
    m_default_config = R"(
    {
        "output_video": {
            "method": "INTERPOLATION_TYPE_BILINEAR",
            "format": "IMAGE_FORMAT_NV12",
            "grayscale": false,
            "resolutions": [
                {
                    "width": 3840,
                    "height": 2160,
                    "framerate": 30,
                    "pool_max_buffers": 20
                },
                {
                    "width": 640,
                    "height": 640,
                    "framerate": 30,
                    "pool_max_buffers": 20
                }
            ]
        },
        "dewarp": {
            "enabled": false,
            "color_interpolation": "INTERPOLATION_TYPE_BILINEAR",
            "sensor_calib_path": "/home/root/apps/resources/cam_intrinsics.txt",
            "camera_type": "CAMERA_TYPE_PINHOLE",
            "camera_fov": 100.0
        },
        "dis": {
            "enabled": false,
            "minimun_coefficient_filter": 0.1,
            "decrement_coefficient_threshold": 0.001,
            "increment_coefficient_threshold": 0.01,
            "running_average_coefficient": 0.033,
            "std_multiplier": 3.0,
            "black_corners_correction_enabled": true,
            "black_corners_threshold": 0.5,
            "average_luminance_threshold": 0,
            "angular_dis": {
                "enabled": false,
                "vsm": {
                    "hoffset": 1856,
                    "voffset": 1016,
                    "width": 1920,
                    "height": 1080,
                    "max_displacement": 64
                }
            },
            "debug": {
                "generate_resize_grid": false,
                "fix_stabilization": false,
                "fix_stabilization_longitude": 0.0,
                "fix_stabilization_latitude": 0.0
            }
        },
        "gmv": {
            "source": "isp",
            "frequency": 0.0
        },
        "optical_zoom": {
            "enabled": true,
            "magnification": 1.0
        },
        "digital_zoom": {
            "enabled": true,
            "mode": "DIGITAL_ZOOM_MODE_MAGNIFICATION",
            "magnification": 1.0,
            "roi": {
                "x": 200,
                "y": 200,
                "width": 2800,
                "height": 1800
            }
        },
        "rotation": {
            "enabled": false,
            "angle": "ROTATION_ANGLE_0"
        },
        "flip": {
            "enabled": false,
            "direction": "FLIP_DIRECTION_NONE"
        },
        "hailort": {
            "device-id": "device0"
        }
    })";
    m_config = nlohmann::json::parse(m_default_config);
    m_ai_resource = ai_res;
}

void webserver::resources::FrontendResource::http_register(std::shared_ptr<httplib::Server> srv)
{
    srv->Get("/frontend", [this](const httplib::Request &, httplib::Response &res)
             { res.set_content(this->to_string(), "application/json"); });

    srv->Patch("/frontend", [this](const httplib::Request &req, httplib::Response &res)
               {
                    auto partial_config = nlohmann::json::parse(req.body);
                    m_config.merge_patch(partial_config);
                    res.set_content(this->to_string(), "application/json");
                    auto state = ConfigResourceState(this->to_string());
                    on_resource_change(std::make_shared<webserver::resources::ResourceState>(state)); });

    srv->Put("/frontend", [this](const httplib::Request &req, httplib::Response &res)
             {
                auto config = nlohmann::json::parse(req.body);
                auto partial_config = nlohmann::json::diff(m_config, config);
                m_config = m_config.patch(partial_config);
                res.set_content(this->to_string(), "application/json");
                on_resource_change(std::make_shared<webserver::resources::ResourceState>(ConfigResourceState(this->to_string()))); });
}

nlohmann::json webserver::resources::FrontendResource::get_frontend_config()
{
    nlohmann::json conf = m_config;
    conf["denoise"] = m_ai_resource->get_ai_config(AiResource::AiApplications::AI_APPLICATION_DENOISE);
    conf["defog"] = m_ai_resource->get_ai_config(AiResource::AiApplications::AI_APPLICATION_DEFOG);
    return conf;
}