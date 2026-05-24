/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#pragma once

#include <string>

namespace common
{

/**
 * @brief Read the entire contents of a file into a string.
 *
 * Uses cloexec::ifstream + stringstream::rdbuf for the read. Throws std::runtime_error
 * if the file cannot be opened or if the read fails (badbit set).
 *
 * @param path Filesystem path to the file to read.
 * @return std::string containing the file's full contents.
 */
std::string read_file(const std::string &path);

} // namespace common
