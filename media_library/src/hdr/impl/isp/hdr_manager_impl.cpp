#include "hdr_manager_impl.hpp"

#include "hdr_manager.hpp"
#include "logger_macros.hpp"
#include "media_library_types.hpp"

HdrManager::Impl::Impl() = default;

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

    if (!is_resolution_supported(frontend_config.input_config.resolution))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Unsupported resolution for HDR ISP implementation");
        return false;
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

bool HdrManager::Impl::is_resolution_supported(const output_resolution_t &resolution)
{
    if ((resolution.dimensions.destination_width == SUPPORTED_WIDTH_5MP &&
         resolution.dimensions.destination_height == SUPPORTED_HEIGHT_5MP))
    {
        return true;
    }

    LOGGER__MODULE__ERROR(LOGGER_TYPE, "Unsupported HDR resolution: {}x{}", resolution.dimensions.destination_width,
                          resolution.dimensions.destination_height);
    return false;
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
