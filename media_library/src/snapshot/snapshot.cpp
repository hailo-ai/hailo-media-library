#include "snapshot.hpp"
#include "media_library_logger.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/statvfs.h> // For statvfs
#include <fcntl.h>       // For O_WRONLY, O_CREAT, etc.
#include <unistd.h>      // For write, close, fsync
#include <string.h>      // For strerror
#include <errno.h>       // For errno

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
      m_response_pipe_path(RESPONSE_PIPE_PATH), m_frames_remaining(1)
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

void SnapshotManager::request_snapshot(uint32_t frames_count, const std::set<std::string> &stages)
{
    if (!m_running)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot manager is disabled, ignoring request.");
        return;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot requested for {} frames.", frames_count);
    std::lock_guard<std::mutex> lock(m_mutex);

    // Initialize the configuration for this snapshot sequence
    m_frames_remaining = frames_count;

    // Set filtered stages - if no stages specified, use all available stages
    if (stages.empty())
    {
        m_filtered_stages.clear();
        for (const auto &[stage_name, _] : m_snapshot_map)
        {
            m_filtered_stages.insert(stage_name);
        }
    }
    else
    {
        m_filtered_stages = stages;
    }

    prepare_next_frame();
}

bool SnapshotManager::has_snapshot_requested(const std::string &stage_name)
{
    if (!m_running)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_snapshot_map.find(stage_name) == m_snapshot_map.end())
    {
        m_snapshot_map[stage_name] = false;
    }

    return m_snapshot_map[stage_name];
}

void SnapshotManager::take_snapshot(const std::string &stage_name, const HailoMediaLibraryBufferPtr &buffer)
{
    if (!m_running)
    {
        return;
    }

    if (!has_snapshot_requested(stage_name))
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "Snapshot not requested for stage '{}'.", stage_name);
        return;
    }

    if (!buffer || !buffer->buffer_data)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Invalid buffer provided for snapshot, for stage name {}.", stage_name);
        return;
    }

    // Mark this stage as having its snapshot taken
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_snapshot_map[stage_name] = false;
    }

    std::string filename = m_current_snapshot_directory + "/" + stage_name + "_" +
                           std::to_string(buffer->buffer_data->width) + "x" +
                           std::to_string(buffer->buffer_data->height) + ".nv12";

    SnapshotRequest request{stage_name, filename, buffer};

    // Increment the pending operations counter
    m_pending_operations++;

    // Use ThreadPool to process the snapshot request asynchronously
    ThreadPool::GetInstance()->enqueue(&SnapshotManager::process_snapshot_request, this, request);

    // Check if this was the last stage for this frame
    bool is_frame_complete = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Simply check if any stage that should be captured still has a true value
        is_frame_complete =
            std::none_of(m_snapshot_map.begin(), m_snapshot_map.end(), [](const auto &pair) { return pair.second; });
    }

    // If all stages have been processed, the frame is logically complete
    // But we'll wait for all file operations to finish before moving to the next frame
    if (is_frame_complete)
    {
        // Set flag that logical frame is complete, but wait for all pending operations
        std::unique_lock<std::mutex> lock(m_completion_mutex);
        m_frame_complete = true;

        // If there are no more pending operations, process the frame completion immediately
        if (m_pending_operations.load() == 0)
        {
            process_snapshot_frame_complete();
            m_frame_complete = false;
        }
    }
}

void SnapshotManager::process_snapshot_frame_complete()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_frames_remaining > 1)
    {
        m_frames_remaining--;
        LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot frame completed. {} frames remaining.", m_frames_remaining);

        prepare_next_frame();
    }
    else
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot sequence completed.");
    }
}

void SnapshotManager::prepare_next_frame()
{
    m_current_snapshot_directory = generate_timestamp_directory();

    for (const auto &stage_name : m_filtered_stages)
    {
        m_snapshot_map[stage_name] = true;
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

    if (m_snapshot_map.empty())
    {
        ss << "No stages available yet. Run your pipeline first.";
    }
    else
    {
        for (const auto &[stage_name, _] : m_snapshot_map)
        {
            ss << "- " << stage_name << "\n";
        }
    }

    return ss.str();
}

std::string SnapshotManager::generate_timestamp_directory()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    // Add milliseconds to the timestamp to ensure uniqueness
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream timestamp_stream;
    timestamp_stream << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d_%H-%M-%S");
    timestamp_stream << "_" << std::setfill('0') << std::setw(3) << ms.count();

    std::string directory_path = std::string(MEDIA_LIBRARY_PATH) + timestamp_stream.str();
    std::filesystem::create_directories(directory_path);

    LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot directory created: {}", directory_path);

    return directory_path;
}

std::string SnapshotManager::process_command(const std::string &command)
{
    std::istringstream cmd_stream(command);
    std::string cmd_name;
    cmd_stream >> cmd_name;

    // Convert command name to lowercase for case-insensitive comparison
    std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), [](unsigned char c) { return std::tolower(c); });

    std::string response;

    if (cmd_name == SNAPSHOT_COMMAND)
    {
        response = process_snapshot_command(cmd_stream);
    }
    else if (cmd_name == LIST_STAGES_COMMAND)
    {
        response = list_available_stages();
    }
    else if (!command.empty())
    {
        LOGGER__MODULE__WARNING(MODULE_NAME, "Unknown command: '{}'", command);
        response = "Error: Unknown command. Available commands: 'snapshot [frames_count] [stage_list]', 'list_stages'";
    }

    return response;
}

std::string SnapshotManager::process_snapshot_command(std::istringstream &cmd_stream)
{
    // Parse arguments: snapshot [frames_count] [stage1,stage2,...]
    uint32_t frames_count = 1;
    std::string stages_arg;
    std::string response;

    if (cmd_stream >> frames_count)
    {
        // Optional stage filter argument
        if (cmd_stream >> stages_arg)
        {
            std::set<std::string> filtered_stages;
            std::string stage;
            std::istringstream stages_stream(stages_arg);

            // Parse comma-separated list of stages
            while (std::getline(stages_stream, stage, ','))
            {
                if (!stage.empty())
                {
                    filtered_stages.insert(stage);
                }
            }

            request_snapshot(frames_count, filtered_stages);

            if (filtered_stages.empty())
            {
                response = "Snapshot requested for " + std::to_string(frames_count) + " frames";
            }
            else
            {
                response = "Snapshot requested for " + std::to_string(frames_count) + " frames with " +
                           std::to_string(filtered_stages.size()) + " filtered stages";
            }
        }
        else
        {
            // Only frames_count provided
            request_snapshot(frames_count);
            response = "Snapshot requested for " + std::to_string(frames_count) + " frames";
        }
    }
    else
    {
        // No arguments - default to 1 frame
        request_snapshot();
        response = "Snapshot requested for 1 frame";
    }

    return response;
}

bool SnapshotManager::save_medialib_buffer(const HailoMediaLibraryBufferPtr &buffer, const std::string &file_path)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Saving buffer to: {}", file_path);

    const void *y_plane = buffer->get_plane_ptr(0);
    size_t y_size = buffer->get_plane_size(0);
    const void *uv_plane = buffer->get_plane_ptr(1);
    size_t uv_size = buffer->get_plane_size(1);
    size_t total_size = y_size + uv_size;

    std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open file: {}", file_path);
        return false;
    }

    out.write(static_cast<const char *>(y_plane), y_size);
    out.write(static_cast<const char *>(uv_plane), uv_size);

    if (!out)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to write buffer to file: {}", file_path);
        return false;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Saved {} bytes to {}", total_size, file_path);
    return true;
}

void SnapshotManager::process_snapshot_request(const SnapshotRequest &request)
{
    if (!save_medialib_buffer(request.buffer, request.file_path))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to write buffer to file for stage '{}'.", request.stage_name);
    }
    else
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Successfully saved snapshot for stage '{}'.", request.stage_name);
    }

    // Decrement the pending operations counter and check if we need to process frame completion
    int pending = m_pending_operations.fetch_sub(1) - 1;

    if (pending == 0)
    {
        // This was the last pending operation, check if we need to process frame completion
        std::unique_lock<std::mutex> lock(m_completion_mutex);
        if (m_frame_complete)
        {
            m_frame_complete = false;
            process_snapshot_frame_complete();
        }
    }
}
