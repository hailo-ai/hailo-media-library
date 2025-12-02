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

namespace glib_cpp
{

t_error_message parse_error(GstMessage *msg)
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

std::string get_string_from_gvalue(const GValue *value)
{
    gchar *val_c = g_value_dup_string(value);
    std::string result = std::string(val_c);
    g_free(val_c);
    return result;
}

namespace ptrs
{

GstCapsPtr parse_query_caps(GstQuery *query)
{
    GstCaps *tmp;
    GstCapsPtr ptr;
    gst_query_parse_caps(query, &tmp);
    ptr = tmp;
    ptr.set_auto_unref(false);
    return ptr;
}

GstCapsPtr parse_event_caps(GstEvent *event)
{
    GstCaps *tmp;
    GstCapsPtr ptr;
    gst_event_parse_caps(event, &tmp);
    ptr = tmp;
    ptr.set_auto_unref(false);
    return ptr;
}

GstCapsPtr parse_query_accept_caps(GstQuery *query)
{
    GstCaps *tmp;
    GstCapsPtr ptr;
    gst_query_parse_accept_caps(query, &tmp);
    ptr = tmp;
    ptr.set_auto_unref(false);
    return ptr;
}

query_allocation_result parse_query_allocation(GstQuery *query)
{
    query_allocation_result ret;
    GstCaps *caps_tmp;
    gboolean need_tmp;
    gst_query_parse_allocation(query, &caps_tmp, &need_tmp);
    ret.caps = caps_tmp;
    ret.need_pool = need_tmp;
    return ret;
}

buffer_pool_config_result buffer_pool_config_get_params(GstStructure *config)
{
    buffer_pool_config_result ret;
    GstCaps *caps_tmp;
    gst_buffer_pool_config_get_params(config, &caps_tmp, &ret.size, &ret.min_buffers, &ret.max_buffers);
    ret.caps = caps_tmp;
    ret.caps.set_auto_unref(false);
    return ret;
}

GstCapsPtr fixate_caps(GstCapsPtr &caps)
{
    GstCapsPtr ptr;
    ptr = gst_caps_fixate(caps);
    caps.set_auto_unref(false);
    return ptr;
}

GstVideoCodecStatePtr video_encoder_set_output_state(GstVideoEncoder *encoder, GstCapsPtr &caps,
                                                     GstVideoCodecStatePtr &state)
{
    GstVideoCodecStatePtr output_format;
    output_format = gst_video_encoder_set_output_state(encoder, caps, state);
    caps.set_auto_unref(false);
    state.set_auto_unref(false);
    return output_format;
}

GstBufferPtr get_buffer_from_sample(GstSamplePtr &sample)
{
    GstBufferPtr buffer = gst_sample_get_buffer(sample);
    buffer.set_auto_unref(false);
    return buffer;
}

GstBufferPtr get_buffer_from_pad_probe_info(GstPadProbeInfo *info)
{
    GstBufferPtr buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    buffer.set_auto_unref(false);
    return buffer;
}

GstFlowReturn push_buffer_to_app_src(GstAppSrcPtr &appsrc, GstBufferPtr &buffer)
{
    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc.get()), buffer);
    buffer.set_auto_unref(false);
    return ret;
}

GstFlowReturn push_buffer_to_pad(GstPad *pad, GstBufferPtr &buffer)
{
    GstFlowReturn ret = gst_pad_push(pad, buffer);
    buffer.set_auto_unref(false);
    return ret;
}

GstElementPtr get_bin_by_name(const GstElementPtr &element, const std::string &name)
{
    return gst_bin_get_by_name(GST_BIN(element.get()), name.c_str());
}

GstElementPtr get_bin_by_name(const GstElementPtr &element, const gchar *name)
{
    return gst_bin_get_by_name(GST_BIN(element.get()), name);
}

const char *get_pad_name(const GstPadPtr &pad)
{
    return GST_PAD_NAME(pad.get());
}

gboolean pad_event_default(GstPad *pad, GstObject *parent, GstEventPtr &event)
{
    bool ret = gst_pad_event_default(pad, parent, event);
    event.set_auto_unref(false);
    return ret;
}

GstAppSrcPtr element_to_app_src(GstElementPtr &element)
{
    GstAppSrcPtr ptr = GST_APP_SRC(element.get());
    element.set_auto_unref(false);
    return ptr;
}

bool add_pad_to_element(GstElement *element, GstPadPtr &pad)
{
    bool ret = gst_element_add_pad(element, pad);
    pad.set_auto_unref(false);
    return ret;
}

bool remove_pad_from_element(GstElement *element, GstPad *pad)
{
    bool ret = gst_element_remove_pad(element, pad);
    return ret;
}
} // namespace ptrs

} // namespace glib_cpp
