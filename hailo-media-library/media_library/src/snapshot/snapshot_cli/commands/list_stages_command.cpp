#include <string>
#include <vector>

#include "../snapshot_cli.hpp"

static void handle_list_stages_command(SnapshotCli &cli, const std::vector<std::string> &)
{
    if (cli.send_command_and_wait_response("list_stages"))
    {
        SnapshotCli::parse_stages_from_response(cli.m_received_response);
    }
}

void register_list_stages_command(SnapshotCli &cli)
{
    cli.register_command("list_stages", handle_list_stages_command);
}
