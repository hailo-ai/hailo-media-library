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

#include "media_library/media_library_api_types.hpp"
#include "media_library.pb.h"

namespace media_library_proto_converters
{

// MediaLibraryStatus <-> media_library_return
media_library_service::MediaLibraryStatus convert_to_proto_status(media_library_return status);
media_library_return convert_from_proto_status(media_library_service::MediaLibraryStatus status);

// PipelineState <-> media_library_pipeline_state_t
media_library_service::PipelineState convert_to_proto_pipeline_state(media_library_pipeline_state_t state);
media_library_pipeline_state_t convert_from_proto_pipeline_state(media_library_service::PipelineState state);

// ThrottlingState <-> media_library_throttling_state_t
media_library_service::ThrottlingState convert_to_proto_throttling_state(media_library_throttling_state_t state);
media_library_throttling_state_t convert_from_proto_throttling_state(media_library_service::ThrottlingState state);

} // namespace media_library_proto_converters
