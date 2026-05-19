#include "mkv_concatenator.hpp"

#include <glib-object.h>
#include <gst/app/gstappsink.h>

#include "common_utils.hpp"

MkvConcatenator::MkvConcatenator() : m_pipeline(nullptr), m_appsink(nullptr), m_is_done(false), m_started(false)
{
    gst_init(nullptr, nullptr);
}

MkvConcatenator::~MkvConcatenator()
{
    stop();
}

void MkvConcatenator::on_demux_pad_added(GstElement * /*demux*/, GstPad *new_pad, gpointer user_data)
{
    auto *context = static_cast<DemuxContext *>(user_data);

    GstCaps *caps = gst_pad_get_current_caps(new_pad);
    if (!caps)
    {
        caps = gst_pad_query_caps(new_pad, nullptr);
    }

    if (!caps || gst_caps_is_any(caps) || gst_caps_get_size(caps) == 0)
    {
        if (caps)
            gst_caps_unref(caps);
        return;
    }

    const GstStructure *structure = gst_caps_get_structure(caps, 0);
    const gchar *media_type = gst_structure_get_name(structure);

    // Only link video pads (skip audio or subtitle pads)
    if (g_str_has_prefix(media_type, "video/"))
    {
        GstPad *queue_sink = gst_element_get_static_pad(context->queue, "sink");
        if (queue_sink && !gst_pad_is_linked(queue_sink))
        {
            GstPadLinkReturn link_result = gst_pad_link(new_pad, queue_sink);
            if (GST_PAD_LINK_FAILED(link_result))
            {
                HAILO_ANALYTICS_LOG_ERROR("MkvConcatenator: Failed to link demux pad to queue");
            }
        }
        if (queue_sink)
        {
            gst_object_unref(queue_sink);
        }
    }

    gst_caps_unref(caps);
}

bool MkvConcatenator::start(const std::vector<std::string> &file_paths)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_started)
    {
        HAILO_ANALYTICS_LOG_ERROR("MkvConcatenator: Already started");
        return false;
    }

    if (file_paths.empty())
    {
        HAILO_ANALYTICS_LOG_ERROR("MkvConcatenator: No files provided");
        return false;
    }

    // Verify all files exist
    for (const auto &path : file_paths)
    {
        if (!std::filesystem::exists(path))
        {
            HAILO_ANALYTICS_LOG_ERROR("MkvConcatenator: File not found: {}", path);
            return false;
        }
    }

    // Detect codec from the first file
    auto codec = CodecUtils::detect_codec_type(file_paths[0]);
    if (codec == CodecUtils::CodecType::UNKNOWN)
    {
        HAILO_ANALYTICS_LOG_ERROR("MkvConcatenator: Could not detect codec from: {}", file_paths[0]);
        return false;
    }

    const char *parser_element = (codec == CodecUtils::CodecType::H264) ? "h264parse" : "h265parse";

    // Create pipeline and shared elements
    m_pipeline = gst_pipeline_new("mkv-concatenator");

    GstElement *concat = gst_element_factory_make("concat", "concat");
    GstElement *parser = gst_element_factory_make(parser_element, "parser");
    GstElement *muxer = gst_element_factory_make("matroskamux", "muxer");
    m_appsink = gst_element_factory_make("appsink", "sink");

    if (!m_pipeline || !concat || !parser || !muxer || !m_appsink)
    {
        HAILO_ANALYTICS_LOG_ERROR("MkvConcatenator: Failed to create GStreamer elements");
        cleanup();
        return false;
    }

    // Configure matroskamux for streaming (no seeking required)
    g_object_set(G_OBJECT(muxer), "streamable", TRUE, nullptr);

    // Configure appsink: no signal emission, we pull synchronously
    g_object_set(G_OBJECT(m_appsink), "emit-signals", FALSE, "sync", FALSE, nullptr);

    gst_bin_add_many(GST_BIN(m_pipeline), concat, parser, muxer, m_appsink, nullptr);

    if (!gst_element_link_many(concat, parser, muxer, m_appsink, nullptr))
    {
        HAILO_ANALYTICS_LOG_ERROR("MkvConcatenator: Failed to link concat -> parser -> muxer -> appsink");
        cleanup();
        return false;
    }

    // Reserve to prevent reallocation — pointers to elements are passed to GStreamer signal callbacks
    m_demux_contexts.reserve(file_paths.size());

    // For each file: filesrc -> matroskademux --(pad-added)--> queue -> concat.sink_N
    for (size_t i = 0; i < file_paths.size(); ++i)
    {
        std::string filesrc_name = "filesrc_" + std::to_string(i);
        std::string demux_name = "demux_" + std::to_string(i);
        std::string queue_name = "queue_" + std::to_string(i);

        GstElement *filesrc = gst_element_factory_make("filesrc", filesrc_name.c_str());
        GstElement *demux = gst_element_factory_make("matroskademux", demux_name.c_str());
        GstElement *queue = gst_element_factory_make("queue", queue_name.c_str());

        if (!filesrc || !demux || !queue)
        {
            HAILO_ANALYTICS_LOG_ERROR("MkvConcatenator: Failed to create elements for file {}", i);
            cleanup();
            return false;
        }

        g_object_set(G_OBJECT(filesrc), "location", file_paths[i].c_str(), nullptr);

        gst_bin_add_many(GST_BIN(m_pipeline), filesrc, demux, queue, nullptr);

        if (!gst_element_link(filesrc, demux))
        {
            HAILO_ANALYTICS_LOG_ERROR("MkvConcatenator: Failed to link filesrc -> demux for file {}", i);
            cleanup();
            return false;
        }

        // Request a sink pad from concat for this file's stream
        GstPad *concat_sink_pad = gst_element_request_pad_simple(concat, "sink_%u");
        GstPad *queue_src_pad = gst_element_get_static_pad(queue, "src");

        if (!concat_sink_pad || !queue_src_pad)
        {
            HAILO_ANALYTICS_LOG_ERROR("MkvConcatenator: Failed to get pads for file {}", i);
            if (concat_sink_pad)
                gst_object_unref(concat_sink_pad);
            if (queue_src_pad)
                gst_object_unref(queue_src_pad);
            cleanup();
            return false;
        }

        if (GST_PAD_LINK_FAILED(gst_pad_link(queue_src_pad, concat_sink_pad)))
        {
            HAILO_ANALYTICS_LOG_ERROR("MkvConcatenator: Failed to link queue -> concat for file {}", i);
            gst_object_unref(queue_src_pad);
            gst_object_unref(concat_sink_pad);
            cleanup();
            return false;
        }

        gst_object_unref(queue_src_pad);

        // Store context for pad-added callback (demux has dynamic pads)
        m_demux_contexts.push_back({queue, concat_sink_pad});
        g_signal_connect(demux, "pad-added", G_CALLBACK(on_demux_pad_added), &m_demux_contexts.back());
    }

    // Start the pipeline
    GstStateChangeReturn state_ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (state_ret == GST_STATE_CHANGE_FAILURE)
    {
        HAILO_ANALYTICS_LOG_ERROR("MkvConcatenator: Failed to start pipeline");
        cleanup();
        return false;
    }

    m_started = true;
    m_is_done = false;

    HAILO_ANALYTICS_LOG_INFO("MkvConcatenator: Started with {} files", file_paths.size());
    return true;
}

std::vector<uint8_t> MkvConcatenator::pull_chunk(int64_t timeout_ms)
{
    if (m_is_done || !m_appsink)
    {
        return {};
    }

    GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(m_appsink), timeout_ms * GST_MSECOND);
    if (!sample)
    {
        // Check if EOS was reached
        if (gst_app_sink_is_eos(GST_APP_SINK(m_appsink)))
        {
            m_is_done = true;
            HAILO_ANALYTICS_LOG_INFO("MkvConcatenator: Reached end of stream");
        }
        return {};
    }

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer)
    {
        gst_sample_unref(sample);
        return {};
    }

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ))
    {
        gst_sample_unref(sample);
        return {};
    }

    std::vector<uint8_t> data(map.data, map.data + map.size);

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    return data;
}

bool MkvConcatenator::is_done() const
{
    return m_is_done;
}

void MkvConcatenator::stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    cleanup();
}

void MkvConcatenator::cleanup()
{
    if (m_pipeline)
    {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);

        // Release requested concat pads
        for (auto &ctx : m_demux_contexts)
        {
            if (ctx.concat_sink_pad)
            {
                gst_object_unref(ctx.concat_sink_pad);
            }
        }
        m_demux_contexts.clear();

        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        m_appsink = nullptr; // Owned by pipeline
    }

    m_started = false;
    m_is_done = true;
}
