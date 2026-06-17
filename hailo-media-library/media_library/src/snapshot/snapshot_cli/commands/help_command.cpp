#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "../snapshot_cli.hpp"

static void print_help_line(const SnapshotCli &cli, const std::string &left, const std::string &right,
                            const char *left_color)
{
    static constexpr int DESC_COL = 38;
    int padding = std::max(1, DESC_COL - static_cast<int>(left.size()) - 2);
    std::cout << "  " << cli.color(left_color) << left << cli.color(ansi::RESET) << std::string(padding, ' ')
              << cli.color(ansi::DIM) << right << cli.color(ansi::RESET) << "\n";
}

static void print_usage(const SnapshotCli &cli)
{
    std::cout << "\n"
              << cli.color(ansi::BOLD) << "Hailo Snapshot Tool" << cli.color(ansi::RESET) << "\n\n"

              << cli.color(ansi::BOLD_YELLOW) << "COMMANDS" << cli.color(ansi::RESET) << "\n";
    print_help_line(cli, "snapshot [N] [stages] [--interval F]", "Capture pipeline snapshots", ansi::CYAN);
    print_help_line(cli, "list_stages", "Show available pipeline stages", ansi::CYAN);
    print_help_line(cli, "list_snapshots [N|--all]", "Show saved snapshots on disk", ansi::CYAN);
    print_help_line(cli, "clear", "Delete all saved snapshots", ansi::CYAN);
    print_help_line(cli, "help", "Show this help", ansi::CYAN);
    print_help_line(cli, "exit", "Exit this tool", ansi::CYAN);

    std::cout << "\n" << cli.color(ansi::BOLD_YELLOW) << "EXAMPLES" << cli.color(ansi::RESET) << "\n";
    print_help_line(cli, "snapshot", "1 frame, all stages", ansi::GREEN);
    print_help_line(cli, "snapshot 5", "5 consecutive frames", ansi::GREEN);
    print_help_line(cli, "snapshot 3 post_isp,dewarp", "3 frames, specific stages", ansi::GREEN);
    print_help_line(cli, "snapshot 10 --interval 30", "10 snapshots, every 30th frame", ansi::GREEN);
    print_help_line(cli, "snapshot 5 --interval 15 post_isp", "5 snapshots, every 15th frame", ansi::GREEN);
    print_help_line(cli, "list_snapshots 5", "Show last 5 snapshot sessions", ansi::GREEN);

    std::cout << "\n" << cli.color(ansi::BOLD_YELLOW) << "ENVIRONMENT" << cli.color(ansi::RESET) << "\n";
    print_help_line(cli, "MEDIALIB_SNAPSHOT_ENABLE=1", "Enable snapshot feature", ansi::CYAN);
    print_help_line(cli, "MEDIALIB_SNAPSHOT_PATH=<dir>", "Override output directory", ansi::CYAN);

    std::cout << "\n"
              << cli.color(ansi::DIM) << "Arrow keys for history, Tab to complete." << cli.color(ansi::RESET)
              << std::endl;
}

static void handle_help_command(SnapshotCli &cli, const std::vector<std::string> &)
{
    print_usage(cli);
}

static void handle_exit_command(SnapshotCli &cli, const std::vector<std::string> &)
{
    cli.shutdown();
}

void register_help_commands(SnapshotCli &cli)
{
    cli.register_command("help", handle_help_command);
    cli.register_command("exit", handle_exit_command);
    cli.register_command("quit", handle_exit_command);
}
