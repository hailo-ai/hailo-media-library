#pragma once

#include <memory>

#include "v4l2_ctrl.hpp"
#include "media_library_types.hpp"

class HdrManager
{
  public:
  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    bool m_initialized = false;
    output_resolution_t m_input_resolution;

  public:
    HdrManager(std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager);
    ~HdrManager();

    bool init(const frontend_config_t &frontend_config);
    bool start();
    void stop();
    void deinit();
};
