#include "resources.hpp"
#include <iostream>

webserver::resources::AiResource::AiResource() : Resource()
{
    m_type = ResourceType::RESOURCE_AI;
    m_default_config = R"(
    {
        "detection": {
            "enabled": false
        },
        "denoise": {
            "enabled": false
        },
        "defog": {
            "enabled": false
        }
    })";
    m_config = nlohmann::json::parse(m_default_config);
}

std::vector<webserver::resources::AiResource::AiApplications> webserver::resources::AiResource::get_enabled_applications()
{
    std::vector<webserver::resources::AiResource::AiApplications> enabled_applications;
    for (const auto &[key, value] : m_config.items())
    {
        if (value["enabled"])
        {
            if (key == "detection")
                enabled_applications.push_back(AiApplications::AI_APPLICATION_DETECTION);
            else if (key == "denoise")
                enabled_applications.push_back(AiApplications::AI_APPLICATION_DENOISE);
            else if (key == "defog")
                enabled_applications.push_back(AiApplications::AI_APPLICATION_DEFOG);
        }
    }
    return enabled_applications;
}

std::shared_ptr<webserver::resources::AiResource::AiResourceState> webserver::resources::AiResource::parse_state(std::vector<webserver::resources::AiResource::AiApplications> current_enabled, std::vector<webserver::resources::AiResource::AiApplications> prev_enabled)
{
    auto state = std::make_shared<AiResourceState>();
    for (const auto &app : current_enabled)
    {
        if (std::find(prev_enabled.begin(), prev_enabled.end(), app) == prev_enabled.end())
        {
            state->enabled.push_back(app);
        }
    }
    for (const auto &app : prev_enabled)
    {
        if (std::find(current_enabled.begin(), current_enabled.end(), app) == current_enabled.end())
        {
            state->disabled.push_back(app);
        }
    }
    return state;
}

void webserver::resources::AiResource::http_register(std::shared_ptr<httplib::Server> srv)
{
    srv->Get("/ai", [this](const httplib::Request &, httplib::Response &res)
             { res.set_content(this->to_string(), "application/json"); });

    srv->Patch("/ai", [this](const httplib::Request &req, httplib::Response &res)
               {    
                auto prev_enabled_apps = this->get_enabled_applications();
                auto partial_config = nlohmann::json::parse(req.body);
                m_config.merge_patch(partial_config);
                res.set_content(m_config.dump(), "application/json"); 
                auto enabled_apps = this->get_enabled_applications();
                on_resource_change(this->parse_state(enabled_apps, prev_enabled_apps)); });

    srv->Put("/ai", [this](const httplib::Request &req, httplib::Response &res)
             {
                auto prev_enabled_apps = this->get_enabled_applications();
                auto config = nlohmann::json::parse(req.body);
                auto partial_config = nlohmann::json::diff(m_config, config);
                m_config = m_config.patch(partial_config);
                res.set_content(m_config.dump(), "application/json"); 
                auto enabled_apps = this->get_enabled_applications();
                on_resource_change(this->parse_state(enabled_apps, prev_enabled_apps)); });
}