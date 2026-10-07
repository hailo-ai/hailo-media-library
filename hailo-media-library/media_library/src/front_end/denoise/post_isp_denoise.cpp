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

#include "post_isp_denoise.hpp"

#include <stdint.h>
#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include "hailort_denoise.hpp"
#include "buffer_pool.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "media_library_buffer.hpp"
#include "dsp_utils.hpp"

#define MODULE_NAME LoggerType::Denoise

static int round_up_to_multiple(int num_to_round, int multiple)
{
    if (multiple == 0)
        return num_to_round;

    int remainder = num_to_round % multiple;
    if (remainder == 0)
        return num_to_round;

    return num_to_round + multiple - remainder;
}

static constexpr uint8_t NEUTRAL_PIXEL_VALUE = 128;

// The loopback pool is aligned for the network, so it can have more rows and a wider stride than the input
// frame; the part the input does not reach gets a neutral value instead of leftover pool memory.
static void fill_uncovered_area(uint8_t *dst_ptr, uint32_t dst_stride, uint32_t dst_size, uint32_t rows,
                                uint32_t row_bytes)
{
    if (dst_stride > row_bytes)
    {
        for (uint32_t row = 0; row < rows; row++)
        {
            std::memset(dst_ptr + row * dst_stride + row_bytes, NEUTRAL_PIXEL_VALUE, dst_stride - row_bytes);
        }
    }
    const uint32_t copied_bytes = rows * dst_stride;
    if (dst_size > copied_bytes)
    {
        std::memset(dst_ptr + copied_bytes, NEUTRAL_PIXEL_VALUE, dst_size - copied_bytes);
    }
}

static void seed_loopback_plane(const TensorBinding &src, const TensorBinding &dst, const char *channel_label)
{
    if (!src.buffer || !dst.buffer)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Cannot seed {} loopback: missing buffer", channel_label);
        return;
    }
    const uint32_t src_plane = static_cast<uint32_t>(src.plane_id);
    const uint32_t dst_plane = static_cast<uint32_t>(dst.plane_id);
    auto *src_ptr = static_cast<const uint8_t *>(src.buffer->get_plane_ptr(src_plane));
    auto *dst_ptr = static_cast<uint8_t *>(dst.buffer->get_plane_ptr(dst_plane));
    const uint32_t src_stride = src.buffer->get_plane_stride(src_plane);
    const uint32_t dst_stride = dst.buffer->get_plane_stride(dst_plane);
    if (src_ptr == nullptr || dst_ptr == nullptr || src_stride == 0 || dst_stride == 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Cannot seed {} loopback: invalid plane", channel_label);
        return;
    }
    const uint32_t dst_size = dst.buffer->get_plane_size(dst_plane);
    const uint32_t rows = std::min(src.buffer->get_plane_size(src_plane) / src_stride, dst_size / dst_stride);
    const uint32_t row_bytes = std::min(src_stride, dst_stride);

    if (src.buffer->is_dmabuf())
        src.buffer->sync_start(src_plane);
    if (dst.buffer->is_dmabuf())
        dst.buffer->sync_start(dst_plane);
    for (uint32_t row = 0; row < rows; row++)
    {
        std::memcpy(dst_ptr + row * dst_stride, src_ptr + row * src_stride, row_bytes);
    }
    fill_uncovered_area(dst_ptr, dst_stride, dst_size, rows, row_bytes);
    if (dst.buffer->is_dmabuf())
        dst.buffer->sync_end(dst_plane);
    if (src.buffer->is_dmabuf())
        src.buffer->sync_end(src_plane);
    LOGGER__MODULE__INFO(MODULE_NAME, "Cold-start {} loopback seeded from input frame: rows={} row_bytes={}",
                         channel_label, rows, row_bytes);
}

MediaLibraryPostIspDenoise::MediaLibraryPostIspDenoise() : MediaLibraryDenoise()
{
    m_hailort_denoise = std::make_unique<HailortAsyncDenoisePostISP>(
        [this](NetworkInferenceBindingsPtr bindings) { inference_callback(bindings); });
}

MediaLibraryPostIspDenoise::~MediaLibraryPostIspDenoise()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Post ISP Denoise - destructor");
    deinit();
}

// overrides

bool MediaLibraryPostIspDenoise::is_enabled(const denoise_config_t &denoise_configs)
{
    return denoise_configs.enabled && !denoise_configs.bayer;
}

media_library_return MediaLibraryPostIspDenoise::create_and_initialize_buffer_pools(
    const denoise_config_t &denoise_configs, const input_video_config_t &input_video_configs)
{
    // Create output buffer pool
    LOGGER__MODULE__DEBUG(
        MODULE_NAME, "Initalizing buffer pool named {} for output resolution: width {} height {} in buffers size of {}",
        BUFFER_POOL_NAME, input_video_configs.resolution.dimensions.destination_width,
        input_video_configs.resolution.dimensions.destination_height, denoise_configs.pool_max_buffers);
    auto adjusted_width = round_up_to_multiple(input_video_configs.resolution.dimensions.destination_width,
                                               RESOULTION_MULTIPLE_REQUIRED_BY_DENOISE_NETWORK);
    auto adjusted_height = round_up_to_multiple(input_video_configs.resolution.dimensions.destination_height,
                                                RESOULTION_MULTIPLE_REQUIRED_BY_DENOISE_NETWORK);

    if (m_output_buffer_pool == nullptr)
    {
        m_output_buffer_pool = std::make_shared<MediaLibraryBufferPool>(
            adjusted_width, adjusted_height, HAILO_FORMAT_NV12, denoise_configs.pool_max_buffers,
            HAILO_MEMORY_TYPE_DMABUF, BUFFER_POOL_NAME);
    }
    if (m_output_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init buffer pool");
        return media_library_return::MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryPostIspDenoise::free_buffer_pools()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Closing Post-ISP denoise buffer pool");

    if (m_output_buffer_pool == nullptr)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Post-ISP buffer pool already closed or not initialized");
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Waiting for Post-ISP output buffer pool to release used buffers");
    if (m_output_buffer_pool->wait_for_used_buffers() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to wait for used buffers to be released");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Freeing Post-ISP output buffer pool");
    if (m_output_buffer_pool->free() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to free Post-ISP buffer pool");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    m_output_buffer_pool = nullptr;
    LOGGER__MODULE__INFO(MODULE_NAME, "Post-ISP buffer pool closed successfully");
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

bool MediaLibraryPostIspDenoise::process_inference(NetworkInferenceBindingsPtr bindings)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Processing Post-ISP denoise inference");
    return m_hailort_denoise->process(std::move(bindings));
}

media_library_return MediaLibraryPostIspDenoise::acquire_output_buffers(NetworkInferenceBindingsPtr bindings)
{
    return m_output_buffer_pool->acquire_buffer(get_output_buffer(bindings, get_denoised_output_index()));
}

void MediaLibraryPostIspDenoise::copy_meta(HailoMediaLibraryBufferPtr input_buffer,
                                           HailoMediaLibraryBufferPtr output_buffer)
{
    output_buffer->copy_metadata_from(input_buffer);
}

// The first frame after enable has no previous output to feed back, so the feedback starts from the
// frame itself (the steady-state feedback of a static scene) instead of unwritten pool memory.
void MediaLibraryPostIspDenoise::initialize_dummy_loopback_buffers(const NetworkInferenceBindings &bindings)
{
    seed_loopback_plane(bindings.inputs[HailortAsyncDenoisePostISP::InputIndex::Y_CHANNEL],
                        bindings.outputs[HailortAsyncDenoisePostISP::OutputIndex::OUTPUT_Y_CHANNEL], "Y");
    seed_loopback_plane(bindings.inputs[HailortAsyncDenoisePostISP::InputIndex::UV_CHANNEL],
                        bindings.outputs[HailortAsyncDenoisePostISP::OutputIndex::OUTPUT_UV_CHANNEL], "UV");
}
