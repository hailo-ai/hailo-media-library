#include <tl/expected.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>

#include "media_library/media_library.hpp"
#include "common/common.hpp"
#include "media_library/encoder.hpp"
#include "osd.hpp"
#include "osd_types.hpp"

struct Arguments
{
    std::string config_path;
    int duration_seconds;
};

bool parse_arguments(int argc, char *argv[], Arguments &args)
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <config.json> <duration_seconds>" << std::endl;
        return false;
    }

    args.config_path = argv[1];
    args.duration_seconds = std::atoi(argv[2]);

    if (args.duration_seconds <= 0)
    {
        std::cerr << "Invalid duration" << std::endl;
        return false;
    }

    return true;
}

void setup_osd_overlays(MediaLibraryPtr media_lib)
{
    std::string font_path = "/usr/share/fonts/ttf/LiberationMono-Bold.ttf";
    osd::rgba_color_t white = {255, 255, 255, 255};
    osd::rgba_color_t black = {0, 0, 0, 255};

    for (const auto &[stream_id, encoder] : media_lib->m_encoders)
    {
        auto blender = encoder->get_osd_blender();

        osd::TextOverlay status_overlay("status", 0.5, 0.1, "Iteration: 0", white, black, 40.0f, 1, 1, font_path, 0,
                                        osd::rotation_alignment_policy_t::CENTER);
        blender->add_overlay(status_overlay);
        blender->set_overlay_enabled("status", true);
    }
}

void update_osd_dynamic(MediaLibraryPtr media_lib, int iteration)
{
    for (const auto &[stream_id, encoder] : media_lib->m_encoders)
    {
        auto blender = encoder->get_osd_blender();

        auto status_exp = blender->get_overlay("status");
        if (status_exp.has_value())
        {
            auto status_overlay = std::static_pointer_cast<osd::TextOverlay>(status_exp.value());
            status_overlay->label = "Iteration: " + std::to_string(iteration);
            blender->set_overlay(*status_overlay);
        }
    }
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

    if (!examples::initialize_pipeline(resources, args.config_path, "osd_example"))
    {
        resources.cleanup();
        return 1;
    }

    setup_osd_overlays(resources.media_lib);

    std::cout << "Recording with OSD overlays for " << args.duration_seconds << " seconds" << std::endl;

    for (int i = 0; i < args.duration_seconds; i++)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        update_osd_dynamic(resources.media_lib, i);
    }

    std::cout << "Done" << std::endl;

    resources.cleanup();
    return 0;
}
