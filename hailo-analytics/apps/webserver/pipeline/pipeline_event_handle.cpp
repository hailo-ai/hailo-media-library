#include "pipeline.hpp"
#include "resources/configs.hpp"
#include "resources/encoder.hpp"
#include "resources/osd.hpp"
#include "resources/privacy_mask.hpp"

using namespace webserver::pipeline;
using namespace webserver::resources;

// --- helpers (file-local) ---
template <typename T> static inline T clamp01(T v)
{
    return std::max<T>(0, std::min<T>(1, v));
}

void BasePipeline::callback_handle_update_profile(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_DEBUG("Pipeline: Handling update profile event");
    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    config_profile_t current_profile = expected_profile.value();
    WEBSERVER_LOG_INFO("Pipeline: got event named {}", nlohmann::json(notif.event_type).dump());
    std::visit(
        [&](auto &&state) {
            using T = std::decay_t<decltype(state)>;
            if constexpr (std::is_same_v<T, std::shared_ptr<ProfileFPSState>>)
            {
                update_fps(state->value, current_profile);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileResolutionState>>)
            {
                update_resolution(state->value, current_profile);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileFlipState>>)
            {
                update_flip(state->value, current_profile);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileRotationState>>)
            {
                update_rotation(state->value, current_profile);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileDewarpState>>)
            {
                WEBSERVER_LOG_INFO("Updating dewarp to {}", state->value);
                current_profile.iq_settings.dewarp.enabled = state->value;
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileFreezeState>>)
            {
                if (m_app_resources->freeze_stage)
                {
                    WEBSERVER_LOG_INFO("Updating freeze to {}", state->value);
                    m_app_resources->freeze_stage->set_freeze(state->value);
                }
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileValveState>>)
            {
                if (m_app_resources->valve_stage)
                {
                    WEBSERVER_LOG_INFO("Updating valve to {}", state->value);
                    m_app_resources->valve_stage->set_valve(state->value);
                }
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileDisState>>)
            {
                if (current_profile.stabilizer_settings.eis.enabled)
                {
                    WEBSERVER_LOG_ERROR("Cannot set DIS when EIS is enabled");
                    throw std::runtime_error("Cannot set DIS when EIS is enabled");
                }
                WEBSERVER_LOG_INFO("Updating dis to {}", state->value);
                current_profile.stabilizer_settings.dis.enabled = state->value;
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileEisState>>)
            {
                if (current_profile.stabilizer_settings.dis.enabled)
                {
                    WEBSERVER_LOG_ERROR("Cannot set EIS when DIS is enabled");
                    throw std::runtime_error("Cannot set EIS when DIS is enabled");
                }
                WEBSERVER_LOG_INFO("Updating eis to {}", state->value);
                current_profile.stabilizer_settings.eis.enabled = state->value;
                current_profile.stabilizer_settings.gyro.enabled = state->value;
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileDigitalZoomState>>)
            {
                update_zoom(state, current_profile);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileDigitalZoomRoiState>>)
            {
                update_zoom_roi(state, current_profile);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileGrayscaleState>>)
            {
                current_profile.iq_settings.grayscale.enabled = state->value;
            }
            else
            {
                WEBSERVER_LOG_ERROR("Unknown state type");
                throw std::runtime_error("Unknown state type");
            }
        },
        notif.resource_state);

    media_library_return ret = m_app_resources->media_library->set_override_parameters(current_profile);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Failed to set profile");
        if (ret == media_library_return::MEDIA_LIBRARY_PROFILE_IS_RESTRICTED)
        {
            WEBSERVER_LOG_ERROR("Requested profile ({}) is restricted due to current thermal condition",
                                current_profile.name);
        }
        else
        {
            throw std::runtime_error("Failed to set profile");
        }
    }
}

void BasePipeline::update_zoom(std::shared_ptr<ProfileDigitalZoomState> state, config_profile_t &profile_config)
{
    WEBSERVER_LOG_INFO("Updating zoom to {}", state->getMagnification());
    profile_config.application_settings.digital_zoom.enabled = state->getEnable();
    profile_config.application_settings.digital_zoom.mode = digital_zoom_mode_t::DIGITAL_ZOOM_MODE_MAGNIFICATION;
    profile_config.application_settings.digital_zoom.magnification = state->getMagnification();
}

void BasePipeline::update_zoom_roi(std::shared_ptr<ProfileDigitalZoomRoiState> state, config_profile_t &profile_config)
{
    WEBSERVER_LOG_INFO("Updating relative zoom roi to x: {}, y: {}, width: {}, height: {}", state->getX(),
                       state->getY(), state->getWidth(), state->getHeight());
    // NOTE: user have to know the internal resolution that get into dewarp
    auto width = profile_config.sensor_config.input_video.resolution.width;
    auto height = profile_config.sensor_config.input_video.resolution.height;

    profile_config.application_settings.digital_zoom.enabled = state->getEnable();
    profile_config.application_settings.digital_zoom.mode = digital_zoom_mode_t::DIGITAL_ZOOM_MODE_ROI;
    profile_config.application_settings.digital_zoom.magnification = state->getMagnification();
    profile_config.application_settings.digital_zoom.roi.x = relative_to_absolut(state->getX(), width);
    profile_config.application_settings.digital_zoom.roi.y = relative_to_absolut(state->getY(), height);
    profile_config.application_settings.digital_zoom.roi.width = relative_to_absolut(state->getWidth(), width);
    profile_config.application_settings.digital_zoom.roi.height = relative_to_absolut(state->getHeight(), height);
    WEBSERVER_LOG_INFO("Updating absolute values zoom roi to x: {}, y: {}, width: {}, height: {}",
                       profile_config.application_settings.digital_zoom.roi.x,
                       profile_config.application_settings.digital_zoom.roi.y,
                       profile_config.application_settings.digital_zoom.roi.width,
                       profile_config.application_settings.digital_zoom.roi.height);
}

int BasePipeline::relative_to_absolut(float position, uint32_t resolution_axis_size)
{
    if (position > 1 || position < 0)
    {
        WEBSERVER_LOG_ERROR("position {} not between 0 and 1", position);
        throw std::runtime_error("position " + std::to_string(position) + " not between 0 and 1");
    }
    return static_cast<int>(static_cast<float>(position) * static_cast<float>(resolution_axis_size));
}

float BasePipeline::absolut_to_relative(int position, uint32_t resolution_axis_size)
{
    if (position > static_cast<int>(resolution_axis_size) || position < 0)
    {
        WEBSERVER_LOG_ERROR("position {} not between 0 and {}", position, resolution_axis_size);
        throw std::runtime_error("position " + std::to_string(position) + " not between 0 and " +
                                 std::to_string(resolution_axis_size));
    }
    return static_cast<float>(position) / static_cast<float>(resolution_axis_size);
}

int BasePipeline::scale(int position, int old_size, int new_size)
{
    if (position > old_size || position < 0)
    {
        WEBSERVER_LOG_ERROR("position {} not between 0 and {}", position, old_size);
        throw std::runtime_error("position " + std::to_string(position) + " not between 0 and " +
                                 std::to_string(old_size));
    }
    return relative_to_absolut(absolut_to_relative(position, old_size), new_size);
}

// TODO make sure FPS is not reseting stream
void BasePipeline::update_fps(uint32_t fps, config_profile_t &profile_config)
{
    if (fps < 1 || fps > 30)
    {
        WEBSERVER_LOG_ERROR("Framerate out of range: {}", fps);
        throw std::runtime_error("Framerate out of range");
    }
    for (auto &resolution : profile_config.application_settings.application_input_streams.resolutions)
    {
        resolution.framerate = fps;
    }
    // NOTE: waiting for encoder api from mosko
    encoder_config_t encoder = m_app_resources->media_library->m_encoders[m_stream_4k_name]->get_config();
    if (std::holds_alternative<jpeg_encoder_config_t>(encoder))
    {
        WEBSERVER_LOG_CRITICAL("JPEG encoder config is not supported in webserver");
        throw std::runtime_error("JPEG encoder config is not supported in webserver");
    }
    hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder);
    hailo_encoder_config.input_stream.framerate = fps;
}

void BasePipeline::update_resolution(const std::string &resolution, config_profile_t &profile_config)
{
    WEBSERVER_LOG_INFO("Updating resolution to {}", resolution);
    // NOTE: waiting for encoder api from mosko
    encoder_config_t encoder = profile_config.encoded_output_streams[m_stream_4k_name].encoding;
    if (std::holds_alternative<jpeg_encoder_config_t>(encoder))
    {
        WEBSERVER_LOG_CRITICAL("JPEG encoder config is not supported in webserver");
        throw std::runtime_error("JPEG encoder config is not supported in webserver");
    }
    uint32_t width = webserver::common::resolution_map.at(string_to_resolution(resolution)).first;
    uint32_t height = webserver::common::resolution_map.at(string_to_resolution(resolution)).second;

    // update profile
    profile_config.application_settings.application_input_streams.resolutions[0].dimensions.destination_width = width;
    profile_config.application_settings.application_input_streams.resolutions[0].dimensions.destination_height = height;

    auto &zoom_roi = profile_config.application_settings.digital_zoom.roi;
    if (zoom_roi.x > width)
    {
        WEBSERVER_LOG_INFO("Zoom ROI x {} is greater than width {}, clipping to width - 1", zoom_roi.x, width);
        zoom_roi.x = width - 1;
    }
    if (zoom_roi.y > height)
    {
        WEBSERVER_LOG_INFO("Zoom ROI y {} is greater than height {}, clipping to height - 1", zoom_roi.y, height);
        zoom_roi.y = height - 1;
    }
    if (zoom_roi.width + zoom_roi.x > width)
    {
        WEBSERVER_LOG_INFO("Zoom ROI width {} + x {} is greater than width {}, clipping to width - x", zoom_roi.width,
                           zoom_roi.x, width);
        zoom_roi.width = width - zoom_roi.x;
    }
    if (zoom_roi.height + zoom_roi.y > height)
    {
        WEBSERVER_LOG_INFO("Zoom ROI height {} + y {} is greater than height {}, clipping to height - y",
                           zoom_roi.height, zoom_roi.y, height);
        zoom_roi.height = height - zoom_roi.y;
    }

    rotation_angle_t angle = profile_config.application_settings.rotation.angle;
    if (isPortrait(angle))
    {
        std::swap(width, height);
    }
    // update encoder
    hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder);
    hailo_encoder_config.input_stream.width = width;
    hailo_encoder_config.input_stream.height = height;
    // NOTE: waiting for encoder api from mosko
    profile_config.encoded_output_streams[m_stream_4k_name].encoding = hailo_encoder_config;
}

void BasePipeline::update_flip(const std::string &flip, config_profile_t &profile_config)
{
    WEBSERVER_LOG_INFO("Updating flip to {}", flip);
    if (flip_string_map.find(flip) == flip_string_map.end())
    {
        WEBSERVER_LOG_ERROR("Invalid flip direction: {}", flip);
        throw std::runtime_error("Invalid flip direction: " + flip);
    }
    profile_config.application_settings.flip.enabled =
        (flip_string_map.at(flip) != flip_direction_t::FLIP_DIRECTION_NONE);
    profile_config.application_settings.flip.direction = flip_string_map.at(flip);
}

void BasePipeline::update_rotation(const std::string &rotation, config_profile_t &profile_config)
{
    WEBSERVER_LOG_INFO("Updating rotation to {}", rotation);
    if (rotation_string_map.find(rotation) == rotation_string_map.end())
    {
        WEBSERVER_LOG_ERROR("Invalid rotation angle: {}", rotation);
        throw std::runtime_error("Invalid rotation angle: " + rotation);
    }
    rotation_angle_t angle = profile_config.application_settings.rotation.angle;
    if (angle != rotation_string_map.at(rotation))
    {
        WEBSERVER_LOG_DEBUG("Rotation angle changed from {} to {}", angle, rotation);
    }
    // CONFIGURE ENCODER
    // NOTE: waiting for encoder api from mosko
    //  encoder_config_t& encoder = profile_config.encoder_configs[m_stream_4k_name];
    encoder_config_t encoder = m_app_resources->media_library->m_encoders[m_stream_4k_name]->get_config();
    if (std::holds_alternative<jpeg_encoder_config_t>(encoder))
    {
        WEBSERVER_LOG_CRITICAL("JPEG encoder config is not supported in webserver");
        throw std::runtime_error("JPEG encoder config is not supported in webserver");
    }
    hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder);
    auto currentAngle = rotation_string_map.at(rotation);
    auto profileAngle = angle;

    if (isPortrait(currentAngle) != isPortrait(profileAngle))
    {
        std::swap(hailo_encoder_config.input_stream.width, hailo_encoder_config.input_stream.height);
    }
    // NOTE: waiting for encoder api from mosko
    profile_config.encoded_output_streams[m_stream_4k_name].encoding = hailo_encoder_config;

    // CONFIGURE FRONTEND
    // TODO getting encoder from public encoder and putting it to profile is need to be fixed somehow by mosko
    bool enable = (rotation_string_map.at(rotation) != rotation_angle_t::ROTATION_ANGLE_0);
    profile_config.application_settings.rotation.enabled = enable;
    profile_config.application_settings.rotation.angle = rotation_string_map.at(rotation);
}

void BasePipeline::callback_handle_profile_switch(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_INFO("Pipeline: Handling switch profile event");
    auto state = notif.getDirectResourceState<ProfileTypeState>();
    WEBSERVER_LOG_INFO("Pipeline: Switching to profile: {}", static_cast<int>(state->value));
    m_current_profile_type = state->value;
    auto profile_name = this->get_profile_name_by_type(state->value);
    WEBSERVER_LOG_INFO("Pipeline: Resolved profile name: {}", profile_name);
    auto res = m_app_resources->media_library->set_profile(profile_name);
    if (res != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        if (res == MEDIA_LIBRARY_PROFILE_IS_RESTRICTED)
        {
            WEBSERVER_LOG_ERROR(
                "Failed to switch profile to: {} error: {} this profile is restricted in the current thermal condition",
                profile_name, res);
        }
        else
        {
            WEBSERVER_LOG_ERROR("Failed to switch profile: {} error: {}", profile_name, res);
        }

        throw std::runtime_error("Failed to switch profile: " + profile_name);
    }

    WEBSERVER_LOG_DEBUG("Pipeline: Switch profile event handled");

    m_resources.m_event_bus->notify(
        EventType::PROFILE_UPDATE,
        std::make_shared<ProfileState>(ProfileStateData{m_app_resources->media_library->get_current_profile().value(),
                                                        state->value, profile_name, m_supported_profiles}));
}

hailo_encoder_config_t BasePipeline::get_encoder_config()
{
    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    config_profile_t current_profile = expected_profile.value();

    // NOTE: waiting for encoder api from mosko
    //  encoder_config_t encoder_config = current_profile.m_encoders[m_stream_4k_name];
    encoder_config_t encoder_config =
        m_app_resources->media_library->m_encoders[m_stream_4k_name]
            ->get_config(); // TODO get the config form the profile(mosko need to be updated from the real struct)
    if (std::holds_alternative<jpeg_encoder_config_t>(encoder_config))
    {
        WEBSERVER_LOG_CRITICAL("JPEG encoder config is not supported in webserver");
        throw std::runtime_error("JPEG encoder config is not supported in webserver");
    }
    return std::get<hailo_encoder_config_t>(encoder_config);
}

void BasePipeline::callback_handle_encoder(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_DEBUG("Pipeline: Handling encoder resource state change");
    auto state = notif.getResourceStateFromBase<EncoderResource::EncoderResourceState>();

    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    config_profile_t current_profile = expected_profile.value();

    encoder_config_t encoder_config = m_app_resources->media_library->m_encoders[m_stream_4k_name]->get_config();
    if (std::holds_alternative<jpeg_encoder_config_t>(encoder_config))
    {
        WEBSERVER_LOG_CRITICAL("JPEG encoder config is not supported in webserver");
        throw std::runtime_error("JPEG encoder config is not supported in webserver");
    }
    hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder_config);
    state->value.fill_encoder_element_config(hailo_encoder_config);

    current_profile.encoded_output_streams[m_stream_4k_name].encoding = hailo_encoder_config;

    if (m_app_resources->media_library->set_override_parameters(current_profile) !=
        media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Failed to set profile");
        throw std::runtime_error("Failed to set profile");
    }
    WEBSERVER_LOG_DEBUG("Pipeline: Encoder resource state change handled");
}

std::shared_ptr<osd::Blender> BasePipeline::get_osd_blender()
{
    return m_app_resources->media_library->m_encoders[m_stream_4k_name]->get_osd_blender();
}

std::shared_ptr<PrivacyMaskBlender> BasePipeline::get_privacy_blender()
{
    return m_app_resources->media_library->m_encoders[m_stream_4k_name]->get_privacy_mask_blender();
}

void BasePipeline::callback_handle_osd(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_DEBUG("Pipeline: Handling OSD resource state change");

    std::shared_ptr<osd::Blender> osd_blender = get_osd_blender();
    auto state = notif.getResourceStateFromBase<OsdResource::OsdResourceState>();
    osd_blender->configure(state->osd_config);

    WEBSERVER_LOG_DEBUG("Pipeline: OSD resource state change handled");
}

void BasePipeline::callback_handle_privacy_mask(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_DEBUG("Pipeline: Handling privacy mask resource state change");
    auto state = notif.getResourceStateFromBase<PrivacyMaskResource::PrivacyMaskResourceState>();
    auto masks = state->masks;
    std::shared_ptr<PrivacyMaskBlender> privacy_blender = get_privacy_blender();
    if (state->color)
    {
        privacy_blender->set_color(state->color.value());
    }
    else if (state->pixelization_size)
    {
        privacy_blender->set_pixelization_size(state->pixelization_size.value());
    }
    for (std::string id : state->changed_to_enabled)
    {
        WEBSERVER_LOG_DEBUG("Pipeline: recived candidate mask to add: {} ", id);
        if (masks.find(id) != masks.end())
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Adding privacy mask: {}", id);
            privacy_blender->add_static_privacy_mask(masks[id]);
        }
    }
    for (std::string id : state->changed_to_disabled)
    {
        WEBSERVER_LOG_DEBUG("Pipeline: recived candidate mask to disable: {} ", id);
        if (masks.find(id) != masks.end())
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Removing privacy mask: {}", id);
            privacy_blender->remove_static_privacy_mask(id);
        }
    }
    for (std::string &mask : state->polygon_to_update)
    {
        WEBSERVER_LOG_DEBUG("Pipeline: recived candidate mask to update: {} ", mask);
        if (masks.find(mask) != masks.end())
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Updating privacy mask: {}", mask);
            privacy_blender->set_static_privacy_mask(masks[mask]);
        }
    }
    for (std::string &mask : state->polygon_to_delete)
    {
        WEBSERVER_LOG_DEBUG("Pipeline: recived candidate mask to delete: {} ", mask);
        if (masks.find(mask) != masks.end())
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Deleting privacy mask: {}", mask);
            privacy_blender->remove_static_privacy_mask(mask);
        }
    }
    WEBSERVER_LOG_DEBUG("Pipeline: Privacy mask resource state change handled");
}
