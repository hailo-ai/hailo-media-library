#include "hdr_manager.hpp"
#include "hdr_manager_impl.hpp"

#include "media_library_types.hpp"

HdrManager::HdrManager(IspManager &isp_manager) : m_impl(std::make_unique<Impl>(isp_manager))
{
}

HdrManager::~HdrManager()
{
    deinit();
}

void HdrManager::deinit()
{
    m_impl->deinit();
}

StitchMode HdrManager::get_stitch_mode()
{
    return HdrManager::Impl::get_stitch_mode();
}
