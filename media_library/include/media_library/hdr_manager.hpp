#pragma once

#include <memory>

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
    HdrManager();
    ~HdrManager();

    bool init(const frontend_config_t &frontend_config);
    bool start();
    void stop();
    void deinit();
};
