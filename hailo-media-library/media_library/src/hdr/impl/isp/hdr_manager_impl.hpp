#pragma once

#include "hdr_manager.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"

class HdrManager::Impl
{
    static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;

    static constexpr int STITCH_MODE = 1;

  public:
    Impl(IspManager &isp_manager);
    ~Impl();

    void deinit();

    static StitchMode get_stitch_mode();
};
