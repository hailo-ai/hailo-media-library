#include "medialib_instance_registry.hpp"

#include <tl/expected.hpp>
#include <utility>

#include "media_library/media_library_types.hpp"

namespace hailo::gst_api
{

MediaLibInstanceRegistry &MediaLibInstanceRegistry::instance()
{
    static MediaLibInstanceRegistry g_instance;
    return g_instance;
}

tl::expected<MediaLibraryPtr, media_library_return> MediaLibInstanceRegistry::create_and_initialize(
    const std::string &pipeline_name, const std::string &config_string)
{
    if (pipeline_name.empty())
        return tl::unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
    if (config_string.empty())
        return tl::unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);

    // Create instance (MediaLibrary::create() does not require the lock)
    auto create_result = MediaLibrary::create();
    if (!create_result.has_value())
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pipeline_entries[pipeline_name] = Entry{nullptr, true, create_result.error(), {}};
        m_instance_ready_cv.notify_all();
        return tl::unexpected(create_result.error());
    }

    auto media_lib = create_result.value();

    // Initialize outside the lock — this may take a long time.
    // Meanwhile, get_initialized() callers are waiting on m_instance_ready_cv, which releases m_mutex.
    auto init_status = media_lib->initialize(config_string);

    // Store result and wake waiters
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pipeline_entries[pipeline_name] =
            Entry{(init_status == MEDIA_LIBRARY_SUCCESS) ? media_lib : nullptr, true, init_status, {}};
    }
    m_instance_ready_cv.notify_all();

    if (init_status != MEDIA_LIBRARY_SUCCESS)
        return tl::unexpected(init_status);

    return media_lib;
}

tl::expected<MediaLibraryPtr, media_library_return> MediaLibInstanceRegistry::get_initialized(
    const std::string &pipeline_name, std::chrono::seconds timeout)
{
    if (pipeline_name.empty())
        return tl::unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);

    std::unique_lock<std::mutex> lock(m_mutex);

    // Create placeholder if encoder arrives before vision
    m_pipeline_entries.try_emplace(pipeline_name);

    // Wait until vision calls create_and_initialize and sets ready=true, or timeout
    bool wait_succeeded = m_instance_ready_cv.wait_for(lock, timeout, [this, &pipeline_name]() {
        auto entry_it = m_pipeline_entries.find(pipeline_name);
        return entry_it != m_pipeline_entries.end() && entry_it->second.ready;
    });

    if (!wait_succeeded)
    {
        return tl::unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    // Re-lookup after waking (safe under lock, no rehash possible here)
    auto entry_it = m_pipeline_entries.find(pipeline_name);
    if (entry_it == m_pipeline_entries.end() || entry_it->second.init_status != MEDIA_LIBRARY_SUCCESS ||
        !entry_it->second.instance)
    {
        auto error_status =
            (entry_it != m_pipeline_entries.end()) ? entry_it->second.init_status : MEDIA_LIBRARY_UNINITIALIZED;
        return tl::unexpected(error_status);
    }

    return entry_it->second.instance;
}

tl::expected<MediaLibraryPtr, media_library_return> MediaLibInstanceRegistry::get_initialized(
    const std::string &pipeline_name, const std::string &stream_id, std::chrono::seconds timeout)
{
    if (stream_id.empty())
        return tl::unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);

    // Reuse the base overload for the blocking wait
    auto result = get_initialized(pipeline_name, timeout);
    if (!result.has_value())
        return result;

    // Claim the stream-id under the lock
    std::lock_guard<std::mutex> lock(m_mutex);
    auto entry_it = m_pipeline_entries.find(pipeline_name);
    if (entry_it == m_pipeline_entries.end())
        return tl::unexpected(MEDIA_LIBRARY_UNINITIALIZED);

    auto &claimed = entry_it->second.claimed_stream_ids;
    if (claimed.count(stream_id))
        return tl::unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);

    claimed.insert(stream_id);
    return result;
}

void MediaLibInstanceRegistry::release_encoder_stream(const std::string &pipeline_name, const std::string &stream_id)
{
    if (pipeline_name.empty() || stream_id.empty())
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto entry_it = m_pipeline_entries.find(pipeline_name);
    if (entry_it != m_pipeline_entries.end())
        entry_it->second.claimed_stream_ids.erase(stream_id);
}

void MediaLibInstanceRegistry::unregister_instance(const std::string &pipeline_name)
{
    if (pipeline_name.empty())
        return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pipeline_entries.erase(pipeline_name);
    }
    // Wake any waiters so they can see the entry is gone and fail gracefully
    m_instance_ready_cv.notify_all();
}

} // namespace hailo::gst_api
