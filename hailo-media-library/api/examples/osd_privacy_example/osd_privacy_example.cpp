#include "media_library/examples_common.hpp"
#include "media_library/privacy_mask.hpp"
#include "media_library/privacy_mask_types.hpp"

bool add_static_privacy_masks(PrivacyMaskBlenderPtr privacy_mask_blender)
{
    polygon example_polygon;
    example_polygon.id = "privacy_mask1";
    example_polygon.vertices.push_back(vertex(125, 40));
    example_polygon.vertices.push_back(vertex(980, 40));
    example_polygon.vertices.push_back(vertex(1020, 600));
    example_polygon.vertices.push_back(vertex(1350, 920));
    example_polygon.vertices.push_back(vertex(750, 750));
    example_polygon.vertices.push_back(vertex(125, 920));
    example_polygon.vertices.push_back(vertex(250, 600));
    media_library_return ret = privacy_mask_blender->add_static_privacy_mask(example_polygon);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to add privacy_mask1" << std::endl;
        return false;
    }

    polygon example_polygon2;
    example_polygon2.id = "privacy_mask2";
    example_polygon2.vertices.push_back(vertex(2500, 70));
    example_polygon2.vertices.push_back(vertex(2980, 70));
    example_polygon2.vertices.push_back(vertex(2900, 550));
    example_polygon2.vertices.push_back(vertex(2723, 550));
    example_polygon2.vertices.push_back(vertex(2600, 120));
    ret = privacy_mask_blender->add_static_privacy_mask(example_polygon2);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to add privacy_mask2" << std::endl;
        return false;
    }

    polygon example_polygon3;
    example_polygon3.id = "privacy_mask3";
    example_polygon3.vertices.push_back(vertex(400, 3160));
    example_polygon3.vertices.push_back(vertex(-100, 1860));
    example_polygon3.vertices.push_back(vertex(900, 1860));
    ret = privacy_mask_blender->add_static_privacy_mask(example_polygon3);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to add privacy_mask3" << std::endl;
        return false;
    }

    polygon example_polygon4;
    example_polygon4.id = "privacy_mask4";
    example_polygon4.vertices.push_back(vertex(3500, 50));
    example_polygon4.vertices.push_back(vertex(3600, -50));
    example_polygon4.vertices.push_back(vertex(3900, 550));
    example_polygon4.vertices.push_back(vertex(3800, 650));
    ret = privacy_mask_blender->add_static_privacy_mask(example_polygon4);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to add privacy_mask4" << std::endl;
        return false;
    }

    return true;
}

void change_to_pixelization_and_back_to_color(PrivacyMaskBlenderPtr privacy_mask_blender)
{
    std::this_thread::sleep_for(std::chrono::seconds(2)); // sleep for 2 seconds
    std::cout << "changing privacy masks to pixelization" << std::endl;
    privacy_mask_blender->set_pixelization_size(60);

    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "changing size of pixelization" << std::endl;
    privacy_mask_blender->set_pixelization_size(10);

    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "changing privacy masks to color" << std::endl;
    privacy_mask_blender->set_color({23, 161, 231});
}

int update_privacy_masks(PrivacyMaskBlenderPtr privacy_mask_blender)
{
    std::cout << "Updating privacy mask" << std::endl;
    auto polygon_exp = privacy_mask_blender->get_static_privacy_mask("privacy_mask1");
    if (!polygon_exp.has_value())
    {
        std::cout << "Failed to get privacy mask with id 'privacy_mask1'" << std::endl;
        return 1;
    }

    polygon polygon1 = polygon_exp.value();
    polygon1.vertices[0].x = 600;
    polygon1.vertices[0].y = 120;
    privacy_mask_blender->set_static_privacy_mask(polygon1);

    change_to_pixelization_and_back_to_color(privacy_mask_blender);
    return 0;
}

media_library_return add_custom_overlays(std::shared_ptr<osd::Blender> blender)
{
    osd::CustomOverlay custom_overlay("custom_argb", 0.3, 0.5, 0.1, 0.1, 1, osd::custom_overlay_format::ARGB);
    media_library_return ret = blender->add_overlay(custom_overlay);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to add custom_argb overlay" << std::endl;
        return ret;
    }
    auto custom_expected = blender->get_overlay("custom_argb");
    auto existing_custom_overlay = std::static_pointer_cast<osd::CustomOverlay>(custom_expected.value());
    HailoMediaLibraryBufferPtr hailo_argb_buffer = existing_custom_overlay->get_buffer();
    void *plane0_userptr = hailo_argb_buffer->get_plane_ptr(0);
    for (size_t i = 0; i < hailo_argb_buffer->get_plane_size(0); i += 4)
    {
        ((char *)plane0_userptr)[i] = 0x80;     // Alpha: 80 (half opaque)
        ((char *)plane0_userptr)[i + 1] = 0x00; // Red: 00 (no intensity)
        ((char *)plane0_userptr)[i + 2] = 0x00; // Green: 00 (no intensity)
        ((char *)plane0_userptr)[i + 3] = 0xFF; // Blue: FF (full intensity)
    }

    std::cout << "Enable custom overlay" << std::endl;
    ret = blender->set_overlay_enabled("custom_argb", true);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to enable custom_argb overlay" << std::endl;
        return ret;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2)); // sleep for 2 seconds

    std::cout << "Disable custom overlay " << std::endl;
    ret = blender->set_overlay_enabled("custom_argb", false);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to disable custom_argb overlay" << std::endl;
        return ret;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2)); // sleep for 2 seconds

    std::cout << "Enable custom overlay" << std::endl;
    ret = blender->set_overlay_enabled("custom_argb", true);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to enable custom_argb overlay" << std::endl;
        return ret;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2)); // sleep for 2 seconds

    // add another custom overlay but with format A420
    osd::CustomOverlay custom_overlay2("custom_a420", 0.7, 0.7, 0.1, 0.1, 1, osd::custom_overlay_format::A420);
    ret = blender->add_overlay(custom_overlay2);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to add custom_a420 overlay" << std::endl;
        return ret;
    }
    auto custom_expected2 = blender->get_overlay("custom_a420");
    auto existing_custom_overlay2 = std::static_pointer_cast<osd::CustomOverlay>(custom_expected2.value());
    HailoMediaLibraryBufferPtr hailo_a420_buffer = existing_custom_overlay2->get_buffer();

    uint8_t blue_y = 29, blue_u = 255, blue_v = 107, blue_a = 128;
    memset(hailo_a420_buffer->get_plane_ptr(0), blue_y, hailo_a420_buffer->get_plane_size(0));
    memset(hailo_a420_buffer->get_plane_ptr(1), blue_u, hailo_a420_buffer->get_plane_size(1));
    memset(hailo_a420_buffer->get_plane_ptr(2), blue_v, hailo_a420_buffer->get_plane_size(2));
    memset(hailo_a420_buffer->get_plane_ptr(3), blue_a, hailo_a420_buffer->get_plane_size(3));

    std::cout << "Enable custom overlay" << std::endl;
    ret = blender->set_overlay_enabled("custom_a420", true);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to enable custom_a420 overlay" << std::endl;
        return ret;
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
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

    std::string medialib_config_string = read_string_from_file("/usr/bin/medialib_config.json");
    if (m_media_lib->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
        return 1;

    // Standard file setup
    auto streams = m_media_lib->m_frontend->get_outputs_streams();
    if (streams.has_value())
    {
        for (auto s : streams.value())
        {
            std::string output_file_path = OUTPUT_FILE(s.id);
            m_output_files[s.id].open(output_file_path.c_str(), std::ios::out | std::ios::binary | std::ios::app);
        }
    }
    subscribe_elements(m_media_lib);

    std::cout << "Starting frontend." << std::endl;
    if (m_media_lib->start_pipeline() != media_library_return::MEDIA_LIBRARY_SUCCESS)
        return 1;

    std::this_thread::sleep_for(std::chrono::seconds(2));

    if (add_custom_overlays(m_media_lib->m_encoders["sink0"]->get_osd_blender()) != MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to add custom overlays" << std::endl;
        m_media_lib->stop_pipeline();
        cleanup_resources();
        return 1;
    }

    PrivacyMaskBlenderPtr privacy_blender = m_media_lib->m_encoders["sink0"]->get_privacy_mask_blender();
    if (!add_static_privacy_masks(privacy_blender))
    {
        std::cout << "Failed to add static privacy masks" << std::endl;
        m_media_lib->stop_pipeline();
        cleanup_resources();
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(10));

    if (update_privacy_masks(privacy_blender) != 0)
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
