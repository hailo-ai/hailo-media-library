#pragma once

#include "hdr_manager.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"

class HdrManager::Impl
{
    static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;

    static constexpr int SUPPORTED_WIDTH_5MP = 2592;
    static constexpr int SUPPORTED_HEIGHT_5MP = 1944;
    static constexpr int STITCH_MODE = 1;

    bool m_initialized = false;
    output_resolution_t m_input_resolution;
    hdr_dol_t m_dol;

  public:
    Impl();
    ~Impl();

    bool init(const frontend_config_t &frontend_config);
    bool start();
    void stop();
    void deinit();

    static inline int get_stitch_mode()
    {
        return STITCH_MODE;
    }
    static bool is_resolution_supported(const output_resolution_t &resolution);
    static bool is_dol_supported(hdr_dol_t dol);
};
