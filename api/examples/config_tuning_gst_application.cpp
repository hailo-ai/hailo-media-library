#include "media_library/media_library.hpp"
#include "media_library/utils.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/signal_utils.hpp"
#include <optional>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <string_view>
#include <tl/expected.hpp>

// Profile constants
constexpr std::string_view kDefaultProfile = "Daylight";
constexpr std::string_view kProfileDaylight = "Daylight";
constexpr std::string_view kProfileLowlight = "Lowlight";
constexpr std::string_view kProfileHDR = "High_Dynamic_Range";
constexpr std::string_view kProfileIR = "IR";
constexpr std::string_view kMedialibConfigPath = "/etc/imaging/cfg/medialib_configs/webserver_medialib_config.json";

// Mode mapping function
const std::unordered_map<std::string, std::string_view> &mode_map()
{
    static const std::unordered_map<std::string, std::string_view> profiles_map = {
        // daylight
        {"daylight", kProfileDaylight},
        {"lowlight", kProfileLowlight},
        {"hdr", kProfileHDR},
        {"high_dynamic_range", kProfileHDR},
        {"ir", kProfileIR},
    };
    return profiles_map;
}

MediaLibraryPtr m_media_lib;
std::optional<config_profile_t> m_user_profile;

std::optional<std::string> mode_to_profile(const std::string &input_mode)
{
    // Trim whitespace and check if empty
    std::string trimmed_mode = input_mode;
    trimmed_mode.erase(0, trimmed_mode.find_first_not_of(" \t\r\n"));
    trimmed_mode.erase(trimmed_mode.find_last_not_of(" \t\r\n") + 1);

    // If empty or only whitespaces, default to Daylight
    if (trimmed_mode.empty())
    {
        return std::string(kDefaultProfile);
    }

    // Convert to lowercase for case-insensitive comparison
    std::string lower_mode = trimmed_mode;
    std::transform(lower_mode.begin(), lower_mode.end(), lower_mode.begin(), ::tolower);

    const auto &map = mode_map();
    auto it = map.find(lower_mode);
    if (it != map.end())
    {
        return std::string(it->second);
    }

    // Unrecognized mode - return error
    return std::nullopt;
}

bool set_profile(const std::string &profile_name)
{
    media_library_return profile_ret = m_media_lib->set_profile(profile_name);
    if (profile_ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        if (profile_ret == media_library_return::MEDIA_LIBRARY_PROFILE_IS_RESTRICTED)
        {
            std::cout << "Profile is restricted at this moment, skipping" << std::endl;
        }
        else
        {
            std::cout << "Failed to set profile to " << profile_name << std::endl;
            return false;
        }
    }

    auto get_profile_exp = m_media_lib->get_profile(profile_name);
    if (!get_profile_exp.has_value())
    {
        std::cout << "Failed to get profile " << profile_name << std::endl;
        return false;
    }

    m_user_profile = get_profile_exp.value();

    return true;
}

void cleanup_resources()
{
    if (m_media_lib)
    {
        m_media_lib->m_frontend = nullptr;
        for (auto &encoder : m_media_lib->m_encoders)
        {
            encoder.second->stop();
        }
        m_media_lib->m_encoders.clear();
    }
    m_media_lib = nullptr;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cout << "Usage: " << argv[0] << " <mode>" << std::endl;
        std::cout << "Example modes: High_Dynamic_Range, Daylight, etc." << std::endl;
        return 1;
    }

    auto profile_opt = mode_to_profile(argv[1]);
    if (!profile_opt.has_value())
    {
        std::cout << "Error: Unrecognized mode '" << argv[1] << "'" << std::endl;
        std::cout << "Valid modes: daylight, lowlight, hdr, high_dynamic_range, ir" << std::endl;
        return 1;
    }

    std::string profile = profile_opt.value();
    std::cout << "Using profile: " << profile << std::endl;

    // register signal SIGINT and signal handler
    static signal_utils::SignalHandler signal_handler;
    signal_handler.register_signal_handler([](int signal) {
        std::cout << "\nTuning application received signal " << signal << ", shutting down gracefully..." << std::endl;
        cleanup_resources();
        exit(signal);
    });

    m_user_profile = std::nullopt;
    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library" << std::endl;
        return 1;
    }
    m_media_lib = media_lib_expected.value();

    std::string medialib_config_path = std::string(kMedialibConfigPath);

    std::string medialib_config_string = read_string_from_file(medialib_config_path.c_str());

    auto get_profile_exp = m_media_lib->get_current_profile();

    if (m_media_lib->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        cleanup_resources();
        return 1;
    }

    bool profile_ret = set_profile(profile);
    if (!profile_ret)
    {
        std::cout << "Failed to set profile to " << profile << std::endl;
        cleanup_resources();
        return 1;
    }

    // Main application loop
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    cleanup_resources();
    return 0;
}
