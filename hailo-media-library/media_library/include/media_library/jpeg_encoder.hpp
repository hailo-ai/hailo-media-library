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
#pragma once

#include <stdint.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include <tl/expected.hpp>

#include "buffer_pool.hpp"
#include "encoder_config_types.hpp"
#include "media_library_types.hpp"

class LibJpegEncodeSession;

class JpegEncoder
{
  public:
    struct EncodedFrame
    {
        std::vector<uint8_t> bitstream;
        uint64_t pts = 0;
        uint64_t dts = 0;
    };

    JpegEncoder();
    ~JpegEncoder();

    JpegEncoder(const JpegEncoder &) = delete;
    JpegEncoder &operator=(const JpegEncoder &) = delete;

    /**
     * Apply config and (re)build the internal worker pool sized to
     * `config.n_threads`. Any jobs in flight from a previous configure()
     * are dropped — the caller must drain via dequeue() before reconfiguring.
     */
    media_library_return configure(const jpeg_encoder_config_t &config);

    /**
     * Submit a frame for async encoding by the internal worker pool.
     * Blocks if the internal queue is already at capacity (`n_threads` items)
     * until a worker pops a job, then returns. Returns MEDIA_LIBRARY_UNINITIALIZED
     * only if the encoder was stopped while the caller was parked.
     * Results are delivered in submission order via dequeue().
     */
    media_library_return submit(const HailoMediaLibraryBufferPtr &input);

    /**
     * Wait for the next-in-submission-order encoded frame. Returns the
     * encoded frame on success, or a media_library_return error code if the
     * matching submit's encode failed.
     */
    tl::expected<EncodedFrame, media_library_return> dequeue();

    /**
     * Stop and join the worker pool. After this returns, submit() / dequeue() unblock
     * any parked callers with MEDIA_LIBRARY_UNINITIALIZED, and the encoder must be
     * re-configured via configure() before it can encode again. Callers that share
     * the encoder with a consumer thread (e.g. a gst completion thread parked in
     * dequeue()) call this before joining their consumer to guarantee shutdown
     * doesn't hang on the encoder side.
     */
    void stop_workers();

    const jpeg_encoder_config_t &get_config() const
    {
        return m_config;
    }

  private:
    void worker_loop();

    tl::expected<EncodedFrame, media_library_return> encode(LibJpegEncodeSession &session,
                                                            const HailoMediaLibraryBufferPtr &input);

    struct Job
    {
        HailoMediaLibraryBufferPtr input;
        tl::expected<EncodedFrame, media_library_return> result = tl::unexpected(MEDIA_LIBRARY_ERROR);
        std::atomic<bool> done{false};
    };

    jpeg_encoder_config_t m_config{};
    std::mutex m_config_mutex;

    // Worker pool, sized at configure() time.
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_stop{false};
    std::atomic<uint32_t> m_queue_capacity{1};

    // Input queue (workers consume). submit() blocks when size == m_queue_capacity.
    std::mutex m_in_mtx;
    std::condition_variable m_in_not_full;
    std::condition_variable m_in_not_empty;
    std::queue<std::shared_ptr<Job>> m_in_queue;

    // Output queue, populated by submit() in submission order. dequeue() pops
    // the front and blocks until that job's `done` flag is set by a worker.
    std::mutex m_out_mtx;
    std::condition_variable m_out_ready;
    std::queue<std::shared_ptr<Job>> m_out_queue;
};

using JpegEncoderPtr = std::shared_ptr<JpegEncoder>;
