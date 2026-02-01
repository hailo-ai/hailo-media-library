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
#include "osd.hpp"
#include "media_library/media_library_logger.hpp"

#define MODULE_NAME LoggerType::Osd

namespace osd
{
const HorizontalAlignment HorizontalAlignment::LEFT = HorizontalAlignment(0.0);
const HorizontalAlignment HorizontalAlignment::CENTER = HorizontalAlignment(0.5);
const HorizontalAlignment HorizontalAlignment::RIGHT = HorizontalAlignment(1.0);

const VerticalAlignment VerticalAlignment::TOP = VerticalAlignment(0.0);
const VerticalAlignment VerticalAlignment::CENTER = VerticalAlignment(0.5);
const VerticalAlignment VerticalAlignment::BOTTOM = VerticalAlignment(1.0);

static media_library_return check_alignment(float alignment)
{
    if (alignment < 0.0 || alignment > 1.0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Alignment value must be between 0.0 and 1.0, got: {}", alignment);
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

tl::expected<HorizontalAlignment, media_library_return> HorizontalAlignment::create(float alignment)
{
    auto result = check_alignment(alignment);
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(result);
    }
    return HorizontalAlignment(alignment);
}

tl::expected<VerticalAlignment, media_library_return> VerticalAlignment::create(float alignment)
{
    auto result = check_alignment(alignment);
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(result);
    }
    return VerticalAlignment(alignment);
}

} // namespace osd
