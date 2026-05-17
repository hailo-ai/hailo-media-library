#include "snapshot_cli.hpp"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <signal.h>
#include <sstream>

#include <sys/stat.h>
#include <unistd.h>

#include <readline/history.h>
#include <readline/readline.h>

#include "logger_macros.hpp"
#include "media_library_logger.hpp"
#include "snapshot.hpp"

#define MODULE_NAME LoggerType::Snapshot

// Global definitions (declared extern in snapshot_cli.hpp)
bool g_use_color = false;
std::vector<std::string> g_cached_stages;
std::vector<std::string> g_command_completions;

// Tab completion
static char *command_generator(const char *text, int state)
{
    static size_t index;
    if (state == 0)
        index = 0;

    while (index < g_command_completions.size())
    {
        const std::string &name = g_command_completions[index];
        index++;
        if (name.compare(0, strlen(text), text) == 0)
        {
            return strdup(name.c_str());
        }
    }
    return nullptr;
}

static char *stage_generator(const char *text, int state)
{
    static size_t index;
    static std::string prefix;
    static std::string match_text;

    if (state == 0)
    {
        index = 0;
        std::string full(text);
        auto comma_pos = full.rfind(',');
        if (comma_pos != std::string::npos)
        {
            prefix = full.substr(0, comma_pos + 1);
            match_text = full.substr(comma_pos + 1);
        }
        else
        {
            prefix = "";
            match_text = full;
        }
    }

    while (index < g_cached_stages.size())
    {
        const std::string &stage = g_cached_stages[index];
        index++;
        if (stage.compare(0, match_text.size(), match_text) == 0)
        {
            std::string result = prefix + stage;
            return strdup(result.c_str());
        }
    }
    return nullptr;
}

static char **command_completion(const char *text, int start, int /*end*/)
{
    // Prevent readline from falling back to filename completion
    rl_attempted_completion_over = 1;

    // Complete command names at start of line
    if (start == 0)
    {
        return rl_completion_matches(text, command_generator);
    }
    // Complete stage names when inside a snapshot command
    if (!g_cached_stages.empty())
    {
        std::string line(rl_line_buffer, start);
        auto first_non_space = line.find_first_not_of(' ');
        if (first_non_space != std::string::npos && line.compare(first_non_space, 8, "snapshot") == 0)
        {
            return rl_completion_matches(text, stage_generator);
        }
    }
    // Complete flags (--interval, --all, etc.) from the global completions list
    if (text[0] == '-')
    {
        return rl_completion_matches(text, command_generator);
    }
    return nullptr;
}

// --- SnapshotCli core methods ---

SnapshotCli::SnapshotCli() : m_running(false), m_use_color(false), m_waiting_for_response(false)
{
}

SnapshotCli::~SnapshotCli()
{
    cleanup();
}

void SnapshotCli::shutdown()
{
    m_running = false;
    m_response_cv.notify_all();
}

bool SnapshotCli::initialize()
{
    m_running = true;
    m_waiting_for_response = false;
    m_use_color = isatty(STDOUT_FILENO);
    g_use_color = m_use_color;

    register_command_handlers();

    if (!check_pipes_exist())
    {
        return false;
    }

    rl_attempted_completion_function = command_completion;
    // Prevent readline from installing its own SIGINT handler,
    // which would override the custom one set in setup_signal_handlers().
    rl_catch_signals = 0;

    m_monitor_thread = std::thread(&SnapshotCli::monitor_response_pipe, this);

    // Wait for monitor thread to open the response pipe before proceeding
    {
        std::unique_lock<std::mutex> lock(m_monitor_ready_mutex);
        m_monitor_ready_cv.wait_for(lock, std::chrono::seconds(2), [this] { return m_monitor_ready; });
    }

    return true;
}

int SnapshotCli::run()
{
    bool interactive = isatty(STDIN_FILENO);

    fetch_stages_for_completion();

    if (interactive)
    {
        std::cout << color(ansi::DIM) << "───" << color(ansi::RESET) << " " << color(ansi::BOLD_CYAN)
                  << "Hailo Snapshot Tool" << color(ansi::RESET) << " " << color(ansi::DIM) << "───"
                  << color(ansi::RESET) << "\n"
                  << color(ansi::DIM) << "Type 'help' for commands, Tab to complete." << color(ansi::RESET)
                  << std::endl;
    }

    if (interactive)
    {
        run_interactive();
    }
    else
    {
        run_non_interactive();
    }

    cleanup();
    LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot tool exiting.");
    return 0;
}

std::string SnapshotCli::color(const char *code) const
{
    return m_use_color ? code : "";
}

void SnapshotCli::print_error(const std::string &msg) const
{
    std::cout << color(ansi::BOLD_RED) << "Error:" << color(ansi::RESET) << " " << msg << std::endl;
}

void SnapshotCli::print_success(const std::string &msg) const
{
    std::cout << color(ansi::BOLD_GREEN) << msg << color(ansi::RESET) << std::endl;
}

void SnapshotCli::register_command(const std::string &name, CommandHandler handler)
{
    m_command_handlers[name] = std::move(handler);
    g_command_completions.push_back(name);
}

void SnapshotCli::register_completion(const std::string &word)
{
    g_command_completions.push_back(word);
}

void SnapshotCli::register_command_handlers()
{
    register_help_commands(*this);
    register_snapshot_command(*this);
    register_list_stages_command(*this);
    register_list_snapshots_command(*this);
    register_clear_command(*this);
}

void SnapshotCli::run_interactive()
{
    std::string prompt;
    if (m_use_color)
    {
        prompt =
            "\001" + std::string(ansi::BOLD_CYAN) + "\002" + "snapshot> " + "\001" + std::string(ansi::RESET) + "\002";
    }
    else
    {
        prompt = "snapshot> ";
    }

    while (m_running)
    {
        char *line = readline(prompt.c_str());
        if (line == nullptr)
        {
            break;
        }

        std::string input(line);
        free(line);

        if (!input.empty())
        {
            add_history(input.c_str());
            handle_user_input(input);
        }
    }
}

void SnapshotCli::run_non_interactive()
{
    std::string line;
    while (m_running && std::getline(std::cin, line))
    {
        if (!line.empty())
        {
            handle_user_input(line);
        }
    }
}

bool SnapshotCli::check_pipes_exist()
{
    std::filesystem::path pipe_path(SnapshotManager::PIPE_PATH);

    if (!std::filesystem::exists(pipe_path))
    {
        print_error("Snapshot feature is not enabled.");
        std::cout << color(ansi::DIM) << "Set MEDIALIB_SNAPSHOT_ENABLE=1 before starting Media Library."
                  << color(ansi::RESET) << std::endl;
        LOGGER__MODULE__ERROR(MODULE_NAME, "Snapshot feature is not enabled in Media Library.");
        LOGGER__MODULE__ERROR(MODULE_NAME, "Please export MEDIALIB_SNAPSHOT_ENABLE=1 environment variable.");
        return false;
    }
    return true;
}

void SnapshotCli::cleanup()
{
    m_running = false;

    if (m_monitor_thread.joinable())
    {
        m_monitor_thread.join();
    }
}

std::vector<std::string> SnapshotCli::parse_command(const std::string &input)
{
    std::vector<std::string> tokens;
    std::stringstream ss(input);
    std::string token;

    while (ss >> token)
    {
        tokens.push_back(token);
    }

    return tokens;
}

void SnapshotCli::handle_user_input(const std::string &input)
{
    std::vector<std::string> tokens = parse_command(input);

    if (tokens.empty())
    {
        return;
    }

    std::string command = tokens[0];
    std::vector<std::string> args(tokens.begin() + 1, tokens.end());

    auto it = m_command_handlers.find(command);
    if (it != m_command_handlers.end())
    {
        (it->second)(*this, args);
    }
    else
    {
        send_command_and_wait_response(input);
    }
}

// --- Signal handling & main ---

static SnapshotCli *g_cli_instance = nullptr;

static void signal_handler_callback(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        if (g_use_color)
        {
            const char *msg = "\n\033[1;33mInterrupted.\033[0m\n";
            if (write(STDOUT_FILENO, msg, strlen(msg)) < 0)
            {
                // Nothing to do in a signal handler if write fails
            }
        }
        else
        {
            const char *msg = "\nInterrupted.\n";
            if (write(STDOUT_FILENO, msg, strlen(msg)) < 0)
            {
                // Nothing to do in a signal handler if write fails
            }
        }

        if (g_cli_instance)
        {
            g_cli_instance->shutdown();
        }

        rl_done = 1;
    }
}

static void setup_signal_handlers()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &signal_handler_callback;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

int main(int /*argc*/, char * /*argv*/[])
{
    setup_signal_handlers();

    SnapshotCli cli;
    g_cli_instance = &cli;

    int result = 1;
    if (cli.initialize())
    {
        result = cli.run();
    }

    g_cli_instance = nullptr;
    return result;
}
