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
#include <string>

namespace gstmedialibcommon
{
std::string read_json_string_from_file(const std::string &file_path);
void strip_string_syntax(std::string &pipeline_input);
} // namespace gstmedialibcommon

namespace glib_cpp
{
struct t_error_message
{
    std::string message;
    std::string debug_info;
};

t_error_message parse_error(GstMessage *msg);
std::string get_string_from_gvalue(const GValue *value);

template <typename T> std::string get_name(const T *element)
{
    gchar *name_c(gst_object_get_name(GST_OBJECT_CAST(element)));
    std::string name_str(name_c);
    g_free(name_c);
    return name_str;
}
} // namespace glib_cpp
