#include <stdlib.h>
#include <tl/expected.hpp>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <functional>
#include <initializer_list>
#include <optional>

#include "common/common.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/signal_utils.hpp"
#include "media_library/frontend.hpp"
#include "media_library/media_library.hpp"
#include "cloexec_fstream.hpp"

static polygon make_polygon(const std::string &id, std::initializer_list<vertex> verts)
{
    polygon p;
    p.id = id;
    p.vertices.assign(verts.begin(), verts.end());
    return p;
}

// Helper: get current profile and a mutable reference to the primary stream's masking config
static bool apply_profile_update(MediaLibraryPtr media_lib, const std::function<void(privacy_mask_config_t &)> &mutate)
{
    auto profile_exp = media_lib->get_current_profile();
    if (!profile_exp.has_value())
        return false;
    config_profile_t profile = profile_exp.value();
    for (auto &[stream_id, stream_cfg] : profile.encoded_output_streams)
    {
        mutate(stream_cfg.masking);
    }
    return media_lib->set_override_parameters(profile) == media_library_return::MEDIA_LIBRARY_SUCCESS;
}

bool add_static_privacy_masks(MediaLibraryPtr media_lib)
{
    std::vector<polygon> masks_to_add = {
        make_polygon("privacy_mask1",
                     {{125, 40}, {980, 40}, {1020, 600}, {1350, 920}, {750, 750}, {125, 920}, {250, 600}}),
        make_polygon("privacy_mask2", {{2500, 70}, {2980, 70}, {2900, 550}, {2723, 550}, {2600, 120}}),
        make_polygon("privacy_mask3", {{400, 3160}, {-100, 1860}, {900, 1860}}),
        make_polygon("privacy_mask4", {{3500, 50}, {3600, -50}, {3900, 550}, {3800, 650}}),
    };

    bool ok = apply_profile_update(media_lib, [&](privacy_mask_config_t &masking) {
        if (!masking.static_privacy_mask_config.has_value())
            masking.static_privacy_mask_config = static_privacy_mask_config_t{true, {}};
        for (const auto &m : masks_to_add)
            masking.static_privacy_mask_config->masks.push_back(m);
    });
    if (!ok)
        std::cout << "Failed to add static privacy masks via profile override" << std::endl;
    return ok;
}

bool change_to_pixelization_and_back_to_color(MediaLibraryPtr media_lib)
{
    auto step = [&](const char *label, std::function<void(privacy_mask_config_t &)> mutate) -> bool {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cout << label << std::endl;
        if (!apply_profile_update(media_lib, mutate))
        {
            std::cout << "Failed: " << label << std::endl;
            return false;
        }
        return true;
    };

    return step("changing privacy masks to pixelization",
                [](privacy_mask_config_t &m) {
                    m.mask_type = PrivacyMaskType::PIXELIZATION;
                    m.pixelization_size = 60;
                }) &&
           step("changing size of pixelization",
                [](privacy_mask_config_t &m) {
                    m.mask_type = PrivacyMaskType::PIXELIZATION;
                    m.pixelization_size = 10;
                }) &&
           step("changing privacy masks to color", [](privacy_mask_config_t &m) {
               m.mask_type = PrivacyMaskType::COLOR;
               m.color_value = rgb_color_t{23, 161, 231};
           });
}

int update_privacy_masks(MediaLibraryPtr media_lib)
{
    std::cout << "Updating privacy mask" << std::endl;
    bool found = false;
    if (!apply_profile_update(media_lib, [&](privacy_mask_config_t &masking) {
            if (!masking.static_privacy_mask_config.has_value())
                return;
            for (auto &p : masking.static_privacy_mask_config->masks)
            {
                if (p.id == "privacy_mask1" && !p.vertices.empty())
                {
                    p.vertices[0].x = 600;
                    p.vertices[0].y = 120;
                    found = true;
                    return;
                }
            }
        }))
    {
        std::cout << "Failed to update privacy_mask1 via profile override" << std::endl;
        return 1;
    }
    if (!found)
    {
        std::cout << "Failed to find privacy mask with id 'privacy_mask1'" << std::endl;
        return 1;
    }

    return change_to_pixelization_and_back_to_color(media_lib) ? 0 : 1;
}

int main()
{
    static signal_utils::SignalHandler signal_handler;
    signal_handler.register_signal_handler([](int signal) {
        if (m_media_lib)
            m_media_lib->stop_pipeline();
        cleanup_resources();
        exit(signal);
    });

    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
        return 1;
    m_media_lib = media_lib_expected.value();

    if (m_media_lib->initialize(config_path) != media_library_return::MEDIA_LIBRARY_SUCCESS)
        return 1;

    examples::scale_osd_to_output_resolution(m_media_lib);

    // Standard file setup
    auto streams = m_media_lib->m_frontend->get_outputs_streams();
    if (streams.has_value())
    {
        for (auto s : streams.value())
        {
            std::string output_file_path = OUTPUT_FILE_WITH_PREFIX("osd_privacy", s.id);
            delete_output_file(output_file_path);
            m_output_files[s.id].open(output_file_path.c_str(), std::ios::out | std::ios::binary | std::ios::app);
        }
    }
    subscribe_elements(m_media_lib);

    std::cout << "Starting frontend." << std::endl;
    if (m_media_lib->start_pipeline() != media_library_return::MEDIA_LIBRARY_SUCCESS)
        return 1;

    std::this_thread::sleep_for(std::chrono::seconds(2));

    if (!add_static_privacy_masks(m_media_lib))
    {
        std::cout << "Failed to add static privacy masks" << std::endl;
        m_media_lib->stop_pipeline();
        cleanup_resources();
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(10));

    if (update_privacy_masks(m_media_lib) != 0)
    {
        m_media_lib->stop_pipeline();
        cleanup_resources();
        return 1;
    }

    std::cout << "Stopping pipeline." << std::endl;
    m_media_lib->stop_pipeline();
    cleanup_resources();
    return 0;
}
