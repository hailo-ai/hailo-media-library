#include "segmented_mkv_muxer.hpp"

#include <glib-object.h>
#include <gst/app/gstappsrc.h>
#include <gst/gstformat.h>
#include <string.h>
#include <time.h>
#include <chrono>
#include <utility>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

FrameData::FrameData(const uint8_t *nal_data, size_t size, uint64_t timestamp)
    : data(nal_data, nal_data + size), pts(timestamp), dts(0), is_keyframe(false)
{
}

SegmentInfo::SegmentInfo() : start_pts(0), end_pts(0), start_time_epoch_ms(0), index(0), completed(false)
{
}

EpochNamingData::EpochNamingData(const std::string &path, const std::string &prefix)
    : output_path(path), file_prefix(prefix), segment_counter(0), is_valid(true)
{
}

GStreamerMkvSegmenter::GStreamerMkvSegmenter(CodecType codec, const std::string &output_path,
                                             const std::string &file_prefix, uint32_t segment_duration_sec)
    : m_codec_type(codec), m_output_path(output_path), m_file_prefix(file_prefix),
      m_segment_duration_sec(segment_duration_sec), m_epoch_naming_data(nullptr), m_notification_callback(nullptr),
      m_callback_user_data(nullptr), m_pipeline(nullptr), m_appsrc(nullptr), m_parser(nullptr), m_bus(nullptr),
      m_last_segment_end_running_time(0), m_current_segment_start_running_time(0), m_pts_to_epoch_offset_ms(0),
      m_pts_epoch_offset_initialized(false), m_processing_active(false), m_initialized(false), m_running(false)
{

    m_epoch_naming_data = new EpochNamingData(output_path, file_prefix);
}

GStreamerMkvSegmenter::~GStreamerMkvSegmenter()
{

    // 1. FIRST: Stop pipeline to prevent new segments
    if (m_running)
    {
        stop();
    }

    // 2. SECOND: Invalidate naming data before cleanup
    if (m_epoch_naming_data)
    {
        std::lock_guard<std::mutex> lock(m_epoch_naming_data->data_mutex);
        m_epoch_naming_data->is_valid = false;
    }

    // 3. THIRD: Cleanup GStreamer resources
    cleanup();

    // Delete naming data after cleanup
    delete m_epoch_naming_data;
    m_epoch_naming_data = nullptr;
}

bool GStreamerMkvSegmenter::initialize()
{
    if (m_initialized)
    {
        return true;
    }

    // Initialize GStreamer
    if (!gst_is_initialized())
    {
        gst_init(nullptr, nullptr);
    }

    if (!create_pipeline())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create GStreamer pipeline");
        return false;
    }

    m_initialized = true;
    return true;
}

bool GStreamerMkvSegmenter::create_pipeline()
{
    // Create pipeline elements
    m_pipeline = gst_pipeline_new("mkv-segmenter");
    if (!m_pipeline)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create pipeline");
        return false;
    }

    // Create appsrc
    m_appsrc = gst_element_factory_make("appsrc", "source");
    if (!m_appsrc)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create appsrc");
        return false;
    }

    // Create parser based on codec type
    const char *parser_name = (m_codec_type == CodecType::H264) ? "h264parse" : "h265parse";
    m_parser = gst_element_factory_make(parser_name, "parser");
    if (!m_parser)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create {}", parser_name);
        return false;
    }

    // Create splitmuxsink instead of muxer + multifilesink
    GstElement *splitmuxsink = gst_element_factory_make("splitmuxsink", "splitsink");
    if (!splitmuxsink)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create splitmuxsink (requires GStreamer 1.8+)");
        return false;
    }

    // Configure appsrc
    GstCaps *caps;
    if (m_codec_type == CodecType::H264)
    {
        caps = gst_caps_new_simple("video/x-h264", "stream-format", G_TYPE_STRING, "byte-stream", "alignment",
                                   G_TYPE_STRING, "nal", nullptr);
    }
    else
    {
        caps = gst_caps_new_simple("video/x-h265", "stream-format", G_TYPE_STRING, "byte-stream", "alignment",
                                   G_TYPE_STRING, "nal", nullptr);
    }

    g_object_set(G_OBJECT(m_appsrc), "caps", caps, "format", GST_FORMAT_TIME, "is-live", FALSE, "do-timestamp", FALSE,
                 nullptr);
    gst_caps_unref(caps);

    // Add flow-control limits
    const guint64 maxBytes = 3 * 1024 * 1024;       // ~6 MB buffered
    const guint maxBuffers = 60;                    // ≈ two seconds at 30 fps
    g_object_set(G_OBJECT(m_appsrc), "block", TRUE, // wait when the limits are hit
                 "max-bytes", maxBytes, "max-buffers", maxBuffers, nullptr);

    // Configure parser
    g_object_set(G_OBJECT(m_parser), "config-interval", -1, nullptr);

    // Configure splitmuxsink - SAFE VERSION
    std::string location_pattern = generate_location_pattern();

    /*
    HAILO_ANALYTICS_LOG_INFO("=== SPLITMUXSINK DEBUG INFO ===");
    HAILO_ANALYTICS_LOG_INFO("Location pattern: {}", location_pattern);
    HAILO_ANALYTICS_LOG_INFO("Segment duration: {} seconds", m_segment_duration_sec);
    */
    GstStructure *muxer_props =
        gst_structure_new("properties", "streamable", G_TYPE_BOOLEAN, FALSE, "offset-to-zero", G_TYPE_BOOLEAN, TRUE,
                          "writing-app", G_TYPE_STRING, "GStreamerMkvSegmenter", NULL);

    g_object_set(G_OBJECT(splitmuxsink), "max-size-time", (guint64)(m_segment_duration_sec * GST_SECOND),
                 "muxer-factory", "matroskamux",
                 // KEY: Set muxer properties to fix seeking/duration
                 "muxer-properties", muxer_props, "async-finalize", FALSE, // Ensure proper file finalization
                 nullptr);

    gst_structure_free(muxer_props); // Free the structure after use

    // Connect callback with safe data (NOT 'this')
    g_signal_connect(splitmuxsink, "format-location-full", G_CALLBACK(on_epoch_format_location_safe),
                     m_epoch_naming_data);

    // Add elements to pipeline
    gst_bin_add_many(GST_BIN(m_pipeline), m_appsrc, m_parser, splitmuxsink, nullptr);

    // Link elements
    if (!gst_element_link_many(m_appsrc, m_parser, splitmuxsink, nullptr))
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to link elements");
        return false;
    }

    // Set up bus
    m_bus = gst_element_get_bus(m_pipeline);
    gst_bus_set_sync_handler(m_bus, on_bus_message, this, nullptr);

    return true;
}

bool GStreamerMkvSegmenter::start()
{
    if (!m_initialized)
    {
        HAILO_ANALYTICS_LOG_ERROR("Segmenter not initialized");
        return false;
    }

    if (m_running)
    {
        return true;
    }

    // Start processing thread
    m_processing_active = true;
    m_processing_thread = std::thread(&GStreamerMkvSegmenter::process_frame_queue, this);

    // Start pipeline
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to start pipeline");
        m_processing_active = false;
        if (m_processing_thread.joinable())
        {
            m_processing_thread.join();
        }
        return false;
    }

    m_running = true;
    return true;
}

bool GStreamerMkvSegmenter::stop()
{
    if (!m_running)
    {
        return true;
    }

    // Stop processing thread
    m_processing_active = false;
    m_queue_cv.notify_all();
    if (m_processing_thread.joinable())
    {
        m_processing_thread.join();
    }

    // Send EOS
    gst_app_src_end_of_stream(GST_APP_SRC(m_appsrc));

    // Wait for EOS
    GstMessage *msg = gst_bus_timed_pop_filtered(m_bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_EOS);
    if (msg)
    {
        gst_message_unref(msg);
    }

    // Stop pipeline
    gst_element_set_state(m_pipeline, GST_STATE_NULL);

    m_running = false;
    return true;
}

void GStreamerMkvSegmenter::cleanup()
{
    if (m_running)
    {
        stop();
    }

    destroy_pipeline();
    m_initialized = false;
}

void GStreamerMkvSegmenter::destroy_pipeline()
{
    if (m_bus)
    {
        gst_object_unref(m_bus);
        m_bus = nullptr;
    }

    if (m_pipeline)
    {
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }

    m_appsrc = nullptr;
    m_parser = nullptr;
}

void GStreamerMkvSegmenter::set_segment_notification_callback(SegmentNotificationCallback callback, void *user_data)
{
    m_notification_callback = callback;
    m_callback_user_data = user_data;
}

bool GStreamerMkvSegmenter::feed_frame(const uint8_t *nal_data, size_t size, uint64_t pts_ns)
{
    if (!m_running)
    {
        return false;
    }

    // Create frame data
    FrameData frame(nal_data, size, pts_ns);
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_frame_queue.push(std::move(frame));
    }
    m_queue_cv.notify_one();

    return true;
}

void GStreamerMkvSegmenter::process_frame_queue()
{
    while (m_processing_active)
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        m_queue_cv.wait(lock, [this] { return !m_frame_queue.empty() || !m_processing_active; });

        if (!m_processing_active)
        {
            break;
        }

        // Process frames
        std::queue<FrameData> frames_to_process;
        frames_to_process = std::move(m_frame_queue);
        m_frame_queue = std::queue<FrameData>();

        lock.unlock();

        // Process frames
        while (!frames_to_process.empty())
        {
            FrameData &frame = frames_to_process.front();

            // Create GStreamer buffer
            GstBuffer *buffer = gst_buffer_new_allocate(nullptr, frame.data.size(), nullptr);
            if (!buffer)
            {
                HAILO_ANALYTICS_LOG_ERROR("Failed to allocate GstBuffer");
                break;
            }

            GstMapInfo map;
            gst_buffer_map(buffer, &map, GST_MAP_WRITE);
            memcpy(map.data, frame.data.data(), frame.data.size());
            gst_buffer_unmap(buffer, &map);

            // Set timestamps
            GST_BUFFER_PTS(buffer) = frame.pts;
            if (frame.dts != 0)
            {
                GST_BUFFER_DTS(buffer) = frame.dts;
            }

            // Push buffer
            GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buffer);
            if (ret != GST_FLOW_OK)
            {
                HAILO_ANALYTICS_LOG_ERROR("Failed to push buffer: {}", ret);
                gst_buffer_unref(buffer);
                break;
            }

            frames_to_process.pop();
        }
    }
}

bool GStreamerMkvSegmenter::is_keyframe(const uint8_t *nal_data, size_t size) const
{
    if (size < 5)
        return false;

    // Parse all NAL units in the frame to find keyframe indicators
    const uint8_t *data = nal_data;
    size_t remaining = size;
    bool found_keyframe = false;

    while (remaining > 4)
    {
        // Find start code (0x00 0x00 0x00 0x01 or 0x00 0x00 0x01)
        size_t start_code_size = 0;

        if (remaining >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1)
        {
            start_code_size = 4;
        }
        else if (remaining >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1)
        {
            start_code_size = 3;
        }
        else
        {
            // Move to next byte and continue searching
            data++;
            remaining--;
            continue;
        }

        // Move past start code
        data += start_code_size;
        remaining -= start_code_size;

        if (remaining == 0)
            break;

        // Parse NAL unit header
        if (m_codec_type == CodecType::H264)
        {
            uint8_t nal_header = data[0];
            uint8_t nal_type = nal_header & 0x1F;

            // H264 keyframe indicators:
            // 5 = IDR slice (primary keyframe)
            // 7 = SPS (sequence start, indicates keyframe)
            // 8 = PPS (picture parameters, often with keyframes)
            if (nal_type == 5)
            {
                found_keyframe = true;
                break; // IDR is definitive keyframe
            }
            else if (nal_type == 7)
            {
                found_keyframe = true; // Don't break, look for IDR
            }
            else if (nal_type == 8)
            {
                found_keyframe = true; // Don't break, look for IDR
            }
        }
        else
        { // H265
            if (remaining < 2)
                break;

            uint16_t nal_header = (data[0] << 8) | data[1];
            uint8_t nal_type = (nal_header >> 9) & 0x3F;

            // H265 keyframe indicators:
            // 19-20 = IDR slices (primary keyframes)
            // 21 = CRA (Clean Random Access)
            // 32 = VPS, 33 = SPS, 34 = PPS (sequence/picture parameters)
            if ((nal_type >= 19 && nal_type <= 21))
            {
                found_keyframe = true;
                break; // IDR/CRA is definitive keyframe
            }
            else if (nal_type >= 32 && nal_type <= 34)
            {
                found_keyframe = true; // Don't break, look for IDR/CRA
            }
        }

        // Find next NAL unit by looking for next start code
        bool found_next = false;
        for (size_t i = 1; i < remaining; i++)
        {
            if (i + 3 < remaining && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1)
            {
                data += i;
                remaining -= i;
                found_next = true;
                break;
            }
            else if (i + 2 < remaining && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
            {
                data += i;
                remaining -= i;
                found_next = true;
                break;
            }
        }

        if (!found_next)
        {
            // No more NAL units found
            break;
        }
    }

    return found_keyframe;
}

std::string GStreamerMkvSegmenter::generate_location_pattern() const
{
    return m_output_path + "/" + m_file_prefix + "_%06d.mkv";
}

uint64_t GStreamerMkvSegmenter::get_current_epoch_time_ms() const
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return static_cast<uint64_t>(millis);
}

GstBusSyncReply GStreamerMkvSegmenter::on_bus_message(GstBus * /*bus*/, GstMessage *message, gpointer user_data)
{
    GStreamerMkvSegmenter *segmenter = static_cast<GStreamerMkvSegmenter *>(user_data);

    switch (GST_MESSAGE_TYPE(message))
    {
    case GST_MESSAGE_ERROR: {
        GError *error;
        gchar *debug;
        gst_message_parse_error(message, &error, &debug);
        HAILO_ANALYTICS_LOG_ERROR("ERROR from element {}: {}", GST_OBJECT_NAME(message->src), error->message);
        HAILO_ANALYTICS_LOG_ERROR("Debug info: {}", debug ? debug : "none");
        g_clear_error(&error);
        g_free(debug);
        gst_message_unref(message);
        return GST_BUS_DROP;
    }
    case GST_MESSAGE_WARNING: {
        GError *error;
        gchar *debug;
        gst_message_parse_warning(message, &error, &debug);
        HAILO_ANALYTICS_LOG_ERROR("WARNING from element {}: {}", GST_OBJECT_NAME(message->src), error->message);
        HAILO_ANALYTICS_LOG_ERROR("Debug info: {}", debug ? debug : "none");
        g_clear_error(&error);
        g_free(debug);
        gst_message_unref(message);
        return GST_BUS_DROP;
    }
    case GST_MESSAGE_EOS:
        return GST_BUS_PASS; // Let stop() receive it

    case GST_MESSAGE_ELEMENT: {
        const GstStructure *s = gst_message_get_structure(message);

        // Debug: Print all element messages
        /*
        gchar* struct_str = gst_structure_to_string(s);
        HAILO_ANALYTICS_LOG_INFO("MKV Muxer ELEMENT MESSAGE: {}", struct_str);
        g_free(struct_str);
        */

        // Handle fragment opened (new segment starts)
        if (gst_structure_has_name(s, "splitmuxsink-fragment-opened"))
        {

            const gchar *filename = gst_structure_get_string(s, "location");
            guint64 running_time = 0;

            gst_structure_get_uint64(s, "running-time", &running_time);

            // Track the start of this segment
            segmenter->m_current_segment_start_running_time = running_time;

            // Calibrate PTS-to-epoch offset on the first segment (pipeline latency ≈ 0 at start)
            uint64_t now_epoch_ms = segmenter->get_current_epoch_time_ms();
            if (!segmenter->m_pts_epoch_offset_initialized)
            {
                segmenter->m_pts_to_epoch_offset_ms =
                    static_cast<int64_t>(now_epoch_ms) - static_cast<int64_t>(running_time / 1000000);
                segmenter->m_pts_epoch_offset_initialized = true;
            }

            // Calculate segment start epoch from PTS (immune to pipeline latency accumulation)
            uint64_t pts_based_epoch_ms = static_cast<uint64_t>(static_cast<int64_t>(running_time / 1000000) +
                                                                segmenter->m_pts_to_epoch_offset_ms);

            // Extract segment index and store start time
            if (filename)
            {
                uint32_t segment_index = segmenter->extract_segment_index(filename);
                segmenter->m_segment_start_times[segment_index] = running_time;
                segmenter->m_segment_start_epoch_times[segment_index] = pts_based_epoch_ms;
            }
        }
        // Handle fragment closed (segment complete)
        else if (gst_structure_has_name(s, "splitmuxsink-fragment-closed"))
        {
            const gchar *filename = gst_structure_get_string(s, "location");
            guint64 running_time = 0;

            gst_structure_get_uint64(s, "running-time", &running_time);

            if (filename && segmenter)
            {
                segmenter->handle_split_mux_segment_with_running_time(filename, running_time);
            }

            // Update for next segment
            segmenter->m_last_segment_end_running_time = running_time;
        }
        gst_message_unref(message);
        return GST_BUS_DROP;
    }
    default:
        gst_message_unref(message);
        return GST_BUS_DROP;
    }
}

gchar *GStreamerMkvSegmenter::on_epoch_format_location_safe(GstElement * /*splitmux*/, guint fragment_id,
                                                            GstSample * /*first_sample*/, gpointer user_data)
{

    // Check if user_data is valid
    if (!user_data)
    {
        HAILO_ANALYTICS_LOG_ERROR("ERROR: user_data is NULL");
        return g_strdup_printf("error_%u_%lu.mkv", fragment_id, (uint64_t)time(nullptr));
    }

    EpochNamingData *data = static_cast<EpochNamingData *>(user_data);

    // Thread-safe access
    std::lock_guard<std::mutex> lock(data->data_mutex);

    // Check if data is still valid
    if (!data->is_valid)
    {
        HAILO_ANALYTICS_LOG_ERROR("ERROR: naming data is no longer valid");
        return g_strdup_printf("invalid_%u_%lu.mkv", fragment_id, (uint64_t)time(nullptr));
    }

    // Get current epoch timestamp, we can either use up to seconds or milliseconds
    uint64_t epoch_timestamp = (uint64_t)time(nullptr);
    // uint64_t epoch_timestamp =
    // std::chrono::duration_cast<std::chrono::milliseconds>(system_clock::now().time_since_epoch()).count();

    // Generate filename using COPIED strings (safe)
    gchar *filename =
        g_strdup_printf("%s/%s_%lu.mkv", data->output_path.c_str(), data->file_prefix.c_str(), epoch_timestamp);

    // Increment counter (thread-safe)
    data->segment_counter.fetch_add(1);

    return filename;
}

// Helper method to extract segment index from filename
uint32_t GStreamerMkvSegmenter::extract_segment_index(const char *filename)
{
    if (!filename)
        return 0;

    std::string filename_str(filename);
    size_t pos = filename_str.find_last_of("_");
    if (pos != std::string::npos)
    {
        size_t dot_pos = filename_str.find_last_of(".");
        if (dot_pos != std::string::npos && dot_pos > pos)
        {
            std::string index_str = filename_str.substr(pos + 1, dot_pos - pos - 1);
            return static_cast<uint32_t>(std::stoul(index_str));
        }
    }
    return 0;
}

void GStreamerMkvSegmenter::handle_split_mux_segment_with_running_time(const char *filename, uint64_t end_running_time)
{

    // Extract segment index
    uint32_t segment_index = extract_segment_index(filename);

    // Find the start running time for this segment
    uint64_t start_running_time = 0;
    auto it = m_segment_start_times.find(segment_index);
    if (it != m_segment_start_times.end())
    {
        start_running_time = it->second;
    }
    else
    {
        // Fallback: use the last segment's end time
        start_running_time = m_last_segment_end_running_time;
        HAILO_ANALYTICS_LOG_INFO("WARNING: Using fallback start time: {} ns ({:.3f} sec)", start_running_time,
                                 start_running_time / (double)GST_SECOND);
    }

    // Calculate ACTUAL duration from running times
    uint64_t duration_ns = (end_running_time > start_running_time) ? (end_running_time - start_running_time) : 0;

    uint32_t duration_ms = static_cast<uint32_t>(duration_ns / 1000000);

    // Use PTS-based epoch recorded at segment open (immune to pipeline latency accumulation)
    uint64_t start_time_epoch_ms;
    uint64_t now_epoch_ms = get_current_epoch_time_ms();
    auto epoch_it = m_segment_start_epoch_times.find(segment_index);
    if (epoch_it != m_segment_start_epoch_times.end())
    {
        start_time_epoch_ms = epoch_it->second;
        m_segment_start_epoch_times.erase(epoch_it);
    }
    else if (m_pts_epoch_offset_initialized)
    {
        // Fallback: compute from running-time + calibrated offset
        start_time_epoch_ms =
            static_cast<uint64_t>(static_cast<int64_t>(start_running_time / 1000000) + m_pts_to_epoch_offset_ms);
        HAILO_ANALYTICS_LOG_WARN("WARNING: No cached epoch for segment {}, computed from running-time", segment_index);
    }
    else
    {
        // Last resort: backwards calculation (legacy behavior)
        start_time_epoch_ms = now_epoch_ms - duration_ms;
        HAILO_ANALYTICS_LOG_WARN("WARNING: No epoch calibration for segment {}, using legacy fallback", segment_index);
    }

    // Also compute end_epoch from PTS (not start + gstreamer_duration)
    uint64_t end_time_epoch_ms;
    if (m_pts_epoch_offset_initialized)
    {
        end_time_epoch_ms =
            static_cast<uint64_t>(static_cast<int64_t>(end_running_time / 1000000) + m_pts_to_epoch_offset_ms);
    }
    else
    {
        end_time_epoch_ms = start_time_epoch_ms + duration_ms;
    }

    int64_t pipeline_latency_ms = static_cast<int64_t>(now_epoch_ms) - static_cast<int64_t>(end_time_epoch_ms);
    HAILO_ANALYTICS_LOG_INFO("SEGMENT_CLOSE idx={} file={} start_epoch={} end_epoch={} duration_ms={} "
                             "pipeline_latency={}ms",
                             segment_index, filename, start_time_epoch_ms, end_time_epoch_ms, duration_ms,
                             pipeline_latency_ms);

    // Create segment info
    SegmentInfo info;
    info.filename = std::string(filename);
    info.index = segment_index;
    info.completed = true;
    info.start_time_epoch_ms = start_time_epoch_ms;

    // **CALL NOTIFICATION CALLBACK WITH ACCURATE DURATION**
    if (m_notification_callback)
    {

        /*
        HAILO_ANALYTICS_LOG_INFO("*** CALLING NOTIFICATION CALLBACK OF CLOSING FILE WITH ACCURATE DURATION ***");
        HAILO_ANALYTICS_LOG_INFO("  - Filename: {}", info.filename);
        HAILO_ANALYTICS_LOG_INFO("  - ACCURATE Duration: {} ms", duration_ms);
        HAILO_ANALYTICS_LOG_INFO("  - Start time: {} ms", info.start_time_epoch_ms);
        HAILO_ANALYTICS_LOG_INFO("  - Index: {}", info.index);
        */

        m_notification_callback(info.filename.c_str(),
                                duration_ms, // ACCURATE duration from running-time calculation
                                info.start_time_epoch_ms, info.index, m_callback_user_data);
    }
    else
    {
        HAILO_ANALYTICS_LOG_INFO("WARNING: m_notification_callback is NULL");
    }

    // Clean up old start times
    if (m_segment_start_times.size() > 10)
    {
        auto oldest = m_segment_start_times.begin();
        m_segment_start_times.erase(oldest);
    }
    if (m_segment_start_epoch_times.size() > 10)
    {
        auto oldest = m_segment_start_epoch_times.begin();
        m_segment_start_epoch_times.erase(oldest);
    }
}
