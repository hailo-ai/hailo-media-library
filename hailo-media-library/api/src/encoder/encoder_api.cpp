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
#include "media_library/encoder.hpp"
#include "encoder_api_internal.hpp"

tl::expected<MediaLibraryEncoderPtr, media_library_return> MediaLibraryEncoder::create(
    const output_stream_id_t &stream_id)
{
    auto impl_expected = Impl::create(stream_id);
    if (impl_expected.has_value())
    {
        return std::make_shared<MediaLibraryEncoder>(impl_expected.value());
    }
    else
    {
        return tl::make_unexpected(impl_expected.error());
    }
}

MediaLibraryEncoder::MediaLibraryEncoder(std::shared_ptr<MediaLibraryEncoder::Impl> impl) : m_impl(impl)
{
}

media_library_return MediaLibraryEncoder::subscribe(AppWrapperCallback callback)
{
    return m_impl->subscribe(callback);
}

media_library_return MediaLibraryEncoder::unsubscribe()
{
    return m_impl->unsubscribe();
}

media_library_return MediaLibraryEncoder::start()
{
    return m_impl->start();
}

media_library_return MediaLibraryEncoder::stop()
{
    return m_impl->stop();
}

media_library_return MediaLibraryEncoder::add_buffer(HailoMediaLibraryBufferPtr ptr)
{
    return m_impl->add_buffer(ptr);
}

std::shared_ptr<osd::Blender> MediaLibraryEncoder::get_osd_blender()
{
    return m_impl->get_osd_blender();
}

std::shared_ptr<PrivacyMaskBlender> MediaLibraryEncoder::get_privacy_mask_blender()
{
    return m_impl->get_privacy_mask_blender();
}

media_library_return MediaLibraryEncoder::set_config(encoder_config_t &config)
{
    return m_impl->set_config(config);
}

media_library_return MediaLibraryEncoder::set_config(const std::string &config)
{
    return m_impl->set_config(config);
}

void MediaLibraryEncoder::set_config_manager_interactor(const ConfigManagerInteractor &config_manager_interactor)
{
    m_impl->set_config_manager_interactor(config_manager_interactor);
}

void MediaLibraryEncoder::add_config_attacher(bool enable)
{
    m_impl->add_config_attacher(enable);
}

media_library_return MediaLibraryEncoder::force_keyframe()
{
    return m_impl->force_keyframe();
}

encoder_config_t MediaLibraryEncoder::get_config()
{
    return m_impl->get_config();
}

encoder_config_t MediaLibraryEncoder::get_user_config()
{
    return m_impl->get_user_config();
}

EncoderType MediaLibraryEncoder::get_type()
{
    return m_impl->get_type();
}

float MediaLibraryEncoder::get_current_fps()
{
    return m_impl->get_current_fps();
}

encoder_monitors MediaLibraryEncoder::get_encoder_monitors()
{
    return m_impl->get_encoder_monitors();
}

void MediaLibraryEncoder::set_sensor_index(size_t sensor_index)
{
    m_impl->set_sensor_index(sensor_index);
}
