#pragma once

// General includes
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <tl/expected.hpp>
#include <functional>
#include <iostream>
#include <queue>
#include <thread>
#include <vector>

// Media library includes
#include "gsthailobuffermeta.hpp"
#include "media_library/buffer_pool.hpp"
#include "media_library/media_library_types.hpp"

// Infra includes
#include "output_module.hpp"

// Defines
#define UDP_MODULE_SRC_QUEUE_NAME "udp_appsrc_q"
#define UDP_MODULE_SRC_NAME "udp_src"

namespace hailo_analytics::pipeline::sinks
{

class UdpModule;
using UdpModulePtr = std::shared_ptr<UdpModule>;

class UdpModule : public OutputModule
{
  private:
    std::string m_host;
    std::string m_port;

  public:
    static tl::expected<UdpModulePtr, AppStatus> create(std::string name, std::string host, std::string port,
                                                        EncodingType type, bool print_fps);
    ~UdpModule() override;
    UdpModule(std::string name, std::string host, std::string port, EncodingType type, AppStatus &status,
              bool print_fps);

  private:
    std::string create_pipeline_string();
};

} // namespace hailo_analytics::pipeline::sinks
