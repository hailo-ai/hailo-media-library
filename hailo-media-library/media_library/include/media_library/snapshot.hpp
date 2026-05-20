#pragma once

#include <stdint.h>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <set>
#include <iosfwd>
#include <string>

#include "buffer_pool.hpp"
#include "pipe_handler.hpp"
#include "media_library_buffer.hpp"

struct SnapshotRequest
{
    std::string stage_name;
    std::string file_path;
    HailoMediaLibraryBufferPtr buffer;
};

class SnapshotManager
{
  private:
    // Protected by m_mutex
    std::unordered_map<std::string, bool> m_stage_requested;
    std::string m_current_snapshot_directory;
    std::mutex m_mutex;

    // Protected by m_completion_mutex
    std::atomic<int> m_pending_operations;
    std::mutex m_completion_mutex;
    bool m_frame_complete;

    std::atomic<bool> m_running;
    std::unique_ptr<PipeHandler> m_pipe_handler;
    std::string m_pipe_path;
    std::string m_response_pipe_path;

  protected:
    SnapshotManager();
    SnapshotManager(const SnapshotManager &) = delete;
    SnapshotManager &operator=(const SnapshotManager &) = delete;

    uint32_t m_frames_remaining;
    uint32_t m_total_frames_requested;
    uint32_t m_frame_interval;
    uint32_t m_skip_counter;
    std::string m_interval_counter_stage;
    std::set<std::string> m_filtered_stages;
    std::string m_pending_pipe_message;

    void prepare_next_frame();
    void advance_frame_sequence();
    std::string build_snapshot_filename(const std::string &stage_name, const HailoMediaLibraryBufferPtr &buffer) const;
    bool init_pipe_handler();
    void stop_pipe_handler();
    void process_snapshot_request(const SnapshotRequest &request);
    std::string generate_timestamp_directory();
    bool save_medialib_buffer(const HailoMediaLibraryBufferPtr &buffer, const std::string &file_path);
    std::string process_command(const std::string &command);
    bool has_snapshot_requested(const std::string &stage_name);
    void process_snapshot_frame_complete();
    std::string process_snapshot_command(std::istringstream &cmd_stream);
    void signal_frame_complete_if_ready();
    bool are_all_stages_cleared() const;
    static std::string format_to_extension(HailoFormat format);

    inline bool is_enabled() const
    {
        return m_running.load();
    }

    int get_pending_operations() const
    {
        return m_pending_operations.load();
    }

    void set_pending_operations(int value)
    {
        m_pending_operations.store(value);
    }

  public:
    static constexpr const char *MEDIA_LIBRARY_PATH = "/tmp/medialib_snapshots/";
    static constexpr const char *PIPE_PATH = "/tmp/medialib_snapshot_pipe";
    static constexpr const char *RESPONSE_PIPE_PATH = "/tmp/medialib_snapshot_response_pipe";
    static constexpr const char *SNAPSHOT_COMMAND = "snapshot";
    static constexpr const char *LIST_STAGES_COMMAND = "list_stages";
    static constexpr int MAX_PENDING_SNAPSHOT_OPERATIONS = 6;

    static SnapshotManager &get_instance();
    ~SnapshotManager();

    void enable_snapshot(bool enable);
    uint32_t request_snapshot(uint32_t frames_count = 1, const std::set<std::string> &stages = {},
                              uint32_t frame_interval = 1);
    void take_snapshot(const std::string &stage_name, const HailoMediaLibraryBufferPtr &buffer,
                       bool synchronous = false);
    std::string list_available_stages();
};
