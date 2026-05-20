#pragma once

#include <gst/gst.h>
#include <glib.h>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <atomic>
#include <vector>

enum class CodecType
{
    H264,
    H265
};

struct FrameData
{
    std::vector<uint8_t> data;
    uint64_t pts; // in nanoseconds
    uint64_t dts; // in nanoseconds (will be calculated if needed)
    bool is_keyframe;

    FrameData(const uint8_t *nal_data, size_t size, uint64_t timestamp);
};

// Callback function type for segment notifications
typedef void (*SegmentNotificationCallback)(const char *filename,         // Full path to completed file
                                            uint32_t duration_ms,         // Duration in milliseconds
                                            uint64_t start_time_epoch_ms, // Start time in epoch milliseconds
                                            uint32_t segment_index,       // Segment number
                                            void *user_data               // User data pointer
);

struct SegmentInfo
{
    std::string filename;
    uint64_t start_pts;
    uint64_t end_pts;
    uint64_t start_time_epoch_ms;
    uint32_t index;
    bool completed;

    SegmentInfo();
};

struct EpochNamingData
{
    std::string output_path;
    std::string file_prefix;
    std::atomic<uint32_t> segment_counter;
    std::mutex data_mutex;
    bool is_valid;

    EpochNamingData(const std::string &path, const std::string &prefix);
};

class GStreamerMkvSegmenter
{
  public:
    GStreamerMkvSegmenter(CodecType codec, const std::string &output_path, const std::string &file_prefix,
                          uint32_t segment_duration_sec);
    ~GStreamerMkvSegmenter();

    bool initialize();
    bool start();
    bool stop();
    void cleanup();

    // Feed raw NAL unit with PTS (in nanoseconds)
    bool feed_frame(const uint8_t *nal_data, size_t size, uint64_t pts_ns);

    // Set callback for segment notifications
    void set_segment_notification_callback(SegmentNotificationCallback callback, void *user_data);

  private:
    // GStreamer pipeline setup
    bool create_pipeline();
    void destroy_pipeline();

    // Frame processing
    void process_frame_queue();
    bool is_keyframe(const uint8_t *nal_data, size_t size) const;
    void reorder_frames(std::queue<FrameData> &frames);

    // GStreamer callbacks
    static GstBusSyncReply on_bus_message(GstBus *bus, GstMessage *message, gpointer user_data);
    static gchar *on_epoch_format_location_safe(GstElement *splitmux, guint fragment_id, GstSample *first_sample,
                                                gpointer user_data);

    // Utility functions
    std::string generate_location_pattern() const;
    uint64_t get_current_epoch_time_ms() const;
    uint32_t extract_segment_index(const char *filename);
    void handle_split_mux_segment_with_running_time(const char *filename, uint64_t end_running_time);

    // Configuration
    CodecType m_codec_type;
    std::string m_output_path;
    std::string m_file_prefix;
    uint32_t m_segment_duration_sec;
    EpochNamingData *m_epoch_naming_data;

    // Notification callback
    SegmentNotificationCallback m_notification_callback;
    void *m_callback_user_data;

    // GStreamer elements
    GstElement *m_pipeline;
    GstElement *m_appsrc;
    GstElement *m_parser; // h264parse or h265parse
    GstBus *m_bus;

    // Segment management
    uint64_t m_last_segment_end_running_time;                 // Running time when last segment ended
    uint64_t m_current_segment_start_running_time;            // Running time when current segment started
    std::map<uint32_t, uint64_t> m_segment_start_times;       // Map to store segment running time information
    std::map<uint32_t, uint64_t> m_segment_start_epoch_times; // Map to store wall-clock epoch ms at segment open

    // PTS-to-epoch calibration (calculated once at pipeline start when latency ≈ 0)
    int64_t m_pts_to_epoch_offset_ms; // epoch_ms - (running_time_ns / 1,000,000) at first segment
    bool m_pts_epoch_offset_initialized;

    // Frame processing
    std::queue<FrameData> m_frame_queue;
    std::mutex m_queue_mutex;
    std::condition_variable m_queue_cv;
    std::thread m_processing_thread;
    bool m_processing_active;

    // Statistics
    bool m_initialized;
    bool m_running;
};
