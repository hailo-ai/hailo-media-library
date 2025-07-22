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

#include "gstmedialibptrs.hpp"
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

template <typename T> std::string get_name(const T *ptr)
{
    gchar *name_c(gst_object_get_name(GST_OBJECT_CAST(ptr)));
    std::string name_str(name_c);
    g_free(name_c);
    return name_str;
}

template <typename GST_TYPE, auto unref_func> std::string get_name(const GstPtr<GST_TYPE, unref_func> &ptr)
{
    return get_name(ptr.get());
}

namespace ptrs
{

GstCapsPtr parse_query_caps(GstQuery *query);
GstCapsPtr parse_event_caps(GstEvent *event);
GstCapsPtr parse_query_accept_caps(GstQuery *query);

struct query_allocation_result
{
    GstCapsPtr caps;
    bool need_pool;
};
query_allocation_result parse_query_allocation(GstQuery *query);

struct buffer_pool_config_result
{
    GstCapsPtr caps;
    uint size;
    uint min_buffers;
    uint max_buffers;
};
buffer_pool_config_result buffer_pool_config_get_params(GstStructure *config);
GstCapsPtr fixate_caps(GstCapsPtr &caps);
GstVideoCodecStatePtr video_encoder_set_output_state(GstVideoEncoder *encoder, GstCapsPtr &caps,
                                                     GstVideoCodecStatePtr &state);

GstBufferPtr get_buffer_from_sample(GstSamplePtr &sample);
GstBufferPtr get_buffer_from_pad_probe_info(GstPadProbeInfo *info);
GstFlowReturn push_buffer_to_app_src(GstAppSrcPtr &appsrc, GstBufferPtr &buffer);
GstFlowReturn push_buffer_to_pad(GstPad *pad, GstBufferPtr &buffer);

GstElementPtr get_bin_by_name(const GstElementPtr &element, const std::string &name);
GstElementPtr get_bin_by_name(const GstElementPtr &element, const char *name);
GstAppSrcPtr element_to_app_src(GstElementPtr &element);

const char *get_pad_name(const GstPadPtr &pad);
gboolean pad_event_default(GstPad *pad, GstObject *parent, GstEventPtr &event);
bool add_pad_to_element(GstElement *element, GstPadPtr &pad);

} // namespace ptrs

} // namespace glib_cpp
