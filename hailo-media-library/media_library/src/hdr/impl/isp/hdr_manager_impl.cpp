#include "hdr_manager_impl.hpp"

#include "hdr_manager.hpp"

HdrManager::Impl::Impl(IspManager &)
{
}

HdrManager::Impl::~Impl()
{
    deinit();
}

void HdrManager::Impl::deinit()
{
}

StitchMode HdrManager::Impl::get_stitch_mode()
{
    return StitchMode::ISP;
}
