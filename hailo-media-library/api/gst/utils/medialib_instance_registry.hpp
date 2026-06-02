/**
 * @file medialib_instance_registry.hpp
 * @brief Process-wide registry for sharing a MediaLibrary instance across GStreamer elements.
 *
 * The vision element creates and initialises the MediaLibrary via
 * @c create_and_initialize(). Encoder elements in the same pipeline call
 * @c get_initialized() to obtain the shared instance, blocking until it
 * is ready. The pipeline name is used as the lookup key.
 **/
#pragma once

#include <tl/expected.hpp>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <chrono>

#include "media_library/media_library.hpp"
#include "media_library/media_library_types.hpp"

namespace hailo::gst_api
{

/**
 * @brief Thread-safe singleton registry mapping pipeline names to MediaLibrary instances.
 */
class MediaLibInstanceRegistry
{
    static constexpr std::chrono::seconds DEFAULT_INITIALIZATION_TIMEOUT{10};

  public:
    /** @brief Return the process-wide singleton. */
    static MediaLibInstanceRegistry &instance();

    /**
     * @brief Create and initialise the MediaLibrary for the given pipeline.
     *
     * Only one caller (the vision element) should call this per @p pipeline_name.
     * Wakes up all waiters blocked in @c get_initialized().
     *
     * @param[in] pipeline_name - unique name of the owning GStreamer pipeline.
     * @param[in] config_string - MediaLibrary JSON configuration.
     * @return The initialised MediaLibrary, or an error code on failure.
     */
    tl::expected<MediaLibraryPtr, media_library_return> create_and_initialize(const std::string &pipeline_name,
                                                                              const std::string &config_string);

    /**
     * @brief Obtain an initialised MediaLibrary instance, blocking until ready.
     *
     * Multiple callers (encoder elements) can call this for the same
     * @p pipeline_name concurrently.
     *
     * @param[in] pipeline_name - pipeline whose MediaLibrary is requested.
     * @param[in] timeout - maximum time to wait (default 10 s).
     * @return The initialised MediaLibrary, or an error code on timeout / failure.
     */
    tl::expected<MediaLibraryPtr, media_library_return> get_initialized(
        const std::string &pipeline_name, std::chrono::seconds timeout = DEFAULT_INITIALIZATION_TIMEOUT);

    /**
     * @brief Obtain an initialised MediaLibrary instance and claim a stream-id.
     *
     * Like @c get_initialized(), but additionally registers @p stream_id as
     * exclusively owned by the caller. If another element has already claimed
     * the same stream-id for this pipeline, the call fails immediately.
     *
     * @param[in] pipeline_name - pipeline whose MediaLibrary is requested.
     * @param[in] stream_id - encoder stream-id to claim (must not be empty).
     * @param[in] timeout - maximum time to wait (default 10 s).
     * @return The initialised MediaLibrary, or an error code on timeout / failure / duplicate claim.
     */
    tl::expected<MediaLibraryPtr, media_library_return> get_initialized(
        const std::string &pipeline_name, const std::string &stream_id,
        std::chrono::seconds timeout = DEFAULT_INITIALIZATION_TIMEOUT);

    /**
     * @brief Release a previously claimed encoder stream-id.
     *
     * Safe to call multiple times or with an unknown pipeline/stream-id (no-op).
     *
     * @param[in] pipeline_name - pipeline that owns the claim.
     * @param[in] stream_id - stream-id to release.
     */
    void release_encoder_stream(const std::string &pipeline_name, const std::string &stream_id);

    /**
     * @brief Remove the registry entry for a pipeline. Safe to call multiple times.
     *
     * @param[in] pipeline_name - pipeline whose entry should be removed.
     */
    void unregister_instance(const std::string &pipeline_name);

  private:
    MediaLibInstanceRegistry() = default;

    struct Entry
    {
        MediaLibraryPtr instance;                                       ///< Shared MediaLibrary handle.
        bool ready = false;                                             ///< True once initialisation completes.
        media_library_return init_status = MEDIA_LIBRARY_UNINITIALIZED; ///< Result of initialisation.
        std::unordered_set<std::string> claimed_stream_ids;             ///< Encoder stream-ids claimed by elements.
    };

    std::mutex m_mutex;                                        ///< Protects @c m_pipeline_entries.
    std::condition_variable m_instance_ready_cv;               ///< Signalled when an entry becomes ready.
    std::unordered_map<std::string, Entry> m_pipeline_entries; ///< Per-pipeline entries.
};

} // namespace hailo::gst_api
