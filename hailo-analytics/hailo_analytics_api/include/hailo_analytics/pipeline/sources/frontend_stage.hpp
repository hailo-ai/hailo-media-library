#pragma once

/**
 * @file frontend_stage.hpp
 * @brief Stage that captures video frames from various sources using media library frontend.
 **/

// General includes
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

// Media-Library includes
#include "media_library/media_library.hpp"
#include "media_library/media_library_types.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"

#define FRONTEND_QUEUE_SIZE_DEFAULT (1)

namespace hailo_analytics::pipeline::sources
{

/**
 * @brief Class representing a frontend source stage in the pipeline.
 *
 * This stage captures video frames from various sources (camera, file, etc.) using
 * the media library. It can produce multiple output streams, each of which
 * can be subscribed to by different downstream stages.
 * Typically used as a SOURCE stage at the beginning of a pipeline.
 */
class FrontendStage : public hailo_analytics::pipeline::ThreadedStage
{
  protected:
    MediaLibraryInterfacePtr m_media_library; ///< Shared MediaLibrary interface instance.
    std::map<output_stream_id_t, std::vector<hailo_analytics::pipeline::StagePtr>>
        m_stream_subscribers; ///< Map of stream IDs to subscriber stages.

  private:
    std::mutex m_running_mutex;           ///< Mutex for synchronizing start/stop operations.
    std::condition_variable m_running_cv; ///< Condition variable for signaling start/stop.
    std::atomic<bool> m_started;          ///< Flag indicating if the stage has been started.

  public:
    /**
     * @brief Construct a new FrontendStage object.
     *
     * @param name Name of the stage.
     * @param queue_size Size of the processing queue.
     * @param leaky If true, drops oldest buffer when queue is full.
     * @param trace_processing_operations If true, enables performance tracing.
     */
    FrontendStage(std::string name, size_t queue_size = FRONTEND_QUEUE_SIZE_DEFAULT, bool leaky = false,
                  bool trace_processing_operations = true);

    /**
     * @brief Destructor for FrontendStage.
     */
    ~FrontendStage() override;

    /**
     * @brief Create the frontend stage with a MediaLibrary instance.
     *
     * @param media_library Shared pointer to the MediaLibrary.
     * @return AppStatus Status of the creation.
     */
    AppStatus create(MediaLibraryInterfacePtr media_library);

    /**
     * @brief Add a subscriber to this stage's output.
     *
     * Note: Subscription is done by stream ID as frontend has multiple output streams.
     *
     * @param subscriber Stage to subscribe to this stage's output.
     * @param stream_id Optional stream ID for multi-stream frontends.
     */
    void add_subscriber(StagePtr subscriber, std::optional<std::string> stream_id = std::nullopt) override;

    /**
     * @brief Subscribe a stage to a specific output stream.
     *
     * @param stream_id The stream identifier.
     * @param subscriber Stage to receive buffers from this stream.
     * @return AppStatus Status of the subscription.
     */
    AppStatus subscribe_to_stream(output_stream_id_t stream_id, hailo_analytics::pipeline::StagePtr subscriber);

    /**
     * @brief Subscribe all configured output streams to their respective subscribers.
     *
     * @return AppStatus Status of the subscriptions.
     */
    AppStatus subscribe_output_streams();

    /**
     * @brief Stop the frontend stage.
     *
     * @return AppStatus Status of the stop operation.
     */
    virtual AppStatus stop() override;

    /**
     * @brief Initialize the frontend stage.
     *
     * @return AppStatus Status of the initialization.
     */
    AppStatus init() override;

    /**
     * @brief Deinitialize the frontend stage.
     *
     * @return AppStatus Status of the deinitialization.
     */
    AppStatus deinit() override;

    /**
     * @brief Configure the frontend stage with a MediaLibrary instance.
     *
     * @param media_library Shared pointer to the MediaLibrary.
     * @return AppStatus Status of the configuration.
     */
    AppStatus configure(MediaLibraryInterfacePtr media_library);

    /**
     * @brief Main processing loop for the frontend stage.
     *
     * Continuously captures frames from the frontend and sends them to subscribers.
     */
    void loop() override;

    /**
     * @brief Get the available output streams from the frontend.
     *
     * @return Expected containing vector of output streams or error code.
     */
    tl::expected<std::vector<frontend_output_stream_t>, media_library_return> get_outputs_streams();

    /**
     * @brief Get the map of stream subscribers.
     * @return Map of stream IDs to their subscriber stages
     */
    const std::map<output_stream_id_t, std::vector<StagePtr>> &get_stream_subscribers() const
    {
        return m_stream_subscribers;
    }
};

/**
 * @brief Builder-based frontend stage for simplified construction.
 *
 * Provides a builder pattern interface for creating frontend stages with
 * configurable parameters.
 */
class FrontendStageBuild : public FrontendStage
{
  public:
    /**
     * @brief Builder class for FrontendStage construction.
     */
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = FRONTEND_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        /**
         * @brief Set the stage name.
         * @param name Name for the frontend stage.
         * @return Reference to this builder for chaining.
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Set the queue size (optional).
         * @param size Queue size.
         * @return Reference to this builder for chaining.
         */
        Builder &set_queue_size_opt(size_t size);

        /**
         * @brief Set whether the queue is leaky (optional).
         * @param activate True to enable leaky mode.
         * @return Reference to this builder for chaining.
         */
        Builder &set_leaky_opt(bool activate);

        /**
         * @brief Set whether to enable tracing (optional).
         * @param activate True to enable tracing.
         * @return Reference to this builder for chaining.
         */
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Build and return the FrontendStage.
         * @return Shared pointer to the constructed FrontendStage.
         */
        std::shared_ptr<FrontendStage> buildptr() const;
    };

    /**
     * @brief Create a new Builder for FrontendStage construction.
     * @return Builder instance.
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::sources
