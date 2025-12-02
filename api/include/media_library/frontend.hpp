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
/**
 * @file frontend.hpp
 * @brief MediaLibrary Frontend (Dewarp + Multi-Resize) CPP API module
 **/
#pragma once
#include "media_library/buffer_pool.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/privacy_mask.hpp"
#include <climits>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <tl/expected.hpp>

using output_stream_id_t = std::string;

struct frontend_output_stream_t
{
    output_stream_id_t id;
    uint32_t width;
    uint32_t height;
    uint32_t target_fps;
    float current_fps;
    std::string srcpad_name;
};

/*!
 * @brief type for user callback function to receive output frames
 *
 * The provided user function receives the output frames and their sizes
 * as uint32_t argument.
 * @ref MediaLibraryFrontend::subscribe
 */
typedef std::function<void(HailoMediaLibraryBufferPtr, uint32_t)> FrontendWrapperCallback;

/**
 * @brief A map that associates output stream ID with `FrontendWrapperCallback` function objects.
 *
 * The `FrontendCallbacksMap` is a typedef for `std::map<output_stream_id_t, FrontendWrapperCallback>`.
 * It is used to store and retrieve callback functions associated with output stream ID.
 */
typedef std::map<output_stream_id_t, FrontendWrapperCallback> FrontendCallbacksMap;

/** @defgroup MediaLibrary Frontend (Dewarp + Multi-Resize) CPP API
 */
class MediaLibraryFrontend;
using MediaLibraryFrontendPtr = std::shared_ptr<MediaLibraryFrontend>;

/**
 * @brief Frontend object with Dewarp and Multi Resize features.
 *
 * Each instance represents a Frontend Bin.
 * Each Frontend bin have one input stream and may have several output streams, depending on configuration.
 */
class MediaLibraryFrontend
{
  private:
    class Impl;
    std::shared_ptr<Impl> m_impl;

  public:
    /**
     * @brief Constructor for the frontend module
     *
     * @param[in] impl - shared pointer to the implementation object
     * @note This constructor is used internally by the create function.
     */
    MediaLibraryFrontend(std::shared_ptr<Impl> impl);

    /**
     * @brief Constructs an MediaLibraryFrontend object
     *
     * @return tl::expected<MediaLibraryFrontendPtr, media_library_return> -
     * An expected object that holds either a shared pointer
     *  to an MediaLibraryFrontend object, or a error code.
     */
    static tl::expected<MediaLibraryFrontendPtr, media_library_return> create();

    /**
     * @brief get the configuration of the MediaLibraryFrontend module
     * @return tl::expected<frontend_config_t, media_library_return>
     */
    tl::expected<frontend_config_t, media_library_return> get_config();

    /**
     * @brief Set the config object for the MediaLibraryFrontend module
     * @param[in] config - [frontend_config_t] configuration object, obtained from the ``get_config`` function
     * @return media_library_return
     */
    media_library_return set_config(const frontend_config_t &config);

    /**
     * @brief Start the MediaLibraryFrontend module, the MediaLibraryFrontend
     * module will be ready to receive buffers. set_config(const string&) must be called before start()
     * @return media_library_return - status of the start operation
     */

    media_library_return start();

    /**
     * @brief Stop the MediaLibraryFrontend module, the MediaLibraryFrontend
     * module will stop receiving buffers.
     * @return media_library_return - status of the stop operation
     */
    media_library_return stop();

    /**
     * @brief Configure the MediaLibraryFrontend module with the given
     * configuration.
     * @param[in] json_config - a json string containing the configuration
     * @return media_library_return - status of the configuration operation
     */
    media_library_return set_config(const std::string &json_config);

    /**
     * @brief Subscribe to the MediaLibraryFrontend module to receive the output
     * buffers
     * @param[in] callbacks - a map of callbacks functions to be called when a buffer is
     * ready. The callback function AppWrapperCallback object. The callback
     * function should be thread-safe.
     * number of callback functions should be equal to number of outputs.
     * The callback function should not:
     * * Block.
     * * Throw exceptions.
     * * Call the MediaLibraryFrontend module.
     * @return media_library_return - status of the subscription operation
     * @note if the user wishes to add additional arguments for the callback
     * execution a lambda wrapper and pass as single argument to the
     * subscribe function.
     * @ref AppWrapperCallback
     */
    media_library_return subscribe(FrontendCallbacksMap callbacks);

    /**
     * @brief Get all subscriber IDs currently registered with the MediaLibraryFrontend module
     *
     * This function retrieves a list of all subscriber IDs that are currently registered
     * to receive output buffers from the MediaLibraryFrontend module.
     *
     * @return tl::expected<std::vector<std::string>, media_library_return> -
     * An expected object that holds either a vector of subscriber ID strings,
     * or an error code indicating the reason for failure.
     */
    tl::expected<std::vector<std::string>, media_library_return> get_all_subscribers_ids();

    /**
     * @brief Unsubscribe all currently registered subscribers from the MediaLibraryFrontend module
     *
     * This function removes all active subscriptions, effectively stopping all callback
     * functions from receiving output buffers. After calling this function, no callbacks
     * will be invoked until new subscriptions are made.
     *
     * @return media_library_return - status of the unsubscribe all operation
     */
    media_library_return unsubscribe_all();

    /**
     * @brief Unsubscribe a specific subscriber from the MediaLibraryFrontend module
     *
     * This function removes the subscription for a specific subscriber ID, stopping
     * the associated callback function from receiving output buffers. Other active
     * subscriptions remain unaffected.
     *
     * @param[in] id - the subscriber ID to unsubscribe. This should match an ID
     * that was previously registered through the subscribe function.
     * @return media_library_return - status of the unsubscribe operation
     */
    media_library_return unsubscribe(const std::string &id);

    /**
     * @brief Add a buffer to the MediaLibraryFrontend module, to be processed.
     * The add_buffer function receives raw video frame and applies
     * Dewarping and MultiResize on the raw frames.
     * @param[in] ptr - a shared pointer to hailo_media_library_buffer to be processed.
     * @return media_library_return - status of the add buffer operation.
     * @note The MediaLibraryFrontend module will take ownership of the buffer.
     * @warning Can be called only when the MediaLibraryFrontend module is configured with FRONTEND_SRC_ELEMENT_APPSRC.
     */
    media_library_return add_buffer(HailoMediaLibraryBufferPtr ptr);

    /**
     * @brief Retrieves the IDs of the output streams.
     *
     * This function returns a tl::expected object containing a vector of output_stream_id_t.
     * If the operation is successful, the expected object will hold the vector of output stream IDs.
     * Otherwise, it will hold a media_library_return error code indicating the reason for failure.
     *
     * @return A tl::expected object containing either the vector of output stream IDs or an error code.
     */
    tl::expected<std::vector<frontend_output_stream_t>, media_library_return> get_outputs_streams();

    /**
     * @brief Get the current fps of the frontend outputs by the output ids
     * @return std::unordered_map<output_stream_id_t, float> - An unordered map of frontend output ids and their
     * corresponding current fps
     */
    std::unordered_map<output_stream_id_t, float> get_output_streams_current_fps();

    /**
     * @brief Destructor for the frontend module
     */
    ~MediaLibraryFrontend() = default;

    /**
     * @brief Set the freeze state of the frontend
     *
     * This function sets the freeze state of the frontend. When the frontend is frozen, the output buffer's image will
     * be constant and will not change. The first buffer received after the freeze state is set will be the buffer that
     * will be pushed.
     *
     * @param freeze - the freeze state to set
     * @return media_library_return - status of the set freeze operation
     *
     */
    media_library_return set_freeze(bool freeze);

    /**
     * @brief Wait for the pipeline to reach PLAYING state
     *
     * This function waits for the frontend pipeline to transition to the PLAYING state
     * within the specified timeout period.
     *
     * @param timeout - the maximum time to wait for the pipeline to reach PLAYING state
     * @return bool - true if the pipeline reached PLAYING state within the timeout, false otherwise
     */
    bool wait_for_pipeline_playing(std::chrono::milliseconds timeout);
};
