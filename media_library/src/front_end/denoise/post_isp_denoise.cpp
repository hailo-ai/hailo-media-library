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
#include "hailort_denoise.hpp"
#include "denoise_common.hpp"
#include "buffer_pool.hpp"
#include "config_parser.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "media_library_utils.hpp"
#include "isp_utils.hpp"
#include "v4l2_ctrl.hpp"
#include "imaging/hailo_video_device.hpp"

#include <linux/v4l2-controls.h>
#include <linux/v4l2-subdev.h>
#include <stdint.h>
#include <string>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <tl/expected.hpp>
#include <vector>
#include <shared_mutex>
#include <chrono>
#include <ctime>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>

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

MediaLibraryPostIspDenoise::MediaLibraryPostIspDenoise() : MediaLibraryDenoise()
{
    m_hailort_denoise = std::make_unique<HailortAsyncDenoisePostISP>(
        [this](NetworkInferenceBindingsPtr bindings) { inference_callback(bindings); });
}

MediaLibraryPostIspDenoise::~MediaLibraryPostIspDenoise()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Post ISP Denoise - destructor");
}

// overrides

bool MediaLibraryPostIspDenoise::currently_enabled()
{
    return m_denoise_configs.enabled && !m_denoise_configs.bayer;
}

bool MediaLibraryPostIspDenoise::enabled(const denoise_config_t &denoise_configs)
{
    return denoise_common::post_isp_enabled(m_denoise_configs, denoise_configs);
}

bool MediaLibraryPostIspDenoise::disabled(const denoise_config_t &denoise_configs)
{
    return denoise_common::post_isp_disabled(m_denoise_configs, denoise_configs);
}

bool MediaLibraryPostIspDenoise::enable_changed(const denoise_config_t &denoise_configs)
{
    return denoise_common::post_isp_enable_changed(m_denoise_configs, denoise_configs);
}

bool MediaLibraryPostIspDenoise::network_changed(const denoise_config_t &denoise_configs,
                                                 const hailort_t &hailort_configs)
{
    return !denoise_configs.bayer && ((denoise_configs.network_config != m_denoise_configs.network_config) ||
                                      (hailort_configs.device_id != m_hailort_configs.device_id));
}

media_library_return MediaLibraryPostIspDenoise::create_and_initialize_buffer_pools(
    const input_video_config_t &input_video_configs)
{
    // Create output buffer pool
    LOGGER__MODULE__DEBUG(
        MODULE_NAME, "Initalizing buffer pool named {} for output resolution: width {} height {} in buffers size of {}",
        BUFFER_POOL_NAME, input_video_configs.resolution.dimensions.destination_width,
        input_video_configs.resolution.dimensions.destination_height, BUFFER_POOL_MAX_BUFFERS);
    auto adjusted_width = round_up_to_multiple(input_video_configs.resolution.dimensions.destination_width,
                                               RESOULTION_MULTIPLE_REQUIRED_BY_DENOISE_NETWORK);
    auto adjusted_height = round_up_to_multiple(input_video_configs.resolution.dimensions.destination_height,
                                                RESOULTION_MULTIPLE_REQUIRED_BY_DENOISE_NETWORK);

    if (m_output_buffer_pool == nullptr)
    {
        m_output_buffer_pool = std::make_shared<MediaLibraryBufferPool>(adjusted_width, adjusted_height,
                                                                        HAILO_FORMAT_NV12, BUFFER_POOL_MAX_BUFFERS,
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
    if (m_output_buffer_pool->wait_for_used_buffers() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to wait for used buffers to be released");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    if (m_output_buffer_pool->free() != MEDIA_LIBRARY_SUCCESS)
    {
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

bool MediaLibraryPostIspDenoise::process_inference(NetworkInferenceBindingsPtr bindings)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Processing Post-ISP denoise inference");
    return m_hailort_denoise->process(std::move(bindings));
}

media_library_return MediaLibraryPostIspDenoise::acquire_output_buffer(NetworkInferenceBindingsPtr bindings)
{
    return m_output_buffer_pool->acquire_buffer(get_output_buffer(bindings, get_denoised_output_index()));
}

void MediaLibraryPostIspDenoise::copy_meta(HailoMediaLibraryBufferPtr input_buffer,
                                           HailoMediaLibraryBufferPtr output_buffer)
{
    output_buffer->copy_metadata_from(input_buffer);
}

media_library_return MediaLibraryPostIspDenoise::acquire_input_buffer(NetworkInferenceBindingsPtr /*bindings*/)
{
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}
