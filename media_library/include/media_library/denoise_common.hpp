/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once

#include "media_library_types.hpp"

namespace denoise_common
{
// When bayer=true, post isp denoise is disabled
inline bool post_isp_enable_changed(const denoise_config_t &old_configs, const denoise_config_t &new_configs)
{
    bool old_bayer = old_configs.bayer;
    bool new_bayer = new_configs.bayer;
    bool old_enabled = old_configs.enabled;
    bool new_enabled = new_configs.enabled;

    return (!old_bayer && old_enabled && !new_enabled) || (!new_bayer && !old_enabled && new_enabled) ||
           (old_bayer && !new_bayer && new_enabled) || (!old_bayer && new_bayer && old_enabled);
}

// enable=true and bayer=false must be met for post isp denoise to be enabled
inline bool post_isp_enabled(const denoise_config_t &old_configs, const denoise_config_t &new_configs)
{
    bool enable_changed = post_isp_enable_changed(old_configs, new_configs);
    return enable_changed && new_configs.enabled && !new_configs.bayer;
}

// We may disable post-isp-denoise when enable=true but bayer=true
inline bool post_isp_disabled(const denoise_config_t &old_configs, const denoise_config_t &new_configs)
{
    bool enable_changed = post_isp_enable_changed(old_configs, new_configs);
    return enable_changed && (!new_configs.enabled || new_configs.bayer);
}

// When bayer=true, pre isp denoise is enabled
inline bool pre_isp_enable_changed(const denoise_config_t &old_configs, const denoise_config_t &new_configs)
{
    bool old_bayer = old_configs.bayer;
    bool new_bayer = new_configs.bayer;
    bool old_enabled = old_configs.enabled;
    bool new_enabled = new_configs.enabled;

    return (new_bayer && !old_enabled && new_enabled) || (old_bayer && old_enabled && !new_enabled) ||
           (old_bayer && !new_bayer && old_enabled) || (!old_bayer && new_bayer && new_enabled);
}

// enable=true and bayer=true must be met for pre isp denoise to be enabled
// Ex: enable stays true, but bayer changes from true to false means pre-isp should disable
inline bool pre_isp_enabled(const denoise_config_t &old_configs, const denoise_config_t &new_configs)
{
    bool enable_changed = pre_isp_enable_changed(old_configs, new_configs);
    return enable_changed && new_configs.enabled && new_configs.bayer;
}

// We may disable pre-isp-denoise when enable=true but bayer=false
inline bool pre_isp_disabled(const denoise_config_t &old_configs, const denoise_config_t &new_configs)
{
    bool enable_changed = pre_isp_enable_changed(old_configs, new_configs);
    return enable_changed && (!new_configs.enabled || !new_configs.bayer);
}

} // namespace denoise_common
