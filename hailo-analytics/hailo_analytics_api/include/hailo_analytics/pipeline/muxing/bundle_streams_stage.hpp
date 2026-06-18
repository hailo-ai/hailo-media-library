#pragma once

/**
 * @file bundle_streams_stage.hpp
 * @brief Stage that bundles N parallel streams onto a single carrier buffer.
 **/

#include <stddef.h>
#include <chrono>
#include <memory>
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
 * Collection is non-blocking across queues so an empty queue can't stall the single frontend
 * producer thread; the frontend delivers streams in lockstep, so each cycle assembles one full set.
 */
class BundleStreamsStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    BundleStreamsStage(std::string name, std::string carrier_stream_id, std::vector<std::string> passenger_stream_ids,
                       size_t queue_size, bool leaky, bool trace_processing_operations = true);

    /**
     * @brief Validates that the publisher belongs to the declared roster (no-op otherwise).
     *
     * Queues are pre-created in the constructor — this override exists only to fail-fast on a
     * misconfigured `connect_frontend` or `connect` that targets this stage with an unknown
     * stream id.
     */
    void add_queue(std::string publisher_name) override;

    /**
     * @brief Per-cycle: collect a full frame set, attach passengers to the carrier, and forward it.
     */
    void loop() override;

  private:
    /**
     * @brief Collect one buffer from the carrier and every passenger queue without blocking on any
     * single queue. Returns false (carrier/passengers must then be ignored) if the stage
     * stopped/flushed or no buffer arrived on a pending queue within @p timeout.
     */
    bool collect_frame_set(BufferPtr &carrier, std::vector<BufferPtr> &passengers, std::chrono::seconds timeout);

    std::string m_carrier_stream_id;
    std::vector<std::string> m_passenger_stream_ids;
    // Direct pointers into m_queues, populated in the ctor and reused on every loop cycle so
    // we don't linear-search m_queues per frame per stream.
    QueuePtr m_carrier_queue;
    std::vector<QueuePtr> m_passenger_queues; // in roster order, parallel to m_passenger_stream_ids
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
