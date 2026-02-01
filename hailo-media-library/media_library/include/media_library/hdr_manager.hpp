#pragma once

#include <memory>

#include "isp_manager.hpp"

enum class StitchMode
{
    ISP = 1,
    NNCORE = 2
};

class HdrManager
{
  public:
  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

  public:
    HdrManager(IspManager &isp_manager);
    ~HdrManager();

    void deinit();
    static StitchMode get_stitch_mode();
};
