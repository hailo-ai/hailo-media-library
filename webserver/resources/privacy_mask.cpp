#include "resources.hpp"
#include <iostream>

webserver::resources::PrivacyMaskResource::PrivacyMaskResource() : Resource()
{
    m_default_config = R"(
    {
     "Privacy Mask 1": false,   
     "Privacy Mask 2": false,
     "Privacy Mask 3": false
    })";
    m_config = nlohmann::json::parse(m_default_config);

    polygon pol1;
    pol1.id = "Privacy Mask 1";
    pol1.vertices.push_back(vertex(2000, 1200));
    pol1.vertices.push_back(vertex(2000, 500));
    pol1.vertices.push_back(vertex(2800, 500));
    pol1.vertices.push_back(vertex(2800, 1200));
    polygon pol2;
    pol2.id = "Privacy Mask 2";
    pol2.vertices.push_back(vertex(300, 40));
    pol2.vertices.push_back(vertex(600, 40));
    pol2.vertices.push_back(vertex(750, 290));
    pol2.vertices.push_back(vertex(600, 540));
    pol2.vertices.push_back(vertex(300, 540));
    pol2.vertices.push_back(vertex(150, 290));
    polygon pol3;
    pol3.id = "Privacy Mask 3";
    pol3.vertices.push_back(vertex(1000, 200));
    pol3.vertices.push_back(vertex(1400, 1200));
    pol3.vertices.push_back(vertex(600, 1200));

    m_privacy_masks = {
        {pol1.id, pol1},
        {pol2.id, pol2},
        {pol3.id, pol3}};
}

std::shared_ptr<webserver::resources::PrivacyMaskResource::PrivacyMaskResourceState> webserver::resources::PrivacyMaskResource::parse_state(std::vector<std::string> current_enabled, std::vector<std::string> prev_enabled)
{
    auto state = std::make_shared<webserver::resources::PrivacyMaskResource::PrivacyMaskResourceState>();
    for (auto &mask : m_privacy_masks)
    {
        if (std::find(current_enabled.begin(), current_enabled.end(), mask.first) != current_enabled.end() &&
            std::find(prev_enabled.begin(), prev_enabled.end(), mask.first) == prev_enabled.end())
        {
            state->enabled.push_back(mask.first);
        }
        if (std::find(current_enabled.begin(), current_enabled.end(), mask.first) == current_enabled.end() &&
            std::find(prev_enabled.begin(), prev_enabled.end(), mask.first) != prev_enabled.end())
        {
            state->disabled.push_back(mask.first);
        }
    }
    return state;
}

std::vector<std::string> webserver::resources::PrivacyMaskResource::get_enabled_masks()
{
    std::vector<std::string> enabled;
    for (const auto &mask : m_privacy_masks)
    {
        if (m_config[mask.first])
        {
            enabled.push_back(mask.first);
        }
    }
    return enabled;
}

void webserver::resources::PrivacyMaskResource::http_register(std::shared_ptr<httplib::Server> srv)
{
    srv->Get("/privacy_mask", [this](const httplib::Request &, httplib::Response &res)
             { res.set_content(this->to_string(), "application/json"); });

    srv->Patch("/privacy_mask", [this](const httplib::Request &req, httplib::Response &res)
               {
                    auto prev_enabled = this->get_enabled_masks();
                    auto partial_config = nlohmann::json::parse(req.body);
                    m_config.merge_patch(partial_config);
                    res.set_content(this->to_string(), "application/json");
                    on_resource_change(this->parse_state(this->get_enabled_masks(), prev_enabled)); });

    srv->Put("/privacy_mask", [this](const httplib::Request &req, httplib::Response &res)
             {
                auto prev_enabled = this->get_enabled_masks();
                auto config = nlohmann::json::parse(req.body);
                auto partial_config = nlohmann::json::diff(m_config, config);
                m_config = m_config.patch(partial_config);
                res.set_content(this->to_string(), "application/json");
                on_resource_change(this->parse_state(this->get_enabled_masks(), prev_enabled)); });
}