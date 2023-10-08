/*
* Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
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
 * @file config_manager.hpp
 * @brief MediaLibrary Configuration loading and processing
 **/

# pragma once
#include <stdint.h>
#include <vector>
#include <string>

#include "media_library_types.hpp"

class ConfigManager;

class ConfigManager
{
public:
    /**
     * @brief Constructor for the ConfigManager module
     *
     */
    ConfigManager();

    /**
     * @brief Destructor for the ConfigManager module
     */
    ~ConfigManager();

    /**
     * @brief Copy constructor (deleted)
     */
    ConfigManager(const ConfigManager&) = delete;

     /**
     * @brief Copy assignment operator (deleted)
     */
    ConfigManager& operator=(const ConfigManager&) = delete;

    /**
     * @brief Move constructor
     */
    ConfigManager(ConfigManager&&) = delete;

    /**
    * @brief Move assignment
    */
    ConfigManager& operator=(ConfigManager&&) = delete;

    /**
    * @brief Validate the user's configuration json string against internal schema
    *
    * @param[in] user_config_string - the user's configuration (as a json string)
    * @return media_library_return
    */
    media_library_return validate_configuration(const std::string &user_config_string);

    /**
    * @brief Validate a json string and populate a configuration struct 
    *
    * @param[in] user_config - the user's configuration (as a json string)
    * @param[out] pre_proc_conf - the user's configuration (as a json string)
    * @return media_library_return
    */
    media_library_return config_string_to_struct(const std::string &user_config_string,
                                                 pre_proc_op_configurations &pre_proc_conf);

private:
    class ConfigManagerImpl; // internal implementation class
    std::unique_ptr<ConfigManagerImpl> m_config_manager_impl;
};
