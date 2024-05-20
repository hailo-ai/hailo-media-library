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
#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include <fstream>
#include <string>

namespace gstmedialibcommon {

    inline std::string read_json_string_from_file(const gchar *file_path)
    {
        std::ifstream file_to_read;
        file_to_read.open(file_path);
        if (!file_to_read.is_open())
            throw std::runtime_error("config path is not valid");
        std::string file_string((std::istreambuf_iterator<char>(file_to_read)), std::istreambuf_iterator<char>());
        file_to_read.close();
        return file_string;
    }

    inline void strip_string_syntax(std::string &pipeline_input)
    {
        if (pipeline_input.front() == '\'' && pipeline_input.back() == '\'')
        {
            pipeline_input.erase(0, 1);
            pipeline_input.pop_back();
        }
    }

} // namespace gsthailocommon
