/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#include "common/file_reader.hpp"

#include "media_library/cloexec_fstream.hpp"
#include <sstream>
#include <stdexcept>

namespace common
{

std::string read_file(const std::string &path)
{
    cloexec::ifstream input_stream(path);
    if (!input_stream.is_open())
    {
        throw std::runtime_error("read_file: failed to open " + path);
    }

    std::stringstream contents;
    contents << input_stream.rdbuf();

    if (input_stream.bad())
    {
        throw std::runtime_error("read_file: failed to read " + path);
    }

    return contents.str();
}

} // namespace common
