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

#include "proto_converters.hpp"
#include "media_library/media_library_logger.hpp"

namespace media_library_proto_converters
{

media_library_service::MediaLibraryStatus convert_to_proto_status(media_library_return status)
{
    switch (status)
    {
    case media_library_return::MEDIA_LIBRARY_SUCCESS:
        return media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_SUCCESS;
    case media_library_return::MEDIA_LIBRARY_ERROR:
        return media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_ERROR;
    case media_library_return::MEDIA_LIBRARY_INVALID_ARGUMENT:
        return media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_INVALID_ARGUMENT;
    case media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR:
        return media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_CONFIGURATION_ERROR;
    case media_library_return::MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR:
        return media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    case media_library_return::MEDIA_LIBRARY_DSP_OPERATION_ERROR:
        return media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_DSP_OPERATION_ERROR;
    case media_library_return::MEDIA_LIBRARY_UNINITIALIZED:
        return media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_UNINITIALIZED;
    case media_library_return::MEDIA_LIBRARY_OUT_OF_RESOURCES:
        return media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_OUT_OF_RESOURCES;
    case media_library_return::MEDIA_LIBRARY_ENCODER_ENCODE_ERROR:
        return media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_ENCODER_ENCODE_ERROR;
    case media_library_return::MEDIA_LIBRARY_ENCODER_COULD_NOT_GET_PHYSICAL_ADDRESS:
        return media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_ENCODER_COULD_NOT_GET_PHYSICAL_ADDRESS;
    case media_library_return::MEDIA_LIBRARY_BUFFER_NOT_FOUND:
        return media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_BUFFER_NOT_FOUND;
    case media_library_return::MEDIA_LIBRARY_FREETYPE_ERROR:
        return media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_FREETYPE_ERROR;
    case media_library_return::MEDIA_LIBRARY_PROFILE_IS_RESTRICTED:
        return media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_PROFILE_IS_RESTRICTED;
    case media_library_return::MEDIA_LIBRARY_PROFILE_VALIDATION_FAILED:
        return media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_PROFILE_VALIDATION_FAILED;
    default:
        LOGGER__ERROR("convert_to_proto_status: unknown media_library_return value={}", static_cast<int>(status));
        return media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_ERROR;
    }
}

media_library_return convert_from_proto_status(media_library_service::MediaLibraryStatus status)
{
    switch (status)
    {
    case media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_SUCCESS:
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    case media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_ERROR:
        return media_library_return::MEDIA_LIBRARY_ERROR;
    case media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_INVALID_ARGUMENT:
        return media_library_return::MEDIA_LIBRARY_INVALID_ARGUMENT;
    case media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_CONFIGURATION_ERROR:
        return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
    case media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR:
        return media_library_return::MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    case media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_DSP_OPERATION_ERROR:
        return media_library_return::MEDIA_LIBRARY_DSP_OPERATION_ERROR;
    case media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_UNINITIALIZED:
        return media_library_return::MEDIA_LIBRARY_UNINITIALIZED;
    case media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_OUT_OF_RESOURCES:
        return media_library_return::MEDIA_LIBRARY_OUT_OF_RESOURCES;
    case media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_ENCODER_ENCODE_ERROR:
        return media_library_return::MEDIA_LIBRARY_ENCODER_ENCODE_ERROR;
    case media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_ENCODER_COULD_NOT_GET_PHYSICAL_ADDRESS:
        return media_library_return::MEDIA_LIBRARY_ENCODER_COULD_NOT_GET_PHYSICAL_ADDRESS;
    case media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_BUFFER_NOT_FOUND:
        return media_library_return::MEDIA_LIBRARY_BUFFER_NOT_FOUND;
    case media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_FREETYPE_ERROR:
        return media_library_return::MEDIA_LIBRARY_FREETYPE_ERROR;
    case media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_PROFILE_IS_RESTRICTED:
        return media_library_return::MEDIA_LIBRARY_PROFILE_IS_RESTRICTED;
    case media_library_service::MediaLibraryStatus::PROTO_MEDIA_LIBRARY_PROFILE_VALIDATION_FAILED:
        return media_library_return::MEDIA_LIBRARY_PROFILE_VALIDATION_FAILED;
    default:
        LOGGER__ERROR("convert_from_proto_status: unknown MediaLibraryStatus value={}", static_cast<int>(status));
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
}

media_library_service::PipelineState convert_to_proto_pipeline_state(media_library_pipeline_state_t state)
{
    switch (state)
    {
    case media_library_pipeline_state_t::PIPELINE_STATE_UNINITIALIZED:
        return media_library_service::PipelineState::PIPELINE_STATE_UNINITIALIZED;
    case media_library_pipeline_state_t::PIPELINE_STATE_RUNNING:
        return media_library_service::PipelineState::PIPELINE_STATE_RUNNING;
    case media_library_pipeline_state_t::PIPELINE_STATE_STOPPED:
        return media_library_service::PipelineState::PIPELINE_STATE_STOPPED;
    default:
        LOGGER__ERROR("convert_to_proto_pipeline_state: unknown pipeline_state value={}", static_cast<int>(state));
        return media_library_service::PipelineState::PIPELINE_STATE_UNINITIALIZED;
    }
}

media_library_pipeline_state_t convert_from_proto_pipeline_state(media_library_service::PipelineState state)
{
    switch (state)
    {
    case media_library_service::PipelineState::PIPELINE_STATE_RUNNING:
        return media_library_pipeline_state_t::PIPELINE_STATE_RUNNING;
    case media_library_service::PipelineState::PIPELINE_STATE_STOPPED:
        return media_library_pipeline_state_t::PIPELINE_STATE_STOPPED;
    case media_library_service::PipelineState::PIPELINE_STATE_UNINITIALIZED:
        return media_library_pipeline_state_t::PIPELINE_STATE_UNINITIALIZED;
    default:
        LOGGER__ERROR("convert_from_proto_pipeline_state: unknown PipelineState value={}", static_cast<int>(state));
        return media_library_pipeline_state_t::PIPELINE_STATE_UNINITIALIZED;
    }
}

media_library_service::ThrottlingState convert_to_proto_throttling_state(media_library_throttling_state_t state)
{
    switch (state)
    {
    case media_library_throttling_state_t::THROTTLING_STATE_UNINITIALIZED:
        return media_library_service::ThrottlingState::THROTTLING_STATE_UNINITIALIZED;
    case media_library_throttling_state_t::THROTTLING_STATE_FULL_PERFORMANCE:
        return media_library_service::ThrottlingState::THROTTLING_STATE_FULL_PERFORMANCE;
    case media_library_throttling_state_t::THROTTLING_STATE_COOLING:
        return media_library_service::ThrottlingState::THROTTLING_STATE_COOLING;
    case media_library_throttling_state_t::THROTTLING_STATE_S0:
        return media_library_service::ThrottlingState::THROTTLING_STATE_S0;
    case media_library_throttling_state_t::THROTTLING_STATE_S1:
        return media_library_service::ThrottlingState::THROTTLING_STATE_S1;
    case media_library_throttling_state_t::THROTTLING_STATE_S2:
        return media_library_service::ThrottlingState::THROTTLING_STATE_S2;
    case media_library_throttling_state_t::THROTTLING_STATE_S3:
        return media_library_service::ThrottlingState::THROTTLING_STATE_S3;
    case media_library_throttling_state_t::THROTTLING_STATE_S4:
        return media_library_service::ThrottlingState::THROTTLING_STATE_S4;
    default:
        LOGGER__ERROR("convert_to_proto_throttling_state: unknown throttling_state value={}", static_cast<int>(state));
        return media_library_service::ThrottlingState::THROTTLING_STATE_UNINITIALIZED;
    }
}

media_library_throttling_state_t convert_from_proto_throttling_state(media_library_service::ThrottlingState state)
{
    switch (state)
    {
    case media_library_service::ThrottlingState::THROTTLING_STATE_FULL_PERFORMANCE:
        return media_library_throttling_state_t::THROTTLING_STATE_FULL_PERFORMANCE;
    case media_library_service::ThrottlingState::THROTTLING_STATE_COOLING:
        return media_library_throttling_state_t::THROTTLING_STATE_COOLING;
    case media_library_service::ThrottlingState::THROTTLING_STATE_S0:
        return media_library_throttling_state_t::THROTTLING_STATE_S0;
    case media_library_service::ThrottlingState::THROTTLING_STATE_S1:
        return media_library_throttling_state_t::THROTTLING_STATE_S1;
    case media_library_service::ThrottlingState::THROTTLING_STATE_S2:
        return media_library_throttling_state_t::THROTTLING_STATE_S2;
    case media_library_service::ThrottlingState::THROTTLING_STATE_S3:
        return media_library_throttling_state_t::THROTTLING_STATE_S3;
    case media_library_service::ThrottlingState::THROTTLING_STATE_S4:
        return media_library_throttling_state_t::THROTTLING_STATE_S4;
    case media_library_service::ThrottlingState::THROTTLING_STATE_UNINITIALIZED:
        return media_library_throttling_state_t::THROTTLING_STATE_UNINITIALIZED;
    default:
        LOGGER__ERROR("convert_from_proto_throttling_state: unknown ThrottlingState value={}", static_cast<int>(state));
        return media_library_throttling_state_t::THROTTLING_STATE_UNINITIALIZED;
    }
}

} // namespace media_library_proto_converters
