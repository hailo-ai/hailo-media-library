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

#include "buffer_utils/buffer_utils.hpp"
#include <algorithm>
#include <thread>
#include <iomanip>
#include "media_library/media_library_logger.hpp"
#include "impl/custom_overlay_impl.hpp"
#include "impl/image_overlay_impl.hpp"
#include "impl/text_overlay_impl.hpp"
#include "impl/datetime_overlay_impl.hpp"
#include "osd.hpp"
#include "osd_repository.hpp"

#define MODULE_NAME LoggerType::Osd

namespace osd
{
tl::expected<std::shared_ptr<OsdBlender>, media_library_return> OsdBlender::create(const std::string &stream_id,
                                                                                   const OverlayRepositoryPtr &repo)
{
    auto blender = std::make_shared<OsdBlender>();
    blender->m_stream_id = stream_id;

    if (repo == nullptr)
    {
        auto repo_expected = OverlayRepository::create();
        if (!repo_expected.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create OSD repository ({})", repo_expected.error());
            return tl::make_unexpected(repo_expected.error());
        }
        blender->m_osd_repository = repo_expected.value();
    }
    else
    {
        blender->m_osd_repository = repo;
    }

    return blender;
}

OsdBlender::OsdBlender()
{
    m_frame_width = 0;
    m_frame_height = 0;
    m_frame_size_set = false;

    // Acquire DSP device
    dsp_status dsp_result = dsp_utils::acquire_device();
    if (dsp_result != DSP_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Accuire DSP device failed with status code {}", dsp_result);
        return;
    }
}

OsdBlender::~OsdBlender()
{
    dsp_status dsp_result = dsp_utils::release_device();
    if (dsp_result != DSP_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Release DSP device failed with status code {}", dsp_result);
    }
}

void OsdBlender::set_stream_id(const std::string &stream_id)
{
    m_stream_id = stream_id;
}

media_library_return OsdBlender::blend(HailoMediaLibraryBufferPtr &input_buffer)
{
    auto &encoded_output_streams_config = input_buffer->get_attached_profile()->encoded_output_streams;
    auto stream_it = encoded_output_streams_config.find(m_stream_id);
    if (stream_it == encoded_output_streams_config.end())
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Stream id {} not found in attached profile, skipping frame", m_stream_id);
        return MEDIA_LIBRARY_SUCCESS;
    }
    auto &osd_config = stream_it->second.osd;

    m_osd_repository->update_overlays_if_needed(osd_config);

    std::vector<OverlayImplPtr> overlays_to_blend = m_osd_repository->get_blending_overlays(osd_config);

    std::vector<dsp_overlay_properties_t> all_overlays_to_blend;
    all_overlays_to_blend.reserve(overlays_to_blend.size());
    for (const auto &overlay : overlays_to_blend)
    {
        if (!overlay->get_enabled())
        {
            continue;
        }

        auto dsp_overlays_expected = overlay->get_dsp_overlays();
        if (!dsp_overlays_expected.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get DSP compatible overlays ({})",
                                  dsp_overlays_expected.error());
            return dsp_overlays_expected.error();
        }
        auto dsp_overlays = dsp_overlays_expected.value();

        all_overlays_to_blend.insert(all_overlays_to_blend.end(), dsp_overlays.begin(), dsp_overlays.end());
    }

    // DSP blend have better color quality when the overlays have even x and y offsets
    for (auto &dsp_overlay : all_overlays_to_blend)
    {
        dsp_overlay.x_offset -= dsp_overlay.x_offset % 2;
        dsp_overlay.y_offset -= dsp_overlay.y_offset % 2;
    }

    // Perform blending for all overlays
    for (unsigned int i = 0; i < all_overlays_to_blend.size(); i += dsp_utils::max_blend_overlays)
    {
        auto first = all_overlays_to_blend.begin() + i;
        auto last = all_overlays_to_blend.end();

        if (i + dsp_utils::max_blend_overlays < all_overlays_to_blend.size())
        {
            last = first + dsp_utils::max_blend_overlays;
        }

        std::vector blend_chuck(first, last);

        dsp_status status =
            dsp_utils::perform_dsp_multiblend(input_buffer->buffer_data.get(), blend_chuck.data(), blend_chuck.size());

        if (status != DSP_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "DSP blend failed with {}", status);
            return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return OsdBlender::set_frame_size(int frame_width, int frame_height)
{
    if (frame_width < 1 || frame_height < 1)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Frame size is invalid ({}x{})", frame_width, frame_height);
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    if (m_frame_width == frame_width && m_frame_height == frame_height)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Frame size is already set to {}x{}", frame_width, frame_height);
        return MEDIA_LIBRARY_SUCCESS;
    }

    m_frame_width = frame_width;
    m_frame_height = frame_height;
    m_frame_size_set = true;

    m_osd_repository->set_frame_size(frame_width, frame_height);

    return MEDIA_LIBRARY_SUCCESS;
}

} // namespace osd
