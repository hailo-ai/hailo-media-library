#pragma once

#include <cstdint>
#include <unordered_map>
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

// Forward-declare the generated protobuf type instead of including analytics_metadata.pb.h here:
// only the codecs library and tests that introspect the message need the full definition, and
// pulling the .pb.h into the public header would force every transitive consumer (e.g. the
// analytics layer that just uses the Builder) to discover the codecs build-dir include path.
namespace hailo_analytics
{
class Frame;
}

/**
 * @brief Default queue size for analytic metadata packager stage
 */
#define ANALYTIC_METADATA_QUEUE_SIZE_DEFAULT (20)

namespace hailo_analytics::pipeline::codecs
{

/**
 * @brief Per-tracking-id cache of the most recent landmarks payload, with age-based eviction.
 *
 * Lets the packager fill in landmarks on frames where the cropping stage skipped landmark inference
 * (e.g. when bbox_crop runs at half the input frame rate while the tracker emits predictions every
 * frame). The landmarks are stored as the original HailoLandmarksPtr so the saved points remain
 * normalized within their detection's bbox — when reused on a frame whose bbox has moved (Kalman
 * prediction), they translate naturally with the bbox in @ref populate_landmarks.
 */
class LandmarksCache
{
  public:
    /** @brief Record/refresh the landmarks for a tracking id. */
    void update(int tracking_id, HailoLandmarksPtr landmarks);

    /** @brief Look up the last-known landmarks for a tracking id; nullptr if absent. */
    HailoLandmarksPtr lookup(int tracking_id);

    /** @brief Evict entries not touched since the previous advance_frame() call. */
    void advance_frame();

  private:
    struct Entry
    {
        HailoLandmarksPtr landmarks;
        bool seen_this_frame = true;
    };

    std::unordered_map<int, Entry> m_entries;
};

/**
 * @brief Populate a hailo_analytics::Frame protobuf message from a buffer's analytics ROI.
 * @param data Input buffer carrying the HailoROI tree and frame metadata.
 * @param frame Out parameter populated in-place. Caller-owned.
 * @return true if the frame contains at least one object (and ISP/frame fields were set), false otherwise.
 *
 * Exposed at the namespace level (not just as a stage method) so unit tests can exercise the
 * packaging logic without spinning up the threaded stage. Callers must include analytics_metadata.pb.h
 * to construct or inspect the Frame argument.
 */
bool build_metadata_proto(BufferPtr data, hailo_analytics::Frame &frame);

/**
 * @brief Variant that uses a persistent @ref LandmarksCache to fill in landmarks on frames where
 *        a tracked detection has no fresh landmark inference of its own.
 *
 * Same behaviour as the no-cache overload otherwise. Caller owns the cache instance and is
 * responsible for keeping it alive across frames.
 */
bool build_metadata_proto(BufferPtr data, hailo_analytics::Frame &frame, LandmarksCache &cache);

/**
 * @brief Stage for packaging analytic metadata into a Protobuf binary payload for transmission
 *
 * This stage processes AI analytics data (detections, landmarks, classifications) from the
 * pipeline and packages them into a hailo_analytics.Frame Protobuf message. The serialized
 * bytes are attached to the buffer as a HailoZMQMessage so downstream sinks (WebSocket / ZMQ)
 * can ship them verbatim.
 *
 * Coordinates are transformed to native frame dimensions before serialization. Hierarchical
 * object structures (nested ROIs) are preserved through the Detection.detections / landmarks /
 * classifications repeated fields.
 */
class AnalyticMetadataPackagerStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    /**
     * @brief Constructor for AnalyticMetadataPackagerStage
     * @param name Stage name for identification
     * @param queue_size Size of the processing queue (default: ANALYTIC_METADATA_QUEUE_SIZE_DEFAULT)
     * @param leaky Whether the queue should drop old frames when full (default: false)
     * @param trace_processing_operations Enable tracing for processing operations (default: true)
     */
    AnalyticMetadataPackagerStage(std::string name, size_t queue_size = ANALYTIC_METADATA_QUEUE_SIZE_DEFAULT,
                                  bool leaky = false, bool trace_processing_operations = true);

    /**
     * @brief Process incoming data buffer and package analytic metadata into a Protobuf payload
     * @param data Input buffer containing AI analytics data
     * @return AppStatus indicating success or failure of processing
     */
    AppStatus process(BufferPtr data) override;

  private:
    // Persistent landmarks cache so detections from tracker-predicted frames (where the bbox crop
    // stage skipped landmarks inference) still emit landmarks based on the most recent inference
    // for that tracking id.
    LandmarksCache m_landmarks_cache;
};

/**
 * @brief Builder pattern implementation for AnalyticMetadataPackagerStage
 */
class AnalyticMetadataPackagerStageBuild : public AnalyticMetadataPackagerStage
{
  public:
    /**
     * @brief Builder class for constructing AnalyticMetadataPackagerStage instances
     */
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = ANALYTIC_METADATA_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        /**
         * @brief Set the stage name
         * @param name Name for the stage
         * @return Builder reference for chaining
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Set the queue size
         * @param size Size of the processing queue
         * @return Builder reference for chaining
         */
        Builder &set_queue_size_opt(size_t size);

        /**
         * @brief Set the leaky option
         * @param activate If true, queue drops old frames when full
         * @return Builder reference for chaining
         */
        Builder &set_leaky_opt(bool activate);

        /**
         * @brief Set the trace option
         * @param activate If true, enables tracing for processing operations
         * @return Builder reference for chaining
         */
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Build and return shared pointer to AnalyticMetadataPackagerStage
         * @return Shared pointer to constructed AnalyticMetadataPackagerStage
         * @throws std::runtime_error if required parameters are missing
         */
        std::shared_ptr<AnalyticMetadataPackagerStage> buildptr() const;
    };

    /**
     * @brief Create a new Builder instance
     * @return Builder instance for constructing AnalyticMetadataPackagerStage
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::codecs
