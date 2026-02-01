#pragma once

#include "buffer_pool.hpp"
#include "config_manager.hpp"

class ConfigAttacher
{
    const ConfigManagerInteractor *m_interactor;

  public:
    ConfigAttacher(const ConfigManagerInteractor *interactor);
    ~ConfigAttacher();

    bool attach_config(HailoMediaLibraryBufferPtr buffer);
    bool attach_fallback_config(HailoMediaLibraryBufferPtr buffer);
};
