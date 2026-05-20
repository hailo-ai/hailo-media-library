#pragma once

#include <gst/gst.h>
#include <glib.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

/**
 * @brief Concatenates multiple MKV segment files into a single MKV byte stream.
 *
 * Uses a GStreamer pipeline with the `concat` element to demux each segment,
 * sequence the raw video, and remux into a single streamable MKV container.
 * Output is pulled from an appsink — designed for use with HTTP chunked
 * transfer encoding (no temp files).
 *
 * Pipeline structure:
 *   filesrc_0 ! matroskademux_0 --(pad-added)--> queue_0 ! concat.sink_0
 *   filesrc_1 ! matroskademux_1 --(pad-added)--> queue_1 ! concat.sink_1
 *   ...
 *   concat ! h264parse/h265parse ! matroskamux streamable=true ! appsink
 */
class MkvConcatenator
{
  public:
    MkvConcatenator();
    ~MkvConcatenator();

    MkvConcatenator(const MkvConcatenator &) = delete;
    MkvConcatenator &operator=(const MkvConcatenator &) = delete;

    /**
     * @brief Build and start the concatenation pipeline.
     * @param file_paths Ordered list of MKV file paths to concatenate.
     * @return true if pipeline started successfully.
     */
    bool start(const std::vector<std::string> &file_paths);

    /**
     * @brief Pull the next chunk of muxed MKV data from the appsink.
     *
     * Blocks until data is available or the pipeline reaches EOS.
     * Returns an empty vector when concatenation is complete.
     *
     * @param timeout_ms Maximum time to wait for data, in milliseconds.
     * @return MKV byte chunk, or empty vector on EOS/error.
     */
    std::vector<uint8_t> pull_chunk(int64_t timeout_ms = 1000);

    /**
     * @brief Check whether the pipeline has finished producing data.
     */
    bool is_done() const;

    /**
     * @brief Tear down the pipeline and release all GStreamer resources.
     */
    void stop();

  private:
    struct DemuxContext
    {
        GstElement *queue;
        GstPad *concat_sink_pad;
    };

    static void on_demux_pad_added(GstElement *demux, GstPad *new_pad, gpointer user_data);
    void cleanup();

    GstElement *m_pipeline;
    GstElement *m_appsink;
    std::vector<DemuxContext> m_demux_contexts;

    std::atomic<bool> m_is_done;
    std::atomic<bool> m_started;
    mutable std::mutex m_mutex;
};
