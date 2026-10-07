#include <tl/expected.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <optional>
#include <vector>

#include "media_library/media_library.hpp"
#include "common/common.hpp"
#include "media_library_types.hpp"

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

static constexpr const char *STATUS_OVERLAY_ID = "status";

void setup_osd_overlays(MediaLibraryPtr media_lib)
{
    const std::string font_path = "/usr/share/fonts/ttf/LiberationMono-Bold.ttf";
    const rgba_color_t white = {255, 255, 255, 255};
    const rgba_color_t black = {0, 0, 0, 255};

    auto profile_exp = media_lib->get_current_profile();
    if (!profile_exp.has_value())
    {
        std::cerr << "Failed to get current profile" << std::endl;
        return;
    }
    config_profile_t profile = profile_exp.value();

    auto status_overlay = std::make_shared<TextOverlay>();
    status_overlay->id = STATUS_OVERLAY_ID;
    status_overlay->x = 0.5f;
    status_overlay->y = 0.1f;
    status_overlay->label = "Iteration: 0";
    status_overlay->text_color = white;
    status_overlay->background_color = black;
    status_overlay->font_size = 40;
    status_overlay->line_thickness = 1;
    status_overlay->z_index = 1;
    status_overlay->font_path = font_path;
    status_overlay->angle = 0;
    status_overlay->rotation_alignment_policy = rotation_alignment_policy_t::CENTER;

    for (auto &[stream_id, stream_cfg] : profile.encoded_output_streams)
    {
        stream_cfg.osd.text_overlays.push_back(status_overlay);
    }

    if (media_lib->set_override_parameters(profile) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cerr << "Failed to apply OSD overlays via profile override" << std::endl;
    }
}

void update_osd_dynamic(MediaLibraryPtr media_lib, int iteration)
{
    auto profile_exp = media_lib->get_current_profile();
    if (!profile_exp.has_value())
        return;
    config_profile_t profile = profile_exp.value();

    const std::string new_label = "Iteration: " + std::to_string(iteration);
    for (auto &[stream_id, stream_cfg] : profile.encoded_output_streams)
    {
        for (auto &overlay : stream_cfg.osd.text_overlays)
        {
            if (overlay && overlay->id == STATUS_OVERLAY_ID)
            {
                overlay->label = new_label;
            }
        }
    }

    if (media_lib->set_override_parameters(profile) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cerr << "Failed to update status overlay via profile override" << std::endl;
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
