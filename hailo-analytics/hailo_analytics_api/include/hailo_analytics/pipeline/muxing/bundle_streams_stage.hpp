#pragma once

/**
 * @file bundle_streams_stage.hpp
 * @brief Stage that bundles N parallel streams onto a single carrier buffer.
 **/

#include <stddef.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"

namespace hailo_analytics::pipeline::muxing
{

/**
 * @brief Bundles N parallel streams onto a single carrier buffer: collects one buffer per stream,
 * attaches the passengers to the carrier as AttachedStreamMetadata, and forwards the carrier.
 * Waits on a shared condition_variable that every input queue notifies on push/flush.
 */
class BundleStreamsStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    /// How long collect_frame_set() waits for a complete set before logging a stall and waiting again.
    static constexpr std::chrono::seconds COLLECT_TIMEOUT{5};

    BundleStreamsStage(std::string name, std::string carrier_stream_id, std::vector<std::string> passenger_stream_ids,
                       size_t queue_size, bool leaky, bool trace_processing_operations = true);
    ~BundleStreamsStage() override;

    /**
     * @brief Validates that the publisher belongs to the declared roster (no-op otherwise).
     *
     * Queues are pre-created in the constructor — this override exists only to fail-fast on a
     * misconfigured `connect_frontend` or `connect` that targets this stage with an unknown
     * stream id.
     */
    void add_queue(std::string publisher_name) override;

    /**
     * @brief Queues @p data, or drops it if its frame is missing one of the bundled streams.
     */
    void push(BufferPtr data, std::string publisher_name) override;

    /**
     * @brief Per-cycle: collect a full frame set, attach passengers to the carrier, and forward it.
     */
    void loop() override;

  private:
    enum class CollectStatus
    {
        FRAME_SET_READY,
        TIMED_OUT,
        END_OF_STREAM,
    };

    /**
     * @brief Wait until every queue holds a buffer, then pop one buffer from each. carrier/passengers
     * are valid only on FRAME_SET_READY; TIMED_OUT means no complete set arrived within @p timeout.
     */
    CollectStatus collect_frame_set(BufferPtr &carrier, std::vector<BufferPtr> &passengers,
                                    std::chrono::seconds timeout);

    /// True when the carrier and every passenger queue holds at least one buffer (a full set).
    bool all_inputs_ready() const;

    /// True if @p data's frame also produced every other bundled stream.
    bool lists_all_other_streams(const BufferPtr &data, const std::string &publisher_name) const;

    /// Wakes collect_frame_set(); registered as the notify callback of every input queue.
    void notify_input_ready();

    void log_missing_streams(std::chrono::seconds timeout) const;

    /// Logs the buffers discarded since the last report, at most once per COLLECT_TIMEOUT.
    void report_unmatched_drops();

    std::mutex m_ready_mutex;
    std::condition_variable m_ready_cv;

    std::string m_carrier_stream_id;
    std::vector<std::string> m_passenger_stream_ids;
    // Direct pointers into m_queues, populated in the ctor and reused on every loop cycle so
    // we don't linear-search m_queues per frame per stream.
    QueuePtr m_carrier_queue;
    std::vector<QueuePtr> m_passenger_queues; // in roster order, parallel to m_passenger_stream_ids

    // Set once a buffer lists every other bundled stream, i.e. all streams are cut from the same ISP frames.
    std::atomic<bool> m_streams_share_frames{false};
    std::atomic<size_t> m_unmatched_drops{0};
    std::chrono::steady_clock::time_point m_last_drop_report{}; // bundle thread only
};

class BundleStreamsStageBuild : public BundleStreamsStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_carrier_stream_id;
        std::vector<std::string> m_passenger_stream_ids;
        size_t m_queue_size = 1;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_carrier_stream_id(std::string id);
        Builder &set_passenger_stream_ids(std::vector<std::string> ids);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Build the stage. Throws std::invalid_argument if stage_name, carrier_stream_id,
         * or passenger_stream_ids (non-empty) were not configured.
         */
        std::shared_ptr<BundleStreamsStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::muxing
