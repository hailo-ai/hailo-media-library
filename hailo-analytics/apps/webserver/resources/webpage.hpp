#pragma once
#include "common/resources.hpp"

namespace webserver
{
namespace resources
{
class WebpageResource : public Resource
{
  public:
    WebpageResource(std::shared_ptr<EventBus> event_bus);
    void http_register(std::shared_ptr<HTTPServer> srv) override;
    std::string name() override
    {
        return "webpage";
    }
    ResourceType get_type() override
    {
        return ResourceType::RESOURCE_WEBPAGE;
    }
};
} // namespace resources
} // namespace webserver
