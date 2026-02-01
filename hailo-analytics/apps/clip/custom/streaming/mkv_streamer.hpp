#pragma once

#include "gst/gstsample.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

// Video file information structure
struct VideoFile
{
    std::string file_path;
    int64_t start_timestamp_ms; // Epoch timestamp in milliseconds
    int64_t duration_ms;        // Video duration in milliseconds

    VideoFile(const std::string &path, int64_t start_ts, int64_t duration)
        : file_path(path), start_timestamp_ms(start_ts), duration_ms(duration)
    {
    }
};

// RTP packet data structure
struct RtpPacketData
{
    enum class CodecType
    {
        UNKNOWN,
        H264,
        H265
    };

    GstSample *sample;             // GStreamer sample containing the RTP packet
    uint8_t *data;                 // RTP packet data
    size_t size;                   // Packet size in bytes
    uint32_t timestamp;            // RTP timestamp
    uint16_t sequence_number;      // RTP sequence number
    uint32_t ssrc;                 // RTP SSRC (will be set by caller)
    bool marker_bit;               // RTP marker bit
    uint8_t payload_type;          // RTP payload type
    int64_t original_timestamp_ms; // Original timestamp from VideoFile
    CodecType codec_type;          // Codec type
    bool is_keyframe;              // Whether this packet contains keyframe data

    RtpPacketData()
        : data(nullptr), size(0), timestamp(0), sequence_number(0), ssrc(0), marker_bit(false), payload_type(0),
          original_timestamp_ms(0), codec_type(CodecType::UNKNOWN), is_keyframe(false)
    {
    }
};

// Error information structure
struct ErrorInfo
{
    enum class Type
    {
        FILE_NOT_FOUND,
        DECODE_ERROR,
        PIPELINE_ERROR,
        BUFFER_ERROR,
        CODEC_NOT_SUPPORTED,
        UNKNOWN_ERROR
    };

    Type type;
    std::string message;
    std::string file_path; // File that caused the error (if applicable)

    ErrorInfo(Type t, const std::string &msg, const std::string &path = "") : type(t), message(msg), file_path(path)
    {
    }
};

// Callback function types
using RtpPacketCallback = std::function<void(const RtpPacketData &packet)>;
using EndOfStreamCallback = std::function<void()>;
using ErrorCallback = std::function<void(const ErrorInfo &error)>;

class MKVStreamer
{
  public:
    MKVStreamer();
    ~MKVStreamer();

    // Set callback functions
    void set_rtp_packet_callback(RtpPacketCallback callback);
    void set_end_of_stream_callback(EndOfStreamCallback callback);
    void set_error_callback(ErrorCallback callback);

    // Main API functions
    bool start_streaming(const std::vector<VideoFile> &video_files);
    void stop_streaming();

    // Status check
    bool is_streaming() const;

    // Get current streaming info
    int64_t get_total_duration_ms() const;
    size_t get_current_file_index() const;

    // Buffer control (optional exposure)
    void set_buffer_size(size_t max_buffered_files);
    size_t get_buffer_size() const;

    // RTP configuration
    void set_target_frame_rate(double fps);
    void set_rtp_payload_types(uint8_t h264_pt, uint8_t h265_pt);

  private:
    struct FileContext
    {
        VideoFile video_file;
        GstElement *pipeline;
        GstElement *appsink;
        GstElement *payloader; // RTP payloader element
        bool is_prepared;
        bool is_eos;
        int64_t current_position_ms;
        RtpPacketData::CodecType detected_codec;
        uint32_t base_rtp_timestamp; // Base timestamp for this file
        guint bus_watch_id;

        FileContext(const VideoFile &vf)
            : video_file(vf), pipeline(nullptr), appsink(nullptr), payloader(nullptr), is_prepared(false),
              is_eos(false), current_position_ms(0), detected_codec(RtpPacketData::CodecType::UNKNOWN),
              base_rtp_timestamp(0), bus_watch_id(0)
        {
        }

        ~FileContext()
        {
            cleanup();
        }

        void cleanup();
    };

    // Core functionality
    void streaming_thread();
    void buffer_management_thread();
    void process_current_file();
    void perform_shutdown();

    // GStreamer pipeline management
    std::unique_ptr<FileContext> create_file_context(const VideoFile &video_file);
    std::string create_pipeline_string(const std::string &file_path, RtpPacketData::CodecType &detected_codec);
    RtpPacketData::CodecType detect_codec_type(const std::string &file_path);
    static gboolean bus_callback(GstBus *bus, GstMessage *msg, gpointer user_data);
    void handle_bus_message(GstMessage *msg, FileContext *context);

    // RTP packet processing
    bool extract_rtp_packet(FileContext *context);
    void process_gst_sample(GstSample *sample, FileContext *context);

    // Thread management
    std::thread m_streaming_thread;
    std::thread m_buffer_thread;
    std::atomic<bool> m_should_stop;
    std::atomic<bool> m_is_streaming;
    std::atomic<bool> m_shutdown_in_progress{false};

    // Synchronization
    mutable std::mutex m_state_mutex;
    std::condition_variable m_buffer_cv;

    // Video files and current state
    std::vector<VideoFile> m_video_files;
    std::atomic<size_t> m_current_file_index;
    std::atomic<int64_t> m_total_duration_ms;

    // Buffering
    std::queue<std::unique_ptr<FileContext>> m_prepared_contexts;
    std::unique_ptr<FileContext> m_current_context;
    std::atomic<size_t> m_max_buffer_size;
    mutable std::mutex m_buffer_mutex;

    // RTP state management
    std::atomic<uint16_t> m_rtp_sequence_number;
    std::atomic<uint32_t> m_global_rtp_timestamp;
    int64_t m_stream_start_time_ns; // Stream start time for timestamp calculation
    double m_target_fps;
    uint8_t m_h264_payload_type;
    uint8_t m_h265_payload_type;

    // Callbacks
    RtpPacketCallback m_rtp_callback;
    EndOfStreamCallback m_eos_callback;
    ErrorCallback m_error_callback;
    mutable std::mutex m_callback_mutex;

    // Constants
    static constexpr size_t DEFAULT_BUFFER_SIZE = 2;
    static constexpr int64_t FRAME_TIMEOUT_MS = 100;
    static constexpr double DEFAULT_TARGET_FPS = 30.0;
    static constexpr uint8_t DEFAULT_H264_PT = 96;
    static constexpr uint8_t DEFAULT_H265_PT = 97;
    static constexpr uint32_t RTP_CLOCK_RATE = 90000; // 90kHz for video
};
