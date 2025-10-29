#include "hdr_manager.hpp"
#include "hdr_manager_impl.hpp"

#include "media_library_types.hpp"
#include "isp_utils.hpp"

static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;

HdrManager::HdrManager(std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager)
    : m_impl(std::make_unique<Impl>(v4l2_ctrl_manager))
{
}

HdrManager::~HdrManager()
{
    deinit();
}

bool HdrManager::init(const frontend_config_t &frontend_config)
{
    if (!m_impl->is_dol_supported(frontend_config.hdr_config.dol))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Unsupported HDR DOL value: {}", frontend_config.hdr_config.dol);
        return false;
    }

    if (MEDIA_LIBRARY_SUCCESS != isp_utils::setup_hdr(frontend_config.input_config.resolution,
                                                      frontend_config.hdr_config, m_impl->get_stitch_mode(),
                                                      m_impl->get_v4l2_ctrl_manager()))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to setup HDR configuration");
        return false;
    }
    if (!m_impl->init(frontend_config))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to initialize HDR manager, setting SDR instead");
        if (MEDIA_LIBRARY_SUCCESS !=
            isp_utils::setup_sdr(frontend_config.input_config.resolution, m_impl->get_v4l2_ctrl_manager()))
        {
            LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to setup SDR configuration");
        }
        return false;
    }

    m_input_resolution = frontend_config.input_config.resolution;
    m_initialized = true;
    return true;
}

bool HdrManager::start()
{
    return m_impl->start();
}

void HdrManager::stop()
{
    m_impl->stop();
}

void HdrManager::deinit()
{
    if (!m_initialized)
    {
        return;
    }
    m_impl->deinit();
    if (MEDIA_LIBRARY_SUCCESS != isp_utils::setup_sdr(m_input_resolution, m_impl->get_v4l2_ctrl_manager()))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to setup SDR configuration, after deinitializing HDR");
    }
}
