#include "resources.hpp"
#include <iostream>

webserver::resources::FrontendResource::FrontendResource(std::shared_ptr<webserver::resources::AiResource> ai_res, std::shared_ptr<webserver::resources::IspResource> isp_res, std::shared_ptr<webserver::resources::ConfigResource> configs) : Resource()
{
    m_config = configs->get_frontend_default_config();
    m_ai_resource = ai_res;
    m_isp_resource = isp_res;
}

void webserver::resources::FrontendResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->Get("/frontend", std::function<nlohmann::json()>([this]()
                                                          { return this->get_frontend_config(); }));

    srv->Patch("/frontend", [this](const nlohmann::json &partial_config)
               {
        m_config.merge_patch(partial_config);
        auto result = this->m_config;
        auto state = ConfigResourceState(this->to_string());
        on_resource_change(std::make_shared<webserver::resources::ResourceState>(state));
        return result; });

    srv->Put("/frontend", [this](const nlohmann::json &config)
             {
        auto partial_config = nlohmann::json::diff(m_config, config);
        m_config = m_config.patch(partial_config);
        auto result = this->m_config;
        on_resource_change(std::make_shared<webserver::resources::ResourceState>(ConfigResourceState(this->to_string())));
        return result; });
}

nlohmann::json webserver::resources::FrontendResource::get_frontend_config()
{
    nlohmann::json conf = m_config;
    conf["denoise"] = m_ai_resource->get_ai_config(AiResource::AiApplications::AI_APPLICATION_DENOISE);
    conf["defog"] = m_ai_resource->get_ai_config(AiResource::AiApplications::AI_APPLICATION_DEFOG);
    conf["hdr"] = m_isp_resource->get_hdr_config();
    return conf;
}