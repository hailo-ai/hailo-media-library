#pragma once

#include "hdr_manager.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "sensor_types.hpp"
#include "v4l2_ctrl.hpp"

class HdrManager::Impl
{
    static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;

    static constexpr int STITCH_MODE = 1;
    bool m_initialized = false;
    output_resolution_t m_input_resolution;
    hdr_dol_t m_dol;
    std::shared_ptr<v4l2::v4l2ControlManager> m_v4l2_ctrl_manager;

  public:
    Impl(std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager);
    ~Impl();

    bool init(const frontend_config_t &frontend_config);
    bool start();
    void stop();
    void deinit();

    static inline int get_stitch_mode()
    {
        return STITCH_MODE;
    }

    inline std::shared_ptr<v4l2::v4l2ControlManager> get_v4l2_ctrl_manager()
    {
        return m_v4l2_ctrl_manager;
    }

    static bool is_dol_supported(hdr_dol_t dol);
};
