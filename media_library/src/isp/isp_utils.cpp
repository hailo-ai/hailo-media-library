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
#include "isp_utils.hpp"
#include "media_library_logger.hpp"

/** @defgroup isp_utils_definitions MediaLibrary ISP utilities CPP API
 * definitions
 *  @{
 */

namespace isp_utils
{
    void override_file(const std::string &src, const std::string &dst)
    {
        LOGGER__DEBUG("ISP config overriding file {} to {}", src, dst);
        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    }

    void set_default_configuration()
    {
        override_file(ISP_DEFAULT_3A_CONFIG, TRIPLE_A_CONFIG_PATH);
    }

    void set_denoise_configuration()
    {
        override_file(ISP_DENOISE_3A_CONFIG, TRIPLE_A_CONFIG_PATH);
    }

    void set_backlight_configuration()
    {
        override_file(ISP_BACKLIGHT_3A_CONFIG, TRIPLE_A_CONFIG_PATH);
    }

} // namespace isp_utils

/** @} */ // end of isp_utils_definitions