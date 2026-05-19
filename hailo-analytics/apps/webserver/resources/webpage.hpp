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
    void http_register(HTTPServer &srv) override;
    std::string name() override;
    ResourceType get_type() override;
};
} // namespace resources
} // namespace webserver
