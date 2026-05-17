#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ansi
{
static constexpr const char *RESET = "\033[0m";
static constexpr const char *BOLD = "\033[1m";
static constexpr const char *DIM = "\033[2m";
static constexpr const char *RED = "\033[31m";
static constexpr const char *GREEN = "\033[32m";
static constexpr const char *YELLOW = "\033[33m";
static constexpr const char *CYAN = "\033[36m";
static constexpr const char *BOLD_CYAN = "\033[1;36m";
static constexpr const char *BOLD_RED = "\033[1;31m";
static constexpr const char *BOLD_GREEN = "\033[1;32m";
static constexpr const char *BOLD_YELLOW = "\033[1;33m";
} // namespace ansi

namespace unicode
{
static constexpr const char *FULL_BLOCK = "\xe2\x96\x88";                           // █ U+2588
static constexpr const char *LIGHT_SHADE = "\xe2\x96\x91";                          // ░ U+2591
static constexpr const char *CHECK_MARK = "\xe2\x9c\x93";                           // ✓ U+2713
static constexpr const char *TREE_BRANCH = "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 "; // ├──
static constexpr const char *TREE_CORNER = "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 "; // └──
} // namespace unicode

// Global color flag for signal handler (set from main, before signals can fire)
extern bool g_use_color;

// Cached stage names for tab completion (populated by list_stages)
extern std::vector<std::string> g_cached_stages;

// Completable words for readline (command names + flags), populated by register_command/register_completion
extern std::vector<std::string> g_command_completions;

class SnapshotCli
{
  public:
    static constexpr int RESPONSE_TIMEOUT_MS = 500;
    static constexpr int COMMAND_TIMEOUT_SEC = 10;
    static constexpr int PIPE_POLL_INTERVAL_MS = 50;
    static constexpr int MONITOR_SLEEP_INTERVAL_MS = 100;
    static constexpr size_t READ_BUFFER_SIZE = 4096;
    static constexpr uint32_t MAX_FRAME_COUNT = 10000;
    static constexpr uint32_t MAX_FRAME_INTERVAL = 10000;
    static constexpr size_t DEFAULT_LIST_COUNT = 10;

    using CommandHandler = std::function<void(SnapshotCli &, const std::vector<std::string> &)>;

    SnapshotCli();
    virtual ~SnapshotCli();

    void shutdown();
    bool initialize();
    int run();

    // --- Command registration ---
    void register_command(const std::string &name, CommandHandler handler);
    void register_completion(const std::string &word);

    // --- Color helpers ---
    std::string color(const char *code) const;
    void print_error(const std::string &msg) const;
    void print_success(const std::string &msg) const;

    // --- Pipe communication ---
    virtual bool send_command_and_wait_response(const std::string &command, int timeout_sec = COMMAND_TIMEOUT_SEC);
    std::string send_command_silent(const std::string &command, int timeout_sec);
    std::string format_response(const std::string &response) const;

    // --- Response parsing ---
    static void parse_stages_from_response(const std::string &response);

    // Accessed by signal handler and command handlers
    std::atomic<bool> m_running;
    std::string m_received_response;

  protected:
    // Command dispatch
    std::map<std::string, CommandHandler> m_command_handlers;

    bool m_use_color;

    // --- Initialization & lifecycle ---
    void register_command_handlers();

    // --- Input handling ---
    void handle_user_input(const std::string &input);
    std::vector<std::string> parse_command(const std::string &input);

  private:
    // --- Pipe IPC internals ---
    std::mutex m_response_mutex;
    std::condition_variable m_response_cv;
    std::string m_pending_command;
    bool m_waiting_for_response;
    std::thread m_monitor_thread;
    int m_response_pipe_fd = -1;
    int m_epoll_fd = -1;
    bool m_monitor_ready = false;
    std::mutex m_monitor_ready_mutex;
    std::condition_variable m_monitor_ready_cv;

    bool check_pipes_exist();
    void cleanup();

    // --- Run loops ---
    void run_interactive();
    void run_non_interactive();

    // --- Pipe IPC ---
    void monitor_response_pipe();
    void process_pipe_response(const std::string &response);
    void prepare_for_response(const std::string &command);
    bool write_command_to_pipe(const std::string &command);
    bool wait_for_response(int timeout_sec = COMMAND_TIMEOUT_SEC);
    void fetch_stages_for_completion();

    // --- Response formatting ---
    std::string format_stages_response(const std::string &response) const;
};

// --- Command registration (defined in commands/*.cpp) ---
void register_help_commands(SnapshotCli &cli);
void register_snapshot_command(SnapshotCli &cli);
void register_list_stages_command(SnapshotCli &cli);
void register_list_snapshots_command(SnapshotCli &cli);
void register_clear_command(SnapshotCli &cli);
