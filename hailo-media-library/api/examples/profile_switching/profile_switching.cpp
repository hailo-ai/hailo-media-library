#include "media_library/media_library.hpp"
#include "common/common.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>

struct Arguments
{
    std::string config_path;
    int num_iterations;
    std::vector<std::string> profile_names;
};

bool parse_arguments(int argc, char *argv[], Arguments &args)
{
    if (argc < 4)
    {
        std::cerr << "Usage: " << argv[0] << " <config.json> <num_iterations> <profile1> [profile2 ...]" << std::endl;
        return false;
    }

    args.config_path = argv[1];
    args.num_iterations = std::atoi(argv[2]);

    if (args.num_iterations <= 0)
    {
        std::cerr << "Invalid number of iterations" << std::endl;
        return false;
    }

    for (int i = 3; i < argc; i++)
    {
        args.profile_names.push_back(argv[i]);
    }

    return true;
}

size_t find_current_profile_index(MediaLibraryPtr media_lib, const std::vector<std::string> &profile_names)
{
    auto current_profile_exp = media_lib->get_current_profile();
    if (!current_profile_exp.has_value())
    {
        std::cout << "Failed to get current profile" << std::endl;
        return 0;
    }

    std::string current_name = current_profile_exp.value().name;
    for (size_t i = 0; i < profile_names.size(); i++)
    {
        if (profile_names[i] == current_name)
        {
            return i;
        }
    }

    return 0;
}

int main(int argc, char *argv[])
{
    Arguments args;
    if (!parse_arguments(argc, argv, args))
    {
        return 1;
    }

    examples::Resources resources;
    examples::setup_signal_handler(&resources);

    if (!examples::initialize_pipeline(resources, args.config_path, "profile_switching"))
    {
        resources.cleanup();
        return 1;
    }

    size_t current_index = find_current_profile_index(resources.media_lib, args.profile_names);

    std::cout << "Profile: " << args.profile_names[current_index] << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    for (int i = 0; i < args.num_iterations; i++)
    {
        current_index = (current_index + 1) % args.profile_names.size();

        std::cout << "Profile: " << args.profile_names[current_index] << std::endl;
        auto ret = resources.media_lib->set_profile(args.profile_names[current_index]);
        if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            std::cerr << "Failed to switch profile" << std::endl;
            resources.cleanup();
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    resources.cleanup();
    return 0;
}
