#include "hdr_manager_impl.hpp"

#include "hdr_manager.hpp"
#include "logger_macros.hpp"
#include "media_library_types.hpp"
#include "sensor_registry.hpp"

HdrManager::Impl::Impl(std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager)
    : m_v4l2_ctrl_manager(v4l2_ctrl_manager)
{
}

HdrManager::Impl::~Impl()
{
    deinit();
}

bool HdrManager::Impl::init(const frontend_config_t &frontend_config)
{
    if (m_initialized)
    {
        LOGGER__MODULE__INFO(LOGGER_TYPE, "Reinitializing HdrManager");
        deinit();
    }

    if (!is_dol_supported(frontend_config.hdr_config.dol))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Unsupported DOL for HDR ISP implementation");
        return false;
    }

    m_input_resolution = frontend_config.input_config.resolution;
    m_dol = frontend_config.hdr_config.dol;

    m_initialized = true;
    LOGGER__MODULE__INFO(LOGGER_TYPE, "HdrManager (ISP) initialized successfully");
    return true;
}

void HdrManager::Impl::deinit()
{
    stop();
    m_initialized = false;
}

bool HdrManager::Impl::start()
{
    if (!m_initialized)
    {
        return false;
    }

    LOGGER__MODULE__INFO(LOGGER_TYPE, "HdrManager (ISP) started");
    return true;
}

void HdrManager::Impl::stop()
{
    if (!m_initialized)
    {
        return;
    }

    LOGGER__MODULE__INFO(LOGGER_TYPE, "HdrManager (ISP) stopped");
}

bool HdrManager::Impl::is_dol_supported(hdr_dol_t dol)
{
    if (dol == HDR_DOL_2 || dol == HDR_DOL_3)
    {
        return true;
    }

    LOGGER__MODULE__ERROR(LOGGER_TYPE, "Unsupported HDR DOL value: {}", dol);
    return false;
}
