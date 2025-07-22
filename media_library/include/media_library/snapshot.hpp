#pragma once

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <queue>
#include <set>

#include "buffer_pool.hpp"
#include "pipe_handler.hpp"
#include "threadpool.hpp"

// Structure to hold snapshot request data
struct SnapshotRequest
{
    std::string stage_name;
    std::string file_path;
    HailoMediaLibraryBufferPtr buffer;
};

class SnapshotManager
{
  private:
    std::unordered_map<std::string, bool> m_snapshot_map;
    std::string m_current_snapshot_directory;
    std::mutex m_mutex;

    // Tracking for async snapshot operations
    std::atomic<int> m_pending_operations;
    std::condition_variable m_completion_cv;
    std::mutex m_completion_mutex;
    bool m_frame_complete;

    std::atomic<bool> m_running;
    std::unique_ptr<PipeHandler> m_pipe_handler;
    std::string m_pipe_path;
    std::string m_response_pipe_path;

    std::mutex m_frame_mutex;

  protected:
    uint32_t m_frames_remaining;
    std::set<std::string> m_filtered_stages;

    void prepare_next_frame();

  public:
    static constexpr const char *MEDIA_LIBRARY_PATH = "/tmp/medialib_snapshots/";
    static constexpr const char *PIPE_PATH = "/tmp/medialib_snapshot_pipe";
    static constexpr const char *RESPONSE_PIPE_PATH = "/tmp/medialib_snapshot_response_pipe";
    static constexpr const char *SNAPSHOT_COMMAND = "snapshot";
    static constexpr const char *LIST_STAGES_COMMAND = "list_stages";

    static SnapshotManager &get_instance();

    void enable_snapshot(bool enable);
    void request_snapshot(uint32_t frames_count = 1, const std::set<std::string> &stages = {});
    void take_snapshot(const std::string &stage_name, const HailoMediaLibraryBufferPtr &buffer);
    std::string list_available_stages();

    ~SnapshotManager();

  protected:
    SnapshotManager();
    SnapshotManager(const SnapshotManager &) = delete;
    SnapshotManager &operator=(const SnapshotManager &) = delete;

    bool init_pipe_handler();
    void stop_pipe_handler();

    // Process a single snapshot request
    void process_snapshot_request(const SnapshotRequest &request);

    std::string generate_timestamp_directory();
    bool save_medialib_buffer(const HailoMediaLibraryBufferPtr &buffer, const std::string &file_path);
    std::string process_command(const std::string &command);
    bool has_snapshot_requested(const std::string &stage_name);
    void process_snapshot_frame_complete();
    std::string process_snapshot_command(std::istringstream &cmd_stream);

    inline bool is_enabled() const
    {
        return m_running.load();
    }
};
