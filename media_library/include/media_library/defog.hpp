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
 * @file defog.hpp
 * @brief MediaLibrary Defog CPP API module
 **/

#pragma once
#include <stdint.h>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <tl/expected.hpp>

#include "media_library_types.hpp"

/** @defgroup defog_type_definitions MediaLibrary Defog CPP API definitions
 *  @{
 */
class MediaLibraryDefog;
using MediaLibraryDefogPtr = std::shared_ptr<MediaLibraryDefog>;

class MediaLibraryDefog
{
protected:
  class Impl;
  std::shared_ptr<Impl> m_impl;

public:
  /**
   * @brief Create the defog module
   *
   * @param[in] config_string - json configuration string
   * @return tl::expected<MediaLibraryDefogPtr, media_library_return> -
   * An expected object that holds either a shared pointer
   * to an MediaLibraryDefog object, or a error code.
   */
  static tl::expected<std::shared_ptr<MediaLibraryDefog>, media_library_return> create(std::string config_string);

  /**
   * @brief Constructor for the defog module
   *
   * @param[in] impl - shared pointer to the implementation object
   * @note This constructor is used internally by the create function.
   */
  MediaLibraryDefog(std::shared_ptr<MediaLibraryDefog::Impl> impl);

  /**
   * @brief Destructor for the defog module
   */
  ~MediaLibraryDefog();

  /**
   * @brief Configure the defog module with new json string
   *
   * Read the json string and decode it to create the defog_config_t object
   * @param[in] config_string - configuration json as string
   * @return media_library_return - status of the configuration operation
   */
  media_library_return configure(std::string config_string);

  /**
   * @brief Configure the defog module with defog_config_t object
   *
   * Update the defog_config_t object
   * @param[in] defog_config_t - defog_config_t object
   * @param[in] hailort_t - hailort_t object
   * @return media_library_return - status of the configuration operation
   */
  media_library_return configure(defog_config_t &defog_configs, hailort_t &hailort_configs);

  /**
   * @brief get the defog configurations object
   *
   * @return defog_config_t - defog configurations
   */
  defog_config_t &get_defog_configs();

  /**
   * @brief get the hailort configurations object
   *
   * @return hailort_t - hailort configurations
   */
  hailort_t &get_hailort_configs();

  /**
   * @brief check enabled flag
   *
   * @return bool - enabled config flag
   */
  bool is_enabled();
};

/** @} */ // end of defog_type_definitions
