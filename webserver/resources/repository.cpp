#include "resources.hpp"
#include "repository.hpp"

WebserverResourceRepository webserver::resources::ResourceRepository::create()
{
    std::vector<WebserverResource> resources_vec{};
    auto ai_resource = std::make_shared<webserver::resources::AiResource>();
    resources_vec.push_back(ai_resource);
    resources_vec.push_back(std::make_shared<webserver::resources::IspResource>(ai_resource));
    resources_vec.push_back(std::make_shared<webserver::resources::FrontendResource>(ai_resource));
    resources_vec.push_back(std::make_shared<webserver::resources::EncoderResource>());
    resources_vec.push_back(std::make_shared<webserver::resources::OsdResource>());
    resources_vec.push_back(std::make_shared<webserver::resources::PrivacyMaskResource>());
    resources_vec.push_back(std::make_shared<webserver::resources::WebpageResource>());

    return std::make_shared<webserver::resources::ResourceRepository>(resources_vec);
}

webserver::resources::ResourceRepository::ResourceRepository(std::vector<WebserverResource> resources)
{
    for (const auto &resource : resources)
    {
        m_resources[resource->get_type()] = resource;
    }
}

std::map<webserver::resources::ResourceBehaviorType, std::vector<webserver::resources::ResourceType>> webserver::resources::ResourceRepository::get_all_types()
{
    std::map<webserver::resources::ResourceBehaviorType, std::vector<webserver::resources::ResourceType>> m = {{webserver::resources::RESOURCE_BEHAVIOR_CONFIG, {}}, {webserver::resources::RESOURCE_BEHAVIOR_FUNCTIONAL, {}}};
    for (const auto &[res_type, res] : m_resources)
    {
        m[res->get_behavior_type()].push_back(res_type);
    }
    return m;
}
