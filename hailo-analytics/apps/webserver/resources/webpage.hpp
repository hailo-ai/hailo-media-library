#pragma once
#include <memory>
#include <string>

#include "common/resources.hpp"
#include "common/httplib/httplib_utils.hpp"
#include "resources/common/event_bus.hpp"

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
