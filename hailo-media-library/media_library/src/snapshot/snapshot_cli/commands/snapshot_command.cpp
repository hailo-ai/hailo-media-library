#include <stddef.h>
#include <stdint.h>
#include <iostream>
#include <string>
#include <algorithm>
#include <exception>
#include <vector>

#include "../snapshot_cli.hpp"

static void handle_snapshot_command(SnapshotCli &cli, const std::vector<std::string> &args)
{
    uint32_t frame_count = 1;
    uint32_t interval = 1;
    bool has_explicit_frame_count = false;
    std::vector<std::string> stages;

    // Parse args manually to avoid CLI11's parse(vector&) stripping/reordering issues
    for (size_t i = 0; i < args.size(); i++)
    {
        if (args[i] == "--interval")
        {
            if (i + 1 >= args.size())
            {
                cli.print_error("--interval requires a value");
                return;
            }
            try
            {
                unsigned long parsed = std::stoul(args[i + 1]);
                if (parsed < 1 || parsed > SnapshotCli::MAX_FRAME_INTERVAL)
                {
                    cli.print_error("Interval must be between 1 and " +
                                    std::to_string(SnapshotCli::MAX_FRAME_INTERVAL));
                    return;
                }
                interval = static_cast<uint32_t>(parsed);
            }
            catch (const std::exception &)
            {
                cli.print_error("Invalid interval value: " + args[i + 1]);
                return;
            }
            i++;
        }
        else if (!has_explicit_frame_count)
        {
            try
            {
                unsigned long parsed = std::stoul(args[i]);
                if (parsed < 1 || parsed > SnapshotCli::MAX_FRAME_COUNT)
                {
                    cli.print_error("Frame count must be between 1 and " +
                                    std::to_string(SnapshotCli::MAX_FRAME_COUNT));
                    return;
                }
                frame_count = static_cast<uint32_t>(parsed);
                has_explicit_frame_count = true;
            }
            catch (const std::exception &)
            {
                stages.push_back(args[i]);
            }
        }
        else
        {
            stages.push_back(args[i]);
        }
    }

    if (interval > 1 && !has_explicit_frame_count)
    {
        cli.print_error("--interval requires an explicit frame count (e.g., snapshot 10 --interval 30)");
        return;
    }

    std::string command = "snapshot " + std::to_string(frame_count);
    for (const auto &stage : stages)
    {
        command += " " + stage;
    }
    if (interval > 1)
    {
        command += " --interval " + std::to_string(interval);
    }

    int timeout_sec = SnapshotCli::COMMAND_TIMEOUT_SEC;
    if (interval > 1 && frame_count > 1)
    {
        timeout_sec = std::max(SnapshotCli::COMMAND_TIMEOUT_SEC, static_cast<int>(frame_count * interval * 2));
        std::cout << cli.color(ansi::DIM) << "Capturing " << frame_count << " snapshots (every " << interval
                  << " frames)..." << cli.color(ansi::RESET) << std::endl;
    }

    cli.send_command_and_wait_response(command, timeout_sec);
}

void register_snapshot_command(SnapshotCli &cli)
{
    cli.register_command("snapshot", handle_snapshot_command);
    cli.register_completion("--interval");
}
