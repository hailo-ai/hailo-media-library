#include "hdr_manager.hpp"
#include "hdr_manager_impl.hpp"

#include "media_library_types.hpp"

#include "isp_utils.hpp"

static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;

HdrManager::HdrManager() : m_impl(std::make_unique<Impl>())
{
}

HdrManager::~HdrManager()
{
    deinit();
}

bool HdrManager::init(const frontend_config_t &frontend_config)
{
    if (!m_impl->is_resolution_supported(frontend_config.input_config.resolution))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Unsupported HDR resolution: {}x{}",
                              frontend_config.input_config.resolution.dimensions.destination_width,
                              frontend_config.input_config.resolution.dimensions.destination_height);
        return false;
    }
    if (!m_impl->is_dol_supported(frontend_config.hdr_config.dol))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Unsupported HDR DOL value: {}", frontend_config.hdr_config.dol);
        return false;
    }

    isp_utils::setup_hdr(frontend_config.input_config.resolution, frontend_config.hdr_config,
                         m_impl->get_stitch_mode());
    isp_utils::set_hdr_configuration();
    if (!m_impl->init(frontend_config))
    {
        isp_utils::setup_sdr(frontend_config.input_config.resolution);
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
    isp_utils::setup_sdr(m_input_resolution);
}
