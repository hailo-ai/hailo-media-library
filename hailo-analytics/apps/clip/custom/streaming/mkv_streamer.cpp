#include "mkv_streamer.hpp"
#include <iostream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <gst/video/video.h>
#include <gst/rtp/rtp.h>
#include "common_utils.hpp"
#include <malloc.h>

// Helper structure to pass data to and from the GStreamer callback.
struct CodecDetectData
{
    GMainLoop *loop = nullptr;
    RtpPacketData::CodecType codec = RtpPacketData::CodecType::UNKNOWN;
    std::atomic<bool> quit_flag{false};
};

MKVStreamer::MKVStreamer()
    : m_should_stop(false), m_is_streaming(false), m_current_file_index(0), m_total_duration_ms(0),
      m_max_buffer_size(DEFAULT_BUFFER_SIZE), m_rtp_sequence_number(1) // Start from 1
      ,
      m_global_rtp_timestamp(0), m_stream_start_time_ns(0), m_target_fps(DEFAULT_TARGET_FPS),
      m_h264_payload_type(DEFAULT_H264_PT), m_h265_payload_type(DEFAULT_H265_PT)
{
    // Initialize GStreamer
    gst_init(nullptr, nullptr);
}

MKVStreamer::~MKVStreamer()
{
    stop_streaming();
}

void MKVStreamer::set_rtp_packet_callback(RtpPacketCallback callback)
{
    std::lock_guard<std::mutex> lock(m_callback_mutex);
    m_rtp_callback = callback;
}

void MKVStreamer::set_end_of_stream_callback(EndOfStreamCallback callback)
{
    std::lock_guard<std::mutex> lock(m_callback_mutex);
    m_eos_callback = callback;
}

void MKVStreamer::set_error_callback(ErrorCallback callback)
{
    std::lock_guard<std::mutex> lock(m_callback_mutex);
    m_error_callback = callback;
}

void MKVStreamer::set_target_frame_rate(double fps)
{
    m_target_fps = fps;
}

void MKVStreamer::set_rtp_payload_types(uint8_t h264_pt, uint8_t h265_pt)
{
    m_h264_payload_type = h264_pt;
    m_h265_payload_type = h265_pt;
}

bool MKVStreamer::start_streaming(const std::vector<VideoFile> &video_files)
{
    if (m_is_streaming)
    {
        return false;
    }

    if (video_files.empty())
    {
        if (m_error_callback)
        {
            m_error_callback(ErrorInfo(ErrorInfo::Type::UNKNOWN_ERROR, "No video files provided"));
        }
        return false;
    }

    // Validate files exist
    for (const auto &file : video_files)
    {
        if (!std::filesystem::exists(file.file_path))
        {
            if (m_error_callback)
            {
                m_error_callback(
                    ErrorInfo(ErrorInfo::Type::FILE_NOT_FOUND, "File not found: " + file.file_path, file.file_path));
            }
            return false;
        }
    }

    // Reset state
    m_video_files = video_files;
    m_current_file_index = 0;
    m_should_stop = false;

    // Initialize RTP state
    m_rtp_sequence_number = 1;
    m_global_rtp_timestamp = 0;
    m_stream_start_time_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();

    // Calculate total duration
    int64_t total_duration = 0;
    for (const auto &file : video_files)
    {
        total_duration += file.duration_ms;
    }
    m_total_duration_ms = total_duration;

    // Clear any existing prepared contexts
    {
        std::lock_guard<std::mutex> lock(m_buffer_mutex);
        while (!m_prepared_contexts.empty())
        {
            m_prepared_contexts.pop();
        }
        m_current_context.reset();
    }

    m_is_streaming = true;

    // Start threads
    m_buffer_thread = std::thread(&MKVStreamer::buffer_management_thread, this);
    m_streaming_thread = std::thread(&MKVStreamer::streaming_thread, this);

    return true;
}

// New private helper function to handle the actual shutdown and cleanup.
// This is safe to call from any thread, including the ones it manages.
void MKVStreamer::perform_shutdown()
{

    if (m_shutdown_in_progress.exchange(true))
    {
        return; // Already shutting down
    }

    // Get the ID of the current thread to prevent self-join deadlocks.
    const std::thread::id current_thread_id = std::this_thread::get_id();

    // Safely join the streaming thread.
    if (m_streaming_thread.joinable())
    {
        if (m_streaming_thread.get_id() == current_thread_id)
        {
            // Self-join attempt: let the thread terminate on its own.
            m_streaming_thread.detach();
        }
        else
        {
            m_streaming_thread.join();
        }
    }

    // Safely join the buffer management thread.
    if (m_buffer_thread.joinable())
    {
        if (m_buffer_thread.get_id() == current_thread_id)
        {
            // Self-join attempt on the buffer thread.
            m_buffer_thread.detach();
        }
        else
        {
            m_buffer_thread.join();
        }
    }

    // Clean up GStreamer contexts and reset state.
    // This is now safe because both threads have been handled.
    {
        std::lock_guard<std::mutex> lock(m_buffer_mutex);
        while (!m_prepared_contexts.empty())
        {
            m_prepared_contexts.front()->cleanup();
            m_prepared_contexts.pop();
        }
        if (m_current_context)
        {
            m_current_context->cleanup();
            m_current_context.reset();
        }
    }

    m_is_streaming = false;
    m_shutdown_in_progress = false;
}

void MKVStreamer::stop_streaming()
{
    if (!m_is_streaming)
    {
        return;
    }

    // 1. Signal all threads to stop their loops.
    m_should_stop = true;
    m_buffer_cv.notify_all();

    // 2. Perform the actual join and cleanup.
    perform_shutdown();
}

bool MKVStreamer::is_streaming() const
{
    return m_is_streaming;
}

int64_t MKVStreamer::get_total_duration_ms() const
{
    return m_total_duration_ms;
}

size_t MKVStreamer::get_current_file_index() const
{
    return m_current_file_index;
}

void MKVStreamer::set_buffer_size(size_t max_buffered_files)
{
    m_max_buffer_size = max_buffered_files;
}

size_t MKVStreamer::get_buffer_size() const
{
    return m_max_buffer_size;
}

void MKVStreamer::streaming_thread()
{

    while (!m_should_stop && m_current_file_index < m_video_files.size())
    {
        if (!m_current_context)
        {
            // Wait for first file to be prepared
            std::unique_lock<std::mutex> lock(m_buffer_mutex);
            m_buffer_cv.wait(lock, [this] { return !m_prepared_contexts.empty() || m_should_stop; });

            if (m_should_stop)
                break;

            if (!m_prepared_contexts.empty())
            {
                m_current_context = std::move(m_prepared_contexts.front());
                m_prepared_contexts.pop();

                // Set base RTP timestamp for this file
                m_current_context->base_rtp_timestamp = m_global_rtp_timestamp;

                lock.unlock();
                m_buffer_cv.notify_one(); // Notify buffer thread to prepare next file
            }
            else
            {
                continue;
            }
        }

        process_current_file();

        if (m_current_context && m_current_context->is_eos)
        {
            std::lock_guard<std::mutex> lock(m_buffer_mutex);
            m_current_file_index++;
            m_current_context->cleanup();
            m_current_context.reset();
        }
    }

    // Check if we exited the loop naturally (finished all files)
    // vs. being stopped externally.
    const bool is_natural_eos = !m_should_stop.load();

    if (is_natural_eos)
    {

        // We finished the stream naturally. Time to automatically clean up.
        // 1. Signal the buffer thread to stop.
        m_should_stop = true;
        m_buffer_cv.notify_all();

        // 2. Perform the shutdown. This will join the buffer_thread and
        //    detach this streaming_thread (since it's a self-call),
        //    and clean up all GStreamer resources.
        perform_shutdown();

        // 3. Now that everything is cleaned up, safely call the final callback.
        if (m_eos_callback)
        {
            m_eos_callback();
        }
    }
}

void MKVStreamer::buffer_management_thread()
{
    while (!m_should_stop)
    {
        std::unique_lock<std::mutex> lock(m_buffer_mutex);

        // Wait until we need to prepare more files
        m_buffer_cv.wait(lock, [this] {
            return (m_prepared_contexts.size() < m_max_buffer_size &&
                    (m_current_file_index + m_prepared_contexts.size() + (m_current_context ? 1 : 0)) <
                        m_video_files.size()) ||
                   m_should_stop;
        });

        if (m_should_stop)
            break;

        // Prepare next file
        size_t next_index = m_current_file_index + m_prepared_contexts.size() + (m_current_context ? 1 : 0);

        if (next_index < m_video_files.size())
        {
            lock.unlock();

            auto context = create_file_context(m_video_files[next_index]);
            if (context && context->is_prepared)
            {
                lock.lock();
                m_prepared_contexts.push(std::move(context));
                lock.unlock();
                m_buffer_cv.notify_one();
            }
            else if (m_error_callback)
            {
                m_error_callback(ErrorInfo(ErrorInfo::Type::PIPELINE_ERROR,
                                           "Failed to prepare file: " + m_video_files[next_index].file_path,
                                           m_video_files[next_index].file_path));
            }
        }
    }
}

void MKVStreamer::process_current_file()
{
    if (!m_current_context || !m_current_context->is_prepared)
    {
        return;
    }

    // Start pipeline if not already started
    GstState current_state;
    if (gst_element_get_state(m_current_context->pipeline, &current_state, nullptr, 0) != GST_STATE_CHANGE_SUCCESS ||
        current_state != GST_STATE_PLAYING)
    {
        GstStateChangeReturn ret = gst_element_set_state(m_current_context->pipeline, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE)
        {
            if (m_error_callback)
            {
                m_error_callback(ErrorInfo(ErrorInfo::Type::PIPELINE_ERROR,
                                           "Failed to start pipeline for: " + m_current_context->video_file.file_path,
                                           m_current_context->video_file.file_path));
            }
            m_current_context->is_eos = true;
            return;
        }
    }

    // Extract RTP packets
    while (!m_should_stop && !m_current_context->is_eos)
    {
        if (!extract_rtp_packet(m_current_context.get()))
        {
            // Small delay to prevent busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

bool MKVStreamer::extract_rtp_packet(FileContext *context)
{
    if (!context || !context->appsink)
    {
        return false;
    }

    GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(context->appsink), FRAME_TIMEOUT_MS * GST_MSECOND);
    if (sample)
    {
        process_gst_sample(sample, context);
        return true;
    }

    return false;
}

void MKVStreamer::process_gst_sample(GstSample *original_sample, FileContext *context)
{
    GstBuffer *original_buffer = gst_sample_get_buffer(original_sample);
    if (!original_buffer)
    {
        gst_sample_unref(original_sample);
        return;
    }

    // 1. Create a shallow copy of the buffer structure (metadata only, points to same memory) and make it writable.
    GstBuffer *shallow_copy = gst_buffer_copy(original_buffer);
    if (!shallow_copy)
    {
        gst_sample_unref(original_sample); // Clean up the original sample
        return;
    }
    GstBuffer *writable_buffer = gst_buffer_make_writable(shallow_copy);

    // 2. We have our clean buffer. Now, modify its RTP header.
    GstMapInfo map_info;
    if (!gst_buffer_map(writable_buffer, &map_info, GST_MAP_READWRITE))
    {
        gst_buffer_unref(writable_buffer); // We own the copy, we must free it
        gst_sample_unref(original_sample);
        return;
    }

    if (map_info.size >= 12)
    {
        uint8_t *rtp_header = map_info.data;

        // Overwrite the RTP Header
        uint32_t current_file_ts = g_ntohl(*((uint32_t *)(rtp_header + 4)));
        uint32_t global_ts = context->base_rtp_timestamp + current_file_ts;
        uint16_t global_seq_num = m_rtp_sequence_number.fetch_add(1);

        *((uint16_t *)(rtp_header + 2)) = g_htons(global_seq_num);
        *((uint32_t *)(rtp_header + 4)) = g_htonl(global_ts);
        // Note: The SSRC is not modified here, it will be set by the caller (WebRtc Streamer).
        //*((uint32_t*)(rtp_header + 8)) = g_htonl(3185615957);

        m_global_rtp_timestamp = global_ts;
    }

    // We are done writing, so unmap the buffer.
    gst_buffer_unmap(writable_buffer, &map_info);

    // 3. Create a brand new GstSample that wraps our modified buffer.
    //    We can reuse the caps from the original sample.
    GstCaps *caps = gst_sample_get_caps(original_sample);
    GstSample *new_sample = gst_sample_new(writable_buffer, caps, NULL, NULL);

    // 4. Clean up.
    // The new_sample now owns the writable_buffer, so we can unref our handle to it.
    gst_buffer_unref(writable_buffer);
    // We are completely done with the original sample, so unref it.
    // This is the CRUCIAL step that prevents the "has parents" warning.
    gst_sample_unref(original_sample);

    // 5. Populate RtpPacketData with the new, fully independent sample.
    RtpPacketData packet_data;
    packet_data.sample = new_sample; // Pass the new sample to the consumer.
    packet_data.codec_type = context->detected_codec;

    // 6. Call the callback.
    // The consumer of the callback now owns `new_sample` and is responsible
    // for calling gst_sample_unref() on it when it's done.
    {
        std::lock_guard<std::mutex> lock(m_callback_mutex);
        if (m_rtp_callback)
        {
            m_rtp_callback(packet_data);
        }

        gst_sample_unref(new_sample);
    }
}

RtpPacketData::CodecType MKVStreamer::detect_codec_type(const std::string &file_path)
{
    RtpPacketData::CodecType codec_type = RtpPacketData::CodecType::UNKNOWN;

    auto codec_result = CodecUtils::detect_codec_type(file_path);

    switch (codec_result)
    {
    case CodecUtils::CodecType::H264:
        std::cout << "Video codec: H264" << std::endl;
        codec_type = RtpPacketData::CodecType::H264;
        break;
    case CodecUtils::CodecType::H265:
        std::cout << "Video codec: H265" << std::endl;
        codec_type = RtpPacketData::CodecType::H265;
        break;
    case CodecUtils::CodecType::UNKNOWN:
        std::cout << "Video codec: Unknown or not H264/H265" << std::endl;
        break;
    }

    return codec_type;
}

std::unique_ptr<MKVStreamer::FileContext> MKVStreamer::create_file_context(const VideoFile &video_file)
{
    auto context = std::make_unique<FileContext>(video_file);

    // Detect codec type
    context->detected_codec = detect_codec_type(video_file.file_path);
    if (context->detected_codec == RtpPacketData::CodecType::UNKNOWN)
    {
        if (m_error_callback)
        {
            m_error_callback(ErrorInfo(ErrorInfo::Type::CODEC_NOT_SUPPORTED,
                                       "Unsupported or undetectable codec in: " + video_file.file_path,
                                       video_file.file_path));
        }
        return nullptr;
    }

    // Create pipeline
    std::string pipeline_str = create_pipeline_string(video_file.file_path, context->detected_codec);
    GError *error = nullptr;
    context->pipeline = gst_parse_launch(pipeline_str.c_str(), &error);

    if (!context->pipeline || error)
    {
        if (error)
        {
            std::cerr << "Pipeline creation error: " << error->message << std::endl;
            g_error_free(error);
        }
        return nullptr;
    }

    // Get elements
    context->appsink = gst_bin_get_by_name(GST_BIN(context->pipeline), "appsink");
    context->payloader = gst_bin_get_by_name(GST_BIN(context->pipeline), "payloader");

    if (!context->appsink || !context->payloader)
    {
        std::cerr << "Failed to get required pipeline elements" << std::endl;
        return nullptr;
    }

    // Configure RTP payloader
    uint8_t payload_type =
        (context->detected_codec == RtpPacketData::CodecType::H264) ? m_h264_payload_type : m_h265_payload_type;
    g_object_set(context->payloader, "pt", payload_type, nullptr);

    // Set up bus callback
    GstBus *bus = gst_element_get_bus(context->pipeline);
    context->bus_watch_id = gst_bus_add_watch(bus, (GstBusFunc)bus_callback, context.get());
    gst_object_unref(bus);

    // Preroll pipeline to PAUSED state
    GstStateChangeReturn ret = gst_element_set_state(context->pipeline, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        std::cerr << "Failed to preroll pipeline for: " << video_file.file_path << std::endl;
        return nullptr;
    }

    // Wait for preroll to complete
    ret = gst_element_get_state(context->pipeline, nullptr, nullptr, 5 * GST_SECOND);
    if (ret == GST_STATE_CHANGE_SUCCESS || ret == GST_STATE_CHANGE_NO_PREROLL)
    {
        context->is_prepared = true;
    }
    else
    {
        std::cerr << "Pipeline preroll timed out for: " << video_file.file_path << std::endl;
        return nullptr;
    }

    return context;
}

std::string MKVStreamer::create_pipeline_string(const std::string &file_path, RtpPacketData::CodecType &detected_codec)
{
    std::ostringstream pipeline;

    pipeline << "filesrc location=\"" << file_path << "\" ! "
             << "matroskademux ! "
             << "queue max-size-buffers=0 max-size-bytes=0 max-size-time=1000000000 ! ";

    // Add parser and RTP payloader based on codec
    if (detected_codec == RtpPacketData::CodecType::H264)
    {
        pipeline << "h264parse config-interval=1 ! "
                 << "rtph264pay name=payloader config-interval=1 timestamp-offset=0 ! ";
    }
    else if (detected_codec == RtpPacketData::CodecType::H265)
    {
        pipeline << "h265parse config-interval=1 ! "
                 << "rtph265pay name=payloader config-interval=1 timestamp-offset=0 ! ";
    }

    pipeline << "queue max-size-buffers=0 max-size-bytes=0 max-size-time=1000000000 ! "
             << "appsink name=appsink emit-signals=false max-buffers=10 drop=false";

    std::string result = pipeline.str();
    std::cout << "Created pipeline: " << result << std::endl;
    return result;
}

gboolean MKVStreamer::bus_callback([[maybe_unused]] GstBus *bus, GstMessage *msg, gpointer user_data)
{
    FileContext *context = static_cast<FileContext *>(user_data);

    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_EOS:
        context->is_eos = true;
        break;

    case GST_MESSAGE_ERROR: {
        GError *error;
        gchar *debug;
        gst_message_parse_error(msg, &error, &debug);

        std::cerr << "Pipeline error: " << error->message << std::endl;
        if (debug)
        {
            std::cerr << "Debug info: " << debug << std::endl;
            g_free(debug);
        }

        context->is_eos = true; // Treat error as EOS to move to next file
        g_error_free(error);
        break;
    }

    case GST_MESSAGE_WARNING: {
        GError *warning;
        gchar *debug;
        gst_message_parse_warning(msg, &warning, &debug);

        std::cerr << "Pipeline warning: " << warning->message << std::endl;
        if (debug)
        {
            std::cerr << "Debug info: " << debug << std::endl;
            g_free(debug);
        }

        g_error_free(warning);
        break;
    }

    default:
        break;
    }

    return TRUE;
}

void MKVStreamer::FileContext::cleanup()
{
    // 1. Remove the bus watch first
    if (bus_watch_id > 0)
    {
        g_source_remove(bus_watch_id);
        bus_watch_id = 0;
    }

    if (pipeline)
    {
        // 2. Set the pipeline to the NULL state. This begins the process of releasing resources.
        gst_element_set_state(pipeline, GST_STATE_NULL);

        // 3. Wait for the state change to complete. This is a synchronous call
        //    that ensures all elements have shut down before we proceed.
        GstState state, pending;
        gst_element_get_state(pipeline, &state, &pending, GST_CLOCK_TIME_NONE);

        // 4. Unreference the child elements we explicitly got.
        if (appsink)
        {
            // Drain any pending samples from the appsink to prevent leaks.
            // This is crucial if the pipeline stops abruptly.
            GstSample *sample;
            while ((sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 0)) != NULL)
            {
                gst_sample_unref(sample);
            }
            gst_object_unref(appsink);
            appsink = nullptr;
        }

        if (payloader)
        {
            gst_object_unref(payloader);
            payloader = nullptr;
        }

        // 5. Finally, unreference the pipeline itself. Since the bus watch reference
        //    is gone and the state is NULL, this should be the final reference,
        //    triggering its complete destruction and the freeing of its memory pools.
        gst_object_unref(pipeline);
        pipeline = nullptr;

        // Force memory cleanup to reduce fragmentation, avoid caching large amount of memory and return it to the OS
        malloc_trim(0);
    }
}

void MKVStreamer::handle_bus_message(GstMessage *msg, FileContext *context)
{
    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_EOS:
        context->is_eos = true;
        break;

    case GST_MESSAGE_ERROR: {
        GError *error;
        gchar *debug;
        gst_message_parse_error(msg, &error, &debug);

        if (m_error_callback)
        {
            m_error_callback(
                ErrorInfo(ErrorInfo::Type::DECODE_ERROR, std::string(error->message), context->video_file.file_path));
        }

        context->is_eos = true;
        g_error_free(error);
        if (debug)
            g_free(debug);
        break;
    }

    default:
        break;
    }
}
