#include "vlm_frame_preprocessor.hpp"
#include "vlm_inference_manager.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#define TOKEN_PER_EVENT (5)

namespace fs = std::filesystem;

static std::vector<uint8_t> read_file(const std::string &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return {};
    }
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char *>(data.data()), size);
    return data;
}

static bool is_jpeg(const std::string &filename)
{
    auto ext = fs::path(filename).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".jpg" || ext == ".jpeg";
}

/// Load frames from a file path or directory. Returns JPEG byte vectors.
static std::vector<std::vector<uint8_t>> load_frames(const std::string &path, bool &use_video_mode)
{
    std::vector<std::vector<uint8_t>> frames;
    use_video_mode = false;

    if (path == "none" || path.empty())
    {
        return frames;
    }

    if (fs::is_directory(path))
    {
        // Collect all JPEG files, sorted by name
        std::vector<std::string> jpeg_files;
        for (const auto &entry : fs::directory_iterator(path))
        {
            if (entry.is_regular_file() && is_jpeg(entry.path().string()))
            {
                jpeg_files.push_back(entry.path().string());
            }
        }
        std::sort(jpeg_files.begin(), jpeg_files.end());

        for (const auto &file : jpeg_files)
        {
            auto data = read_file(file);
            if (!data.empty())
            {
                frames.push_back(std::move(data));
            }
        }

        // Multiple files → video mode
        if (frames.size() > 1)
        {
            use_video_mode = true;
        }
    }
    else if (fs::is_regular_file(path))
    {
        auto data = read_file(path);
        if (!data.empty())
        {
            frames.push_back(std::move(data));
        }
    }
    else
    {
        std::cerr << "Error: path does not exist: " << path << std::endl;
    }

    return frames;
}

// Run VlmFramePreprocessor over a batch of JPEGs, returning the preprocessed
// RGB byte buffers. On failure, prints the error and returns an empty vector.
static std::vector<std::vector<uint8_t>> preprocess_jpeg_batch(const VlmFramePreprocessor &preprocessor,
                                                               std::vector<std::vector<uint8_t>> jpegs)
{
    std::vector<std::vector<uint8_t>> rgb_frames;
    rgb_frames.reserve(jpegs.size());
    for (size_t index = 0; index < jpegs.size(); index++)
    {
        auto result = preprocessor.preprocess_jpeg(jpegs[index]);
        if (!result)
        {
            std::cerr << "Preprocess failed on frame " << index << ": " << result.error() << std::endl;
            return {};
        }
        rgb_frames.push_back(std::move(result.value()));
    }
    return rgb_frames;
}

// Parse a line into tokens, respecting quoted strings.
static std::vector<std::string> tokenize_command(const std::string &line)
{
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;

    while (stream >> std::ws)
    {
        if (stream.peek() == '"')
        {
            stream.get(); // consume opening quote
            std::getline(stream, token, '"');
        }
        else
        {
            stream >> token;
        }
        if (!token.empty())
        {
            tokens.push_back(token);
            token.clear();
        }
    }
    return tokens;
}

static void print_result(const InferenceResult &result)
{
    std::cout << "\nResponse: " << result.response << std::endl;
    std::cout << "Stats: TTFT=" << static_cast<int>(result.stats.ttft_ms) << "ms TPS=" << std::fixed
              << std::setprecision(1) << result.stats.tps << " Total=" << static_cast<int>(result.stats.total_ms)
              << "ms Tokens=" << result.stats.tokens_generated << " Ctx=" << result.stats.context_usage;
    if (result.stats.context_capacity > 0)
    {
        std::cout << "/" << result.stats.context_capacity;
    }
    std::cout << std::endl;
}

static constexpr size_t MAX_EVENTS = 5;

static const char *const kEventQuestionPrefix = "tell me if there is any activity of the following, answer yes or no "
                                                "separately for each activity listed as follow: ";

static bool read_line(const std::string &prompt_text, std::string &out)
{
    std::cout << prompt_text;
    std::cout.flush();
    return static_cast<bool>(std::getline(std::cin, out));
}

// Interactively build a list of 1-5 event descriptions.
static std::vector<std::string> prompt_for_events()
{
    std::vector<std::string> events;

    while (events.size() < MAX_EVENTS)
    {
        std::string description;
        if (!read_line("Event " + std::to_string(events.size() + 1) + " description: ", description))
        {
            break;
        }
        if (description.empty())
        {
            std::cerr << "Description cannot be empty. Re-enter." << std::endl;
            continue;
        }
        // Square brackets are reserved as event delimiters in the compiled
        // prompt — strip any the user typed so they don't confuse the model.
        description.erase(std::remove_if(description.begin(), description.end(),
                                         [](char character) { return character == '[' || character == ']'; }),
                          description.end());
        events.push_back(std::move(description));

        if (events.size() >= MAX_EVENTS)
        {
            std::cout << "Reached maximum of " << MAX_EVENTS << " events." << std::endl;
            break;
        }

        std::string answer;
        if (!read_line("Add another event? (y/n): ", answer))
        {
            break;
        }
        if (answer.empty() || (answer[0] != 'y' && answer[0] != 'Y'))
        {
            break;
        }
    }

    return events;
}

static std::string compile_event_prompt(const std::vector<std::string> &events)
{
    std::ostringstream stream;
    stream << kEventQuestionPrefix;
    for (size_t index = 0; index < events.size(); index++)
    {
        if (index > 0)
        {
            stream << " ";
        }
        stream << (index + 1) << ".[" << events[index] << "]";
    }
    return stream.str();
}

// Consume the next run of ASCII alpha characters at or after `scan`, lowercased,
// and return it. Advances `scan` past the run. Returns empty string if no alpha
// run is found before end of input.
static std::string consume_alpha_token(const std::string &response, size_t &scan)
{
    while (scan < response.size() && !std::isalpha(static_cast<unsigned char>(response[scan])))
    {
        scan++;
    }
    std::string token;
    while (scan < response.size() && std::isalpha(static_cast<unsigned char>(response[scan])))
    {
        token += static_cast<char>(std::tolower(static_cast<unsigned char>(response[scan])));
        scan++;
    }
    return token;
}

// Parse the model's yes/no response (e.g., "1. Yes\n2. No\n3. Yes.",
// "1. Yes, 2. No, 3. Yes", or a bare "No. The activity in the ...") into a
// strict JSON array of event verdicts.
//
// Two scans:
//   1. Numbered scan — matches "<digit>. Yes|No" (case-insensitive, separator-
//      agnostic), keyed by the digit. This is the model's normal output shape
//      when N > 1.
//   2. Bare-token fallback — when the numbered scan matches nothing (typical
//      with N = 1, where the model drops the "1. " prefix), scan the whole
//      response for standalone "yes"/"no" words and fill the still-unmatched
//      slots in order.
//
// Events the parser couldn't classify are emitted with detected=false AND
// an extra "matched": false field so downstream callers can detect gaps
// without crashing. The output always has exactly `expected_count` elements.
static std::string parse_yesno_to_json(const std::string &response, size_t expected_count)
{
    std::vector<bool> verdicts(expected_count, false);
    std::vector<bool> matched(expected_count, false);

    // ── Pass 1: numbered scan ("<digit>. Yes|No") ────────────────────────────
    for (size_t scan = 0; scan < response.size(); scan++)
    {
        if (!std::isdigit(static_cast<unsigned char>(response[scan])))
        {
            continue;
        }
        size_t digit_start = scan;
        while (scan < response.size() && std::isdigit(static_cast<unsigned char>(response[scan])))
        {
            scan++;
        }
        if (scan >= response.size() || response[scan] != '.')
        {
            continue;
        }
        int event_id = std::stoi(response.substr(digit_start, scan - digit_start));
        scan++; // skip '.'
        while (scan < response.size() && std::isspace(static_cast<unsigned char>(response[scan])))
        {
            scan++;
        }
        if (scan >= response.size())
        {
            break;
        }
        std::string token = consume_alpha_token(response, scan);
        if (event_id < 1 || static_cast<size_t>(event_id) > expected_count)
        {
            continue;
        }
        size_t slot = static_cast<size_t>(event_id - 1);
        if (token == "yes")
        {
            verdicts[slot] = true;
            matched[slot] = true;
        }
        else if (token == "no")
        {
            verdicts[slot] = false;
            matched[slot] = true;
        }
    }

    // ── Pass 2: fallback when pass 1 matched nothing ─────────────────────────
    // Typical for the single-event case where the model replies bare ("No.",
    // "Yes, the activity is clearly present", etc.). Fills unmatched slots in
    // order from the first N yes/no alpha tokens anywhere in the response.
    //
    // Gate on "all slots unmatched" rather than "any slot unmatched" to avoid
    // interfering with multi-event runs where only some numbered lines were
    // matched — in those cases prose elsewhere in the response could get
    // mis-assigned and hurt correctness.
    bool any_matched = false;
    for (size_t index = 0; index < expected_count; index++)
    {
        if (matched[index])
        {
            any_matched = true;
            break;
        }
    }
    if (!any_matched)
    {
        size_t slot_to_fill = 0;
        size_t scan = 0;
        while (scan < response.size() && slot_to_fill < expected_count)
        {
            std::string token = consume_alpha_token(response, scan);
            if (token.empty())
            {
                break;
            }
            if (token == "yes")
            {
                verdicts[slot_to_fill] = true;
                matched[slot_to_fill] = true;
                slot_to_fill++;
            }
            else if (token == "no")
            {
                verdicts[slot_to_fill] = false;
                matched[slot_to_fill] = true;
                slot_to_fill++;
            }
            // Other alpha tokens (e.g., "The", "activity") are ignored.
        }
    }

    std::ostringstream json;
    json << "[";
    for (size_t index = 0; index < expected_count; index++)
    {
        if (index > 0)
        {
            json << ", ";
        }
        json << "{\"id\": " << (index + 1) << ", \"detected\": " << (verdicts[index] ? "true" : "false");
        if (!matched[index])
        {
            json << ", \"matched\": false"; // signal that parser didn't find this id
        }
        json << "}";
    }
    json << "]";
    return json.str();
}

// Returns true if `s` is a non-empty string of ASCII decimal digits. Used to
// disambiguate an optional trailing max_tokens argument from a path/prompt.
static bool is_non_negative_integer(const std::string &s)
{
    if (s.empty())
    {
        return false;
    }
    for (char character : s)
    {
        if (!std::isdigit(static_cast<unsigned char>(character)))
        {
            return false;
        }
    }
    return true;
}

static void print_help()
{
    std::cout
        << "\nCommands:\n"
        << "  new                                              Create new session\n"
        << "  close <session_id>                               Close a session\n"
        << "  infer <session_id> <path> \"<prompt>\" [max]       Infer with session (new frames)\n"
        << "  infer <session_id> \"<prompt>\" [max]              Follow-up infer (text-only, reuses session)\n"
        << "  oneshot <path> \"<prompt>\" [max]                  One-shot inference (no session)\n"
        << "  events <path>                                    Event-detection on a JPEG dir (video mode, one-shot)\n"
        << "  context                                          Show context usage\n"
        << "  sessions                                         List active sessions\n"
        << "  help                                             Show this help\n"
        << "  quit                                             Exit\n"
        << "\n"
        << "  <path> = JPEG file, directory of JPEGs, or \"none\" for text-only\n"
        << "  Directory with 2+ JPEGs automatically uses video mode\n"
        << "  [max] = optional integer max_generated_tokens (default: 256)\n"
        << "  events <path> = directory of JPEGs required; interactively define up to " << MAX_EVENTS << " events\n"
        << std::endl;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        std::cerr << "Usage: " << argv[0] << " <hef_path> [group_id]" << std::endl;
        return 1;
    }

    VlmConfig config;
    config.hef_path = argv[1];
    if (argc == 3)
    {
        config.group_id = argv[2];
    }

    auto manager_exp = VlmInferenceManager::create(config);
    if (!manager_exp)
    {
        std::cerr << "Failed to create VLM manager: " << manager_exp.error() << std::endl;
        return 1;
    }
    auto manager = std::move(manager_exp.value());

    VlmFramePreprocessor preprocessor(manager->input_frame_height(), manager->input_frame_width(),
                                      manager->input_frame_channels());

    print_help();

    std::string line;
    while (true)
    {
        std::cout << "> ";
        if (!std::getline(std::cin, line))
        {
            break;
        }

        auto args = tokenize_command(line);
        if (args.empty())
        {
            continue;
        }

        const auto &cmd = args[0];

        if (cmd == "quit" || cmd == "exit" || cmd == "q")
        {
            break;
        }
        else if (cmd == "help" || cmd == "h")
        {
            print_help();
        }
        else if (cmd == "new")
        {
            auto result = manager->create_session();
            if (!result)
            {
                std::cerr << "Error: " << result.error() << std::endl;
            }
            else
            {
                std::cout << "Session " << result.value() << " created" << std::endl;
            }
        }
        else if (cmd == "close")
        {
            if (args.size() < 2)
            {
                std::cerr << "Usage: close <session_id>" << std::endl;
                continue;
            }
            uint32_t session_id = std::stoul(args[1]);
            auto result = manager->close_session(session_id);
            if (!result)
            {
                std::cerr << "Error: " << result.error() << std::endl;
            }
        }
        else if (cmd == "sessions")
        {
            auto ids = manager->list_sessions();
            if (ids.empty())
            {
                std::cout << "No active sessions" << std::endl;
            }
            else
            {
                std::cout << "Active sessions:";
                for (auto id : ids)
                {
                    std::cout << " " << id;
                }
                std::cout << std::endl;
            }
        }
        else if (cmd == "context")
        {
            auto usage = manager->get_context_usage();
            auto capacity = manager->max_context_capacity();
            if (usage && capacity)
            {
                std::cout << "Context: " << usage.value() << " / " << capacity.value() << " tokens" << std::endl;
            }
            else
            {
                std::cerr << "Error getting context info" << std::endl;
            }
        }
        else if (cmd == "infer")
        {
            // Accepted shapes:
            //   infer <id> "<prompt>"                     (3)  text-only follow-up
            //   infer <id> "<prompt>" <max>               (4)  text-only + max
            //   infer <id> <path> "<prompt>"              (4)  with frames
            //   infer <id> <path> "<prompt>" <max>        (5)  with frames + max
            if (args.size() < 3 || args.size() > 5)
            {
                std::cerr << "Usage: infer <session_id> [<path>] \"<prompt>\" [max_tokens]" << std::endl;
                continue;
            }

            uint32_t session_id = std::stoul(args[1]);

            std::string path;
            std::string prompt;
            uint32_t max_tokens = 0;
            if (args.size() == 3)
            {
                prompt = args[2];
            }
            else if (args.size() == 4)
            {
                // Disambiguate: trailing numeric arg → text-only + max; else → with frames
                if (is_non_negative_integer(args[3]))
                {
                    prompt = args[2];
                    max_tokens = static_cast<uint32_t>(std::stoul(args[3]));
                }
                else
                {
                    path = args[2];
                    prompt = args[3];
                }
            }
            else // size == 5
            {
                path = args[2];
                prompt = args[3];
                if (!is_non_negative_integer(args[4]))
                {
                    std::cerr << "Error: max_tokens must be a non-negative integer, got: " << args[4] << std::endl;
                    continue;
                }
                max_tokens = static_cast<uint32_t>(std::stoul(args[4]));
            }

            bool use_video_mode = false;
            std::vector<std::vector<uint8_t>> frames;
            if (!path.empty())
            {
                frames = load_frames(path, use_video_mode);
                if (path != "none" && frames.empty())
                {
                    std::cerr << "Error: no JPEG frames found at: " << path << std::endl;
                    continue;
                }
            }

            std::cout << "Loading " << frames.size() << " frame(s)"
                      << (use_video_mode   ? " (video mode)"
                          : frames.empty() ? " (text-only follow-up)"
                                           : " (image mode)")
                      << (max_tokens > 0 ? " max_tokens=" + std::to_string(max_tokens) : "") << "..." << std::endl;

            auto rgb_frames = preprocess_jpeg_batch(preprocessor, std::move(frames));
            // preprocess_jpeg_batch returns empty on error (with message already
            // printed). Text-only follow-ups also legitimately have no frames.
            if (rgb_frames.empty() && !path.empty() && path != "none")
            {
                continue;
            }

            InferenceRequest request;
            request.frames = std::move(rgb_frames);
            request.prompt = prompt;
            request.use_video_mode = use_video_mode;
            request.max_generated_tokens = max_tokens;

            auto result = manager->infer(session_id, request);
            if (!result)
            {
                std::cerr << "Error: " << result.error() << std::endl;
            }
            else
            {
                print_result(result.value());
            }
        }
        else if (cmd == "oneshot")
        {
            // Accepted shapes:
            //   oneshot <path> "<prompt>"          (3)
            //   oneshot <path> "<prompt>" <max>    (4)
            if (args.size() < 3 || args.size() > 4)
            {
                std::cerr << "Usage: oneshot <path> \"<prompt>\" [max_tokens]" << std::endl;
                continue;
            }

            const auto &path = args[1];
            const auto &prompt = args[2];

            uint32_t max_tokens = 0;
            if (args.size() == 4)
            {
                if (!is_non_negative_integer(args[3]))
                {
                    std::cerr << "Error: max_tokens must be a non-negative integer, got: " << args[3] << std::endl;
                    continue;
                }
                max_tokens = static_cast<uint32_t>(std::stoul(args[3]));
            }

            bool use_video_mode = false;
            auto frames = load_frames(path, use_video_mode);

            if (frames.empty())
            {
                std::cerr << "Error: no JPEG frames found at: " << path << std::endl;
                continue;
            }

            std::cout << "Loading " << frames.size() << " frame(s)"
                      << (use_video_mode ? " (video mode)" : " (image mode)") << " (one-shot)"
                      << (max_tokens > 0 ? " max_tokens=" + std::to_string(max_tokens) : "") << "..." << std::endl;

            auto rgb_frames = preprocess_jpeg_batch(preprocessor, std::move(frames));
            if (rgb_frames.empty())
            {
                continue;
            }

            InferenceRequest request;
            request.frames = std::move(rgb_frames);
            request.prompt = prompt;
            request.max_generated_tokens = max_tokens;
            request.use_video_mode = use_video_mode;

            auto result = manager->infer_oneshot(request);
            if (!result)
            {
                std::cerr << "Error: " << result.error() << std::endl;
            }
            else
            {
                print_result(result.value());
            }
        }
        else if (cmd == "events")
        {
            if (args.size() != 2)
            {
                std::cerr << "Usage: events <path>   (path must be a directory of JPEGs)" << std::endl;
                continue;
            }

            const auto &path = args[1];
            if (!fs::exists(path))
            {
                std::cerr << "Error: path does not exist: " << path << std::endl;
                continue;
            }
            if (!fs::is_directory(path))
            {
                std::cerr << "Error: path must be a directory of JPEGs: " << path << std::endl;
                continue;
            }

            bool ignored_video_flag = false;
            auto frames = load_frames(path, ignored_video_flag);
            if (frames.empty())
            {
                std::cerr << "Error: no JPEG frames found in directory: " << path << std::endl;
                continue;
            }

            auto events = prompt_for_events();
            if (events.empty())
            {
                std::cerr << "Error: no events entered, aborting." << std::endl;
                continue;
            }

            auto rgb_frames = preprocess_jpeg_batch(preprocessor, std::move(frames));
            if (rgb_frames.empty())
            {
                continue;
            }

            InferenceRequest request;
            request.frames = std::move(rgb_frames);
            request.use_video_mode = true;
            // No system role — this HEF's chat template collapses the task when
            // system is set. Question + list go together in the user message.
            request.prompt = compile_event_prompt(events);
            // Each "N. Yes\n" line is ~5 tokens; cap at TOKEN_PER_EVENT per event so the
            // model can't loop past the N we asked about.
            request.max_generated_tokens = events.size() * TOKEN_PER_EVENT;

            std::cout << "\nCompiled user prompt:\n" << request.prompt << "\n" << std::endl;
            std::cout << "Loading " << request.frames.size() << " frame(s) (video mode, events)..." << std::endl;

            auto result = manager->infer_oneshot(request);
            if (!result)
            {
                std::cerr << "Error: " << result.error() << std::endl;
            }
            else
            {
                print_result(result.value());
                std::cout << "Parsed JSON: " << parse_yesno_to_json(result.value().response, events.size())
                          << std::endl;
            }
        }
        else
        {
            std::cerr << "Unknown command: " << cmd << ". Type 'help' for available commands." << std::endl;
        }
    }

    std::cout << "Exiting." << std::endl;
    return 0;
}
