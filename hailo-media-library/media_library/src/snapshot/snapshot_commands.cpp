#include "snapshot.hpp"
#include "media_library_logger.hpp"

#include <algorithm>
#include <sstream>

#include "logger_macros.hpp"

#define MODULE_NAME LoggerType::Snapshot

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
    // Parse arguments: snapshot [frames_count] [--interval N] [stage1,stage2,... or stage1 stage2 ...]
    uint32_t frames_count = 1;
    uint32_t frame_interval = 1;
    std::string response;

    if (cmd_stream >> frames_count)
    {
        if (frames_count == 0)
        {
            return "Error: Frame count must be at least 1";
        }

        // Collect remaining tokens: --interval flag and stage names
        std::set<std::string> filtered_stages;
        std::string token;
        while (cmd_stream >> token)
        {
            if (token == "--interval")
            {
                if (!(cmd_stream >> frame_interval) || frame_interval == 0)
                {
                    return "Error: --interval requires a positive integer value";
                }
                continue;
            }

            std::istringstream token_stream(token);
            std::string stage;
            while (std::getline(token_stream, stage, ','))
            {
                stage.erase(0, stage.find_first_not_of(" \t"));
                stage.erase(stage.find_last_not_of(" \t") + 1);
                if (!stage.empty())
                {
                    filtered_stages.insert(stage);
                }
            }
        }

        uint32_t accepted_stages = request_snapshot(frames_count, filtered_stages, frame_interval);

        if (!filtered_stages.empty() && accepted_stages == 0)
        {
            return "Error: No matching stages found. Use 'list_stages' to see available stages.";
        }

        response = "Snapshot requested for " + std::to_string(frames_count) + " frames";
        if (!filtered_stages.empty())
        {
            response += " with " + std::to_string(accepted_stages) + " filtered stages";
        }
        if (frame_interval > 1)
        {
            response += " (every " + std::to_string(frame_interval) + " frames)";
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
