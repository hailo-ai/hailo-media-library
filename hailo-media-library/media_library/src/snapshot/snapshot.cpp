#include "snapshot.hpp"
#include "media_library_logger.hpp"

#include <algorithm>
#include <sstream>

#include "env_vars.hpp"
#include "common.hpp"
#include "logger_macros.hpp"

#define MODULE_NAME LoggerType::Snapshot

SnapshotManager &SnapshotManager::get_instance()
{
    static SnapshotManager instance;
    return instance;
}

SnapshotManager::SnapshotManager()
    : m_pending_operations(0), m_frame_complete(false), m_running(false), m_pipe_path(PIPE_PATH),
      m_response_pipe_path(RESPONSE_PIPE_PATH), m_frames_remaining(1), m_total_frames_requested(1), m_frame_interval(1),
      m_skip_counter(0)
{
    if (is_env_variable_on(MEDIALIB_SNAPSHOT_ENABLE_ENV_VAR))
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot manager is enabled by environment variable.");
        if (init_pipe_handler())
        {
            m_running = true;
        }
    }
    else
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot manager is disabled by environment variable.");
    }
}

bool SnapshotManager::init_pipe_handler()
{
    if (m_pipe_handler && m_pipe_handler->is_running())
    {
        return true;
    }

    m_pipe_handler = std::make_unique<PipeHandler>(
        m_pipe_path, [this](const std::string &cmd) { return this->process_command(cmd); }, m_response_pipe_path);

    if (!m_pipe_handler->start())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to initialize pipe handler for snapshot manager.");
        return false;
    }

    return true;
}

void SnapshotManager::stop_pipe_handler()
{
    if (m_pipe_handler)
    {
        m_pipe_handler->stop();
    }
}

void SnapshotManager::enable_snapshot(bool enable)
{
    if (enable == m_running)
    {
        return;
    }

    if (enable)
    {
        if (init_pipe_handler())
        {
            m_running = true;
            LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot manager manually enabled for testing.");
        }
        else
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to enable snapshot manager.");
        }
    }
    else
    {
        stop_pipe_handler();
        m_running = false;
        LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot manager manually disabled.");
    }
}

SnapshotManager::~SnapshotManager()
{
    m_running = false;
    // PipeHandler's destructor will handle the cleanup
}

uint32_t SnapshotManager::request_snapshot(uint32_t frames_count, const std::set<std::string> &stages,
                                           uint32_t frame_interval)
{
    if (!m_running)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot manager is disabled, ignoring request.");
        return 0;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot requested for {} frames (interval: {} frames).", frames_count,
                         frame_interval);

    // Reset completion flag before acquiring m_mutex to maintain consistent
    // lock ordering (m_completion_mutex → m_mutex), matching the order used
    // in process_snapshot_request's async completion path.
    {
        std::lock_guard<std::mutex> completion_lock(m_completion_mutex);
        m_frame_complete = false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    m_frames_remaining = frames_count;
    m_total_frames_requested = frames_count;
    m_frame_interval = std::max(frame_interval, 1u);
    m_skip_counter = 0;
    m_pending_pipe_message.clear();

    // Set filtered stages - if no stages specified, use all available stages
    m_filtered_stages.clear();
    if (stages.empty())
    {
        for (const auto &[stage_name, _] : m_stage_requested)
        {
            m_filtered_stages.insert(stage_name);
        }
    }
    else
    {
        for (const auto &stage : stages)
        {
            if (m_stage_requested.contains(stage))
                m_filtered_stages.insert(stage);
            else
                LOGGER__MODULE__WARNING(MODULE_NAME, "Ignoring unknown stage '{}' — not registered.", stage);
        }
    }

    if (m_filtered_stages.empty())
    {
        m_frames_remaining = 0;
        return 0;
    }

    // Clear all stage flags from any previous (possibly stale) request.
    // Without this, a phantom stage left true from a prior request would
    // block are_all_stages_cleared() indefinitely.
    for (auto &[stage_name, flag] : m_stage_requested)
    {
        flag = false;
    }

    m_interval_counter_stage = *m_filtered_stages.begin();
    prepare_next_frame();

    return static_cast<uint32_t>(m_filtered_stages.size());
}

bool SnapshotManager::has_snapshot_requested(const std::string &stage_name)
{
    if (!m_running)
    {
        return false;
    }

    if (stage_name.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_stage_requested.contains(stage_name))
    {
        m_stage_requested[stage_name] = false;
    }

    return m_stage_requested[stage_name];
}

bool SnapshotManager::are_all_stages_cleared() const
{
    return std::none_of(m_filtered_stages.begin(), m_filtered_stages.end(),
                        [this](const auto &stage) { return m_stage_requested.at(stage); });
}

void SnapshotManager::signal_frame_complete_if_ready()
{
    std::lock_guard<std::mutex> lock(m_completion_mutex);
    m_frame_complete = true;
    if (m_pending_operations.load() == 0)
    {
        process_snapshot_frame_complete();
        m_frame_complete = false;
    }
}

std::string SnapshotManager::build_snapshot_filename(const std::string &stage_name,
                                                     const HailoMediaLibraryBufferPtr &buffer) const
{
    std::string extension = format_to_extension(buffer->buffer_data->format);
    return m_current_snapshot_directory + "/" + stage_name + "_" + std::to_string(buffer->buffer_data->width) + "x" +
           std::to_string(buffer->buffer_data->height) + extension;
}

void SnapshotManager::advance_frame_sequence()
{
    m_frames_remaining--;

    if (m_frames_remaining > 0)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot frame completed. {} frames remaining.", m_frames_remaining);
        m_pending_pipe_message = "SNAPSHOT_PROGRESS:" + std::to_string(m_total_frames_requested - m_frames_remaining) +
                                 "/" + std::to_string(m_total_frames_requested);

        if (m_frame_interval > 1)
        {
            m_skip_counter = m_frame_interval - 1;
            LOGGER__MODULE__TRACE(MODULE_NAME, "Skipping {} frames before next capture.", m_skip_counter);
        }
        else
        {
            prepare_next_frame();
        }
    }
    else
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot sequence completed.");
        m_pending_pipe_message = "SNAPSHOT_COMPLETE:" + std::to_string(m_total_frames_requested);
    }
}

void SnapshotManager::take_snapshot(const std::string &stage_name, const HailoMediaLibraryBufferPtr &buffer,
                                    bool synchronous)
{
    if (!m_running || stage_name.empty())
    {
        return;
    }

    std::string filename;
    bool is_frame_complete = false;
    bool should_save = false;

    // Single lock scope: register stage, handle skip intervals, validate buffer,
    // clear stage flag, build filename, and check frame completion.
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Auto-register unknown stages on first encounter
        if (!m_stage_requested.contains(stage_name))
            m_stage_requested[stage_name] = false;

        // During frame-interval skipping, only the counter stage decrements
        if (m_skip_counter > 0)
        {
            if (stage_name == m_interval_counter_stage)
            {
                m_skip_counter--;
                LOGGER__MODULE__TRACE(MODULE_NAME, "Frame skip: {} remaining", m_skip_counter);
                if (m_skip_counter == 0)
                {
                    prepare_next_frame();
                }
            }
            return;
        }

        if (!m_stage_requested[stage_name])
        {
            LOGGER__MODULE__TRACE(MODULE_NAME, "Snapshot not requested for stage '{}'.", stage_name);
            return;
        }

        // Validate buffer before clearing the stage flag — if invalid, leave
        // the flag set so the stage can be retried on the next frame.
        if (!buffer || !buffer->buffer_data)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Invalid buffer provided for snapshot, for stage name {}.", stage_name);
            return;
        }

        m_stage_requested[stage_name] = false;

        if (!synchronous && m_pending_operations.load() >= MAX_PENDING_SNAPSHOT_OPERATIONS)
        {
            LOGGER__MODULE__WARNING(MODULE_NAME, "Dropping snapshot for '{}': {} async operations pending (max {})",
                                    stage_name, m_pending_operations.load(), MAX_PENDING_SNAPSHOT_OPERATIONS);
        }
        else
        {
            filename = build_snapshot_filename(stage_name, buffer);
            should_save = true;
            m_pending_operations++;
        }

        is_frame_complete = are_all_stages_cleared();

        if (is_frame_complete && m_frames_remaining > 0)
        {
            advance_frame_sequence();
        }
    }

    // Dispatch save outside the lock to avoid holding m_mutex during I/O
    if (should_save)
    {
        SnapshotRequest request{stage_name, filename, buffer};

        if (synchronous)
        {
            process_snapshot_request(request);
        }
        else
        {
            ThreadPool::GetInstance()->enqueue(&SnapshotManager::process_snapshot_request, this, request);
        }
    }

    if (is_frame_complete)
    {
        signal_frame_complete_if_ready();
    }
}

void SnapshotManager::process_snapshot_frame_complete()
{
    std::string pipe_message;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        pipe_message = std::move(m_pending_pipe_message);
    }

    if (!pipe_message.empty() && m_pipe_handler)
    {
        m_pipe_handler->write_response(pipe_message);
    }
}

void SnapshotManager::prepare_next_frame()
{
    m_current_snapshot_directory = generate_timestamp_directory();
    if (m_current_snapshot_directory.empty())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create snapshot directory, aborting snapshot sequence.");
        m_frames_remaining = 0;
        return;
    }

    for (const auto &stage_name : m_filtered_stages)
    {
        m_stage_requested[stage_name] = true;
    }
}

std::string SnapshotManager::list_available_stages()
{
    if (!m_running)
    {
        return "Snapshot manager is disabled";
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    std::stringstream ss;

    ss << "Available stages for snapshot:\n";

    if (m_stage_requested.empty())
    {
        ss << "No stages available yet. Run your pipeline first.";
    }
    else
    {
        for (const auto &[stage_name, _] : m_stage_requested)
        {
            ss << "- " << stage_name << "\n";
        }
    }

    return ss.str();
}
