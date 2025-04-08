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
#include "gstmedialibcommon.hpp"

#include <fstream>
#include <sstream>

std::string gstmedialibcommon::read_json_string_from_file(const std::string &file_path)
{
    std::ifstream file_to_read;
    file_to_read.open(file_path);
    if (!file_to_read.is_open())
        throw std::runtime_error("config path is not valid");
    std::stringstream buffer;
    buffer << file_to_read.rdbuf();
    std::string file_string = buffer.str();
    file_to_read.close();
    return file_string;
}

void gstmedialibcommon::strip_string_syntax(std::string &pipeline_input)
{
    if (pipeline_input.front() == '\'' && pipeline_input.back() == '\'')
    {
        pipeline_input.erase(0, 1);
        pipeline_input.pop_back();
    }
}

glib_cpp::t_error_message glib_cpp::parse_error(GstMessage *msg)
{
    gchar *debug;
    GError *err;
    t_error_message result;

    gst_message_parse_error(msg, &err, &debug);
    result.message = err->message;
    result.debug_info = debug ? std::string(debug) : "none";
    g_error_free(err);
    g_free(debug);
    return result;
}

std::string glib_cpp::get_string_from_gvalue(const GValue *value)
{
    gchar *val_c = g_value_dup_string(value);
    std::string result = std::string(val_c);
    g_free(val_c);
    return result;
}
