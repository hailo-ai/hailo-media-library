#include "resources.hpp"
#include "repository.hpp"

WebserverResourceRepository webserver::resources::ResourceRepository::create(std::vector<webserver::resources::ResourceType> resources)
{
    if (resources.empty())
        resources = {webserver::resources::RESOURCE_FRONTEND,
                     webserver::resources::RESOURCE_OSD,
                     webserver::resources::RESOURCE_ENCODER,
                     webserver::resources::RESOURCE_AI,
                     webserver::resources::RESOURCE_ISP,
                     webserver::resources::RESOURCE_PRIVACY_MASK};

    std::vector<WebserverResource> resources_vec{};
    for (const auto &resource : resources)
    {
        switch (resource)
        {
        case webserver::resources::RESOURCE_FRONTEND:
            resources_vec.push_back(std::make_shared<webserver::resources::FrontendResource>());
            break;
        case webserver::resources::RESOURCE_ENCODER:
            resources_vec.push_back(std::make_shared<webserver::resources::EncoderResource>());
            break;
        case webserver::resources::RESOURCE_OSD:
            resources_vec.push_back(std::make_shared<webserver::resources::OsdResource>());
            break;
        case webserver::resources::RESOURCE_AI:
            resources_vec.push_back(std::make_shared<webserver::resources::AiResource>());
            break;
        // case webserver::resources::RESOURCE_ISP:
        //     resources_vec.push_back(std::make_shared<webserver::resources::IspResource>());
        //     break;
        // case webserver::resources::RESOURCE_PRIVACY_MASK:
        //     resources_vec.push_back(std::make_shared<webserver::resources::PrivacyMaskResource>());
        //     break;
        default:
            break;
        }
    }

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
