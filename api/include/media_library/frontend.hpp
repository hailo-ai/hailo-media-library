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
#include <nlohmann/json.hpp>
#include <tl/expected.hpp>

enum frontend_src_element_t
{
    FRONTEND_SRC_ELEMENT_V4L2SRC = 0,
    FRONTEND_SRC_ELEMENT_APPSRC,

    /** Max enum value to maintain ABI Integrity */
    FRONTEND_SRC_ELEMENT_MAX = INT_MAX
};

typedef std::string output_stream_id_t;

struct frontend_output_stream_t
{
    output_stream_id_t id;
    uint32_t width;
    uint32_t height;
    uint32_t framerate;
};

/*!
 * @brief type for user callback function to receive output frames
 *
 * the provided user function receives the output frames and their sizes
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
 * @brief Frontend object with Dewarp and MultiResize features.
 *
 * Each instance represents a Frontend Bin.
 * Each Frontend bin have 1 input stream and may have several output streams, depending on configuration.
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
     * @param[in] json_config - json configuration string
     * @return tl::expected<MediaLibraryFrontendPtr, media_library_return> -
     * An expected object that holds either a shared pointer
     *  to an MediaLibraryFrontend object, or a error code.
     */
    static tl::expected<MediaLibraryFrontendPtr, media_library_return> create(frontend_src_element_t src_element, std::string json_config);

    /**
     * @brief Start the MediaLibraryFrontend module, the MediaLibraryFrontend
     * module will be ready to receive buffers.
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
    media_library_return configure(std::string json_config);

    /**
     * @brief Subscribe to the MediaLibraryFrontend module to receive the output
     * buffers
     * @param[in] callbacks - a map of callbacks functions to be called when a buffer is
     * ready. The callback function AppWrapperCallback object. The callback
     * function should be thread safe.
     * number of callback functions should be equal to number of outputs.
     * The callback function should not be blocking.
     * The callback function should not throw exceptions.
     * The callback function should not call the MediaLibraryFrontend module.
     * @return media_library_return - status of the subscription operation
     * @note if user wishes to add additional arguments for the callback
     * execution it may use lambda wrapper and pass as single argument to the
     * subscribe function.
     * @ref AppWrapperCallback
     */
    media_library_return subscribe(FrontendCallbacksMap callbacks);

    /**
     * @brief Add a buffer to the MediaLibraryFrontend module, to be processed.
     * The add_buffer function receives raw video frame, and applies
     * Dewarping and MultiResize on the raw frames.
     * @param[in] ptr - a shared pointer to hailo_media_library_buffer to be processed.
     * @return media_library_return - status of the add buffer operation.
     * @note The MediaLibraryFrontend module will take ownership of the buffer.
     * @warning can be called only when the MediaLibraryFrontend module is configured with FRONTEND_SRC_ELEMENT_APPSRC.
     */
    media_library_return add_buffer(HailoMediaLibraryBufferPtr ptr);

    /**
     * @brief Get a privacy mask manager object
     * @return :pointer containing the privacy mask blender object
     * @ref PrivacyMaskBlenderPtr
     */
    PrivacyMaskBlenderPtr get_privacy_mask_blender();

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
     * @brief Destructor for the frontend module
     */
    ~MediaLibraryFrontend() = default;
};