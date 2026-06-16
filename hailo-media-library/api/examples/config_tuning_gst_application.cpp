#include <stdlib.h>
#include <tl/expected.hpp>
#include <iostream>
#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>

#include "media_library/isp_manager.hpp"
#include "media_library/config_manager.hpp"
#include "media_library/utils.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/signal_utils.hpp"

constexpr std::string_view MEDIALIB_CONFIG_PATH =
    "/etc/imaging/cfg/medialib_configs/face_landmarks_medialib_config.json";

std::unique_ptr<IspManager> m_isp_manager;
std::unique_ptr<ConfigManagerInteractor> m_config_manager_interactor;

void cleanup_resources()
{
    std::cout << "Cleaning up resources..." << std::endl;
    m_isp_manager.reset();
    m_config_manager_interactor.reset();
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cout << "Usage: " << argv[0] << " <profile_name>" << std::endl;
        std::cout << "Example: " << argv[0] << " Daylight" << std::endl;
        return 1;
    }

    std::string profile_name = argv[1];
    std::cout << "Config tuning application for profile: " << profile_name << std::endl;

    // Register signal handler for graceful shutdown
    static signal_utils::SignalHandler signal_handler;
    signal_handler.register_signal_handler([](int signal) {
        std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
        cleanup_resources();
        exit(signal);
    });

    // Create ConfigManagerInteractor
    std::string medialib_config_path = std::string(MEDIALIB_CONFIG_PATH);
    std::cout << "Reading config from: " << medialib_config_path << std::endl;
    std::string medialib_config_string = read_string_from_file(medialib_config_path.c_str());
    if (medialib_config_string.empty())
    {
        std::cout << "Failed to read config file: " << medialib_config_path << std::endl;
        return 1;
    }

    std::cout << "Creating ConfigManagerInteractor..." << std::endl;
    auto config_interactor_exp = ConfigManagerInteractor::create(medialib_config_string);
    if (!config_interactor_exp.has_value())
    {
        std::cout << "Failed to create ConfigManagerInteractor" << std::endl;
        return 1;
    }
    m_config_manager_interactor = std::move(config_interactor_exp.value());

    std::cout << "Setting profile: " << profile_name << std::endl;
    media_library_return profile_ret = m_config_manager_interactor->switch_to_profile_by_name(profile_name);
    if (profile_ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to set profile to " << profile_name << std::endl;
        cleanup_resources();
        return 1;
    }

    // Create IspManager
    m_isp_manager = std::make_unique<IspManager>();

    // Get frontend config and set it on the ISP manager
    frontend_config_t frontend_config = m_config_manager_interactor->get_frontend_config();
    m_isp_manager->set_config_manager_interactor(m_config_manager_interactor.get(), true);
    if (!m_isp_manager->set_config(frontend_config))
    {
        std::cout << "Failed to set ISP manager config" << std::endl;
        cleanup_resources();
        return 1;
    }

    auto symlink_3aconfig_opt = m_config_manager_interactor->get_isp_3a_config_symlink_path();
    auto symlink_sensor_opt = m_config_manager_interactor->get_isp_sensor_symlink_path();
    if (symlink_3aconfig_opt.has_value())
    {
        std::filesystem::remove(symlink_3aconfig_opt.value());
    }
    if (symlink_sensor_opt.has_value())
    {
        std::filesystem::remove(symlink_sensor_opt.value());
    }

    std::cout << "Modifying ISP config files..." << std::endl;
    if (!m_isp_manager->modify_isp_config_files())
    {
        std::cout << "Failed to modify ISP config files" << std::endl;
        cleanup_resources();
        return 1;
    }

    std::cout << "ISP config files created successfully!" << std::endl;
    std::cout << "Application running. Press Ctrl+C to exit and clean up." << std::endl;

    // Main application loop
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    cleanup_resources();
    return 0;
}
