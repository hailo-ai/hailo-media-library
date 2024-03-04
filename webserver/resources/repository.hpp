#pragma once
#include "resources.hpp"
#include <map>

namespace webserver
{
    namespace resources
    {
        class ResourceRepository
        {
        private:
            std::map<ResourceType, WebserverResource> m_resources;

        public:
            static std::shared_ptr<webserver::resources::ResourceRepository> create();
            ResourceRepository(std::vector<WebserverResource> resources);
            std::map<ResourceBehaviorType, std::vector<ResourceType>> get_all_types();

            std::shared_ptr<Resource> get(ResourceType t)
            {
                if (m_resources.find(t) == m_resources.end())
                    return nullptr;
                return m_resources[t];
            }
        };
    }
}

typedef std::shared_ptr<webserver::resources::ResourceRepository> WebserverResourceRepository;