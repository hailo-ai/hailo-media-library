#include "../snapshot_cli.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include "env_vars.hpp"
#include "snapshot.hpp"

struct SnapshotFileInfo
{
    std::string stage;
    std::string resolution;
    std::string format;
    uintmax_t file_size;
};

static std::string get_snapshot_base_path()
{
    const char *env_path = std::getenv(MEDIALIB_SNAPSHOT_PATH_ENV_VAR);
    if (env_path != nullptr && env_path[0] != '\0')
    {
        std::string path = env_path;
        if (path.back() != '/')
        {
            path += '/';
        }
        return path;
    }
    return SnapshotManager::MEDIA_LIBRARY_PATH;
}

static std::string format_file_size(uintmax_t bytes)
{
    static constexpr double KB = 1024.0;
    static constexpr double MB = 1024.0 * 1024.0;

    if (bytes >= static_cast<uintmax_t>(MB))
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << (static_cast<double>(bytes) / MB) << " MB";
        return oss.str();
    }
    if (bytes >= static_cast<uintmax_t>(KB))
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << (static_cast<double>(bytes) / KB) << " KB";
        return oss.str();
    }
    return std::to_string(bytes) + " B";
}

static std::string format_extension(const std::string &ext)
{
    if (ext.empty() || ext[0] != '.')
    {
        return ext;
    }
    std::string result = ext.substr(1);
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

static std::string format_timestamp_dir(const std::string &dirname)
{
    if (dirname.size() < 19)
    {
        return dirname;
    }
    std::string result = dirname.substr(0, 10) + " ";
    result += dirname[11];
    result += dirname[12];
    result += ':';
    result += dirname[14];
    result += dirname[15];
    result += ':';
    result += dirname[17];
    result += dirname[18];
    return result;
}

static SnapshotFileInfo parse_snapshot_filename(const std::filesystem::directory_entry &entry)
{
    SnapshotFileInfo info;
    info.file_size = entry.file_size();

    std::string filename = entry.path().stem().string();
    std::string ext = entry.path().extension().string();
    info.format = format_extension(ext);

    auto last_underscore = filename.rfind('_');
    if (last_underscore != std::string::npos)
    {
        std::string resolution_part = filename.substr(last_underscore + 1);
        if (resolution_part.find('x') != std::string::npos)
        {
            info.resolution = resolution_part;
            info.stage = filename.substr(0, last_underscore);
        }
        else
        {
            info.stage = filename;
        }
    }
    else
    {
        info.stage = filename;
    }

    return info;
}

static std::vector<std::filesystem::directory_entry> collect_sessions(const std::string &base_path)
{
    std::vector<std::filesystem::directory_entry> sessions;
    for (const auto &entry : std::filesystem::directory_iterator(base_path))
    {
        if (entry.is_directory())
        {
            sessions.push_back(entry);
        }
    }

    std::sort(sessions.begin(), sessions.end(),
              [](const auto &a, const auto &b) { return a.path().filename() > b.path().filename(); });

    return sessions;
}

static void print_session(const SnapshotCli &cli, const std::filesystem::directory_entry &session)
{
    std::string dirname = session.path().filename().string();
    std::string timestamp = format_timestamp_dir(dirname);

    std::vector<SnapshotFileInfo> files;
    uintmax_t total_size = 0;

    for (const auto &file_entry : std::filesystem::directory_iterator(session.path()))
    {
        if (file_entry.is_regular_file())
        {
            auto info = parse_snapshot_filename(file_entry);
            total_size += info.file_size;
            files.push_back(std::move(info));
        }
    }

    std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.stage < b.stage; });

    std::cout << cli.color(ansi::BOLD_CYAN) << timestamp << cli.color(ansi::RESET) << "  " << cli.color(ansi::DIM)
              << "(" << files.size() << " files, " << format_file_size(total_size) << ")" << cli.color(ansi::RESET)
              << "\n";

    for (size_t i = 0; i < files.size(); i++)
    {
        const auto &file = files[i];
        bool is_last = (i == files.size() - 1);
        std::string connector = is_last ? unicode::TREE_CORNER : unicode::TREE_BRANCH;

        std::cout << "  " << cli.color(ansi::DIM) << connector << cli.color(ansi::RESET) << cli.color(ansi::CYAN)
                  << std::left << std::setw(28) << file.stage << cli.color(ansi::RESET) << std::setw(14)
                  << file.resolution << cli.color(ansi::DIM) << std::setw(8) << file.format
                  << format_file_size(file.file_size) << cli.color(ansi::RESET) << "\n";
    }
    std::cout << std::endl;
}

static void handle_list_snapshots_command(SnapshotCli &cli, const std::vector<std::string> &args)
{
    std::string base_path = get_snapshot_base_path();

    if (!std::filesystem::exists(base_path))
    {
        std::cout << cli.color(ansi::DIM) << "No snapshots found. Directory does not exist: " << base_path
                  << cli.color(ansi::RESET) << std::endl;
        return;
    }

    CLI::App app("list_snapshots");
    app.footer("");

    size_t count = SnapshotCli::DEFAULT_LIST_COUNT;
    bool show_all = false;

    app.add_option("count", count, "Number of sessions to show")->default_val(SnapshotCli::DEFAULT_LIST_COUNT);
    app.add_flag("--all", show_all, "Show all sessions");

    try
    {
        // CLI11 1.9's parse(vector&) strips the first element as a "program name",
        // so prepend a dummy element to avoid losing the actual first argument.
        std::vector<std::string> parse_args = {""};
        parse_args.insert(parse_args.end(), args.begin(), args.end());
        app.parse(parse_args);
    }
    catch (const CLI::ParseError &e)
    {
        cli.print_error(e.what());
        return;
    }

    size_t max_sessions = show_all ? 0 : count;

    auto sessions = collect_sessions(base_path);
    if (sessions.empty())
    {
        std::cout << cli.color(ansi::DIM) << "No snapshots found in " << base_path << cli.color(ansi::RESET)
                  << std::endl;
        return;
    }

    size_t show_count = (max_sessions > 0) ? std::min(max_sessions, sessions.size()) : sessions.size();

    std::cout << cli.color(ansi::BOLD) << "Snapshots" << cli.color(ansi::RESET) << " " << cli.color(ansi::DIM) << "("
              << base_path << ")" << cli.color(ansi::RESET) << "\n";

    if (show_count < sessions.size())
    {
        std::cout << cli.color(ansi::DIM) << "Showing " << show_count << " of " << sessions.size()
                  << " sessions (use --all to show all)" << cli.color(ansi::RESET) << "\n";
    }
    std::cout << std::endl;

    for (size_t s = 0; s < show_count; s++)
    {
        print_session(cli, sessions[s]);
    }
}

void register_list_snapshots_command(SnapshotCli &cli)
{
    cli.register_command("list_snapshots", handle_list_snapshots_command);
    cli.register_completion("--all");
}

static void handle_clear_command(SnapshotCli &cli, const std::vector<std::string> &)
{
    std::string base_path = get_snapshot_base_path();

    if (!std::filesystem::exists(base_path))
    {
        std::cout << cli.color(ansi::DIM) << "Nothing to clear." << cli.color(ansi::RESET) << std::endl;
        return;
    }

    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(base_path, ec))
    {
        std::filesystem::remove_all(entry.path(), ec);
        if (ec)
        {
            cli.print_error("Failed to remove " + entry.path().string() + ": " + ec.message());
            return;
        }
    }

    cli.print_success("Cleared " + base_path);
}

void register_clear_command(SnapshotCli &cli)
{
    cli.register_command("clear", handle_clear_command);
}
