/*
 * Copyright (c) 2017-2026 Hailo Technologies Ltd. All rights reserved.
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
 */

#include "media_library/jpeg_encoder.hpp"

#include <tl/expected.hpp>
#include <utility>

#include "libjpeg_encode_session.hpp"
#include "media_library/media_library_logger.hpp"
#include "media_library_buffer.hpp"

static constexpr LoggerType MODULE_NAME = LoggerType::Encoder;
static constexpr uint32_t DEFAULT_JPEG_QUALITY = 80;

JpegEncoder::JpegEncoder() = default;

JpegEncoder::~JpegEncoder()
{
    stop_workers();
}

void JpegEncoder::stop_workers()
{
    {
        std::lock_guard<std::mutex> lock(m_in_mtx);
        m_stop.store(true);
    }
    // Wake every CV that may be parked on shutdown: workers, submitters blocked on a
    // full in_queue, and dequeue() callers.
    m_in_not_empty.notify_all();
    m_in_not_full.notify_all();
    m_out_ready.notify_all();
    for (auto &t : m_workers)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
    m_workers.clear();
}

media_library_return JpegEncoder::configure(const jpeg_encoder_config_t &config)
{
    stop_workers();
    {
        std::lock_guard<std::mutex> lock(m_in_mtx);
        std::queue<std::shared_ptr<Job>> empty;
        m_in_queue.swap(empty);
    }
    {
        std::lock_guard<std::mutex> lock(m_out_mtx);
        std::queue<std::shared_ptr<Job>> empty;
        m_out_queue.swap(empty);
    }
    m_stop.store(false);

    {
        std::lock_guard<std::mutex> lock(m_config_mutex);
        m_config = config;
        if (m_config.quality == 0)
        {
            m_config.quality = DEFAULT_JPEG_QUALITY;
        }
        if (m_config.n_threads == 0)
        {
            m_config.n_threads = 1;
        }
        m_queue_capacity.store(m_config.n_threads);
    }

    for (uint32_t i = 0; i < m_config.n_threads; ++i)
    {
        m_workers.emplace_back([this]() { worker_loop(); });
    }
    return MEDIA_LIBRARY_SUCCESS;
}

void JpegEncoder::worker_loop()
{
    LibJpegEncodeSession session;
    while (true)
    {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock(m_in_mtx);
            m_in_not_empty.wait(lock, [this]() { return m_stop.load() || !m_in_queue.empty(); });
            if (m_stop.load() && m_in_queue.empty())
            {
                return;
            }
            job = std::move(m_in_queue.front());
            m_in_queue.pop();
        }
        m_in_not_full.notify_one();

        job->result = encode(session, job->input);
        job->input.reset();

        {
            std::lock_guard<std::mutex> lock(m_out_mtx);
            job->done.store(true);
        }
        m_out_ready.notify_all();
    }
}

media_library_return JpegEncoder::submit(const HailoMediaLibraryBufferPtr &input)
{
    if (!input || !input->buffer_data)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "JpegEncoder::submit received null input");
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }
    if (input->buffer_data->format != HAILO_FORMAT_NV12)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "JpegEncoder::submit only supports NV12 input");
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }
    if (m_workers.empty())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "JpegEncoder::submit called before configure()");
        return MEDIA_LIBRARY_UNINITIALIZED;
    }

    auto job = std::make_shared<Job>();
    job->input = input;

    {
        std::unique_lock<std::mutex> lock(m_in_mtx);
        m_in_not_full.wait(lock, [this]() { return m_stop.load() || m_in_queue.size() < m_queue_capacity.load(); });
        if (m_stop.load())
        {
            return MEDIA_LIBRARY_UNINITIALIZED;
        }
        // Both pushes happen while holding m_in_mtx. stop_workers() also acquires m_in_mtx
        // briefly to set m_stop, so it is serialized against us: either submit wins (both
        // pushes happen first, no orphan possible), or stop_workers wins (we see m_stop on
        // the wait predicate above and bail before pushing). Lock-ordering m_in_mtx -> m_out_mtx
        // also matches the worker side.
        {
            std::lock_guard<std::mutex> out_lock(m_out_mtx);
            m_out_queue.push(job);
        }
        m_in_queue.push(std::move(job));
    }
    m_in_not_empty.notify_one();
    return MEDIA_LIBRARY_SUCCESS;
}

tl::expected<JpegEncoder::EncodedFrame, media_library_return> JpegEncoder::dequeue()
{
    std::shared_ptr<Job> job;
    {
        std::unique_lock<std::mutex> lock(m_out_mtx);
        m_out_ready.wait(
            lock, [this]() { return m_stop.load() || (!m_out_queue.empty() && m_out_queue.front()->done.load()); });
        if (m_out_queue.empty() || !m_out_queue.front()->done.load())
        {
            return tl::unexpected(MEDIA_LIBRARY_UNINITIALIZED);
        }
        job = std::move(m_out_queue.front());
        m_out_queue.pop();
    }
    return std::move(job->result);
}

tl::expected<JpegEncoder::EncodedFrame, media_library_return> JpegEncoder::encode(
    LibJpegEncodeSession &session, const HailoMediaLibraryBufferPtr &input)
{
    if (!input || !input->buffer_data)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "JpegEncoder::encode received null input");
        return tl::unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
    }

    const auto &buffer_data = *input->buffer_data;
    if (buffer_data.format != HAILO_FORMAT_NV12)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "JpegEncoder::encode only supports NV12 input");
        return tl::unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
    }
    if (buffer_data.planes.size() < 2)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "JpegEncoder::encode requires 2 NV12 planes, got {}",
                              buffer_data.planes.size());
        return tl::unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
    }

    uint32_t quality;
    {
        std::lock_guard<std::mutex> lock(m_config_mutex);
        quality = m_config.quality;
    }

    const int width = static_cast<int>(buffer_data.width);
    const int height = static_cast<int>(buffer_data.height);
    const int y_stride = static_cast<int>(buffer_data.planes[0].bytesperline);
    const int uv_stride = static_cast<int>(buffer_data.planes[1].bytesperline);
    auto *y_src = static_cast<const uint8_t *>(input->get_plane_ptr(0));
    auto *uv_src = static_cast<const uint8_t *>(input->get_plane_ptr(1));
    if (y_src == nullptr || uv_src == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "JpegEncoder::encode failed to map input planes");
        return tl::unexpected(MEDIA_LIBRARY_ERROR);
    }

    const bool needs_dmabuf_sync = input->is_dmabuf();
    if (needs_dmabuf_sync)
    {
        input->sync_start();
    }
    auto encoded = session.encode_nv12(y_src, y_stride, uv_src, uv_stride, width, height, static_cast<int>(quality));
    if (needs_dmabuf_sync)
    {
        input->sync_end();
    }

    if (!encoded.has_value())
    {
        return tl::unexpected(encoded.error());
    }

    EncodedFrame out;
    out.bitstream = std::move(encoded.value());
    out.pts = input->pts;
    out.dts = input->dts;

    return out;
}
