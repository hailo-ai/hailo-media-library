/**
 * @file gst_pipeline_utils.hpp
 * @brief Shared utility functions for the GStreamer API elements.
 **/
#pragma once

#include <optional>
#include <string>
#include <gst/gst.h>
#include <tl/expected.hpp>

#include "media_library/media_library_types.hpp"

namespace hailo::gst_api
{

/**
 * @brief Walk the GstObject parent chain and return the pipeline name.
 *
 * @param[in] element - the element whose parent pipeline name is queried.
 * @return The pipeline name, or an error code if the element has no
 *         parent pipeline or the pipeline has no name.
 */
tl::expected<std::string, media_library_return> get_parent_pipeline_name(GstElement *element);

/**
 * @brief Check whether a string looks like JSON (starts with '{' or '[').
 *
 * @param[in] s - the string to inspect.
 * @return true if the first non-whitespace character is '{' or '['.
 */
bool looks_like_json(const std::string &s);

/**
 * @brief Extract a stream identifier from a GStreamer pad name.
 *
 * @param[in] pad_name - the pad name (may be NULL).
 * @return The pad name as a std::string, or an empty string if NULL.
 */
std::string stream_id_from_pad_name(const gchar *pad_name);

/**
 * @brief Extract the input_config_t from an encoder config variant.
 *
 * Handles both @c hailo_encoder_config_t and @c jpeg_encoder_config_t.
 *
 * @param[in] enc_config - the encoder configuration variant.
 * @return The input config, or std::nullopt if the variant holds
 *         an unrecognised type.
 */
std::optional<input_config_t> get_input_config_from_encoder(const encoder_config_t &enc_config);

/**
 * @brief Format the available stream IDs from a profile for error messages.
 *
 * @param[in] profile - the configuration profile to inspect.
 * @return A bracket-enclosed, comma-separated list of stream IDs.
 */
std::string format_aviallable_streams_ids(const config_profile_t &profile);

} // namespace hailo::gst_api
