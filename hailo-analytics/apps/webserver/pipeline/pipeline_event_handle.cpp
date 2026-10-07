#include <media_library/dis_common.h>
#include <stdint.h>
#include <media_library/encoder_config_types.hpp>
#include <media_library/media_library.hpp>
#include <media_library/media_library_api_types.hpp>
#include <media_library/privacy_mask_types.hpp>
#include <nlohmann/json.hpp>
#include <tl/expected.hpp>
#include <stddef.h>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <algorithm>

#include "pipeline.hpp"
#include "resources/encoder.hpp"
#include "resources/osd.hpp"
#include "resources/privacy_mask.hpp"
#include "common/common.hpp"
#include "common/logger_macros.hpp"
#include "hailo_analytics/pipeline/routing/valve_stage.hpp"
#include "resources/common/event_bus.hpp"
#include "resources/common/events_utils.hpp"
#include "resources/common/repository.hpp"

using namespace webserver::pipeline;
using namespace webserver::resources;

// --- helpers (file-local) ---
template <typename T> static inline T clamp01(T v)
{
    return std::max<T>(0, std::min<T>(1, v));
}

// Range enforced by the deprecated PrivacyMaskConfigurer::set_pixelization_size.
static constexpr size_t PIXELIZATION_SIZE_MIN = 2;
static constexpr size_t PIXELIZATION_SIZE_MAX = 64;

// Bracket via Daylight only for transitions between Gen2 and Gen3.
static inline bool requires_daylight_bracket(ProfileType from, ProfileType to)
{
    return (from == ProfileType::AiIspGen2 && to == ProfileType::AiIspGen3) ||
           (from == ProfileType::AiIspGen3 && to == ProfileType::AiIspGen2);
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
    hailo_encoder_config_t hailo_encoder_config = read_stream_encoder_config(profile_config);
    hailo_encoder_config.input_stream.framerate = fps;
    profile_config.encoded_output_streams[m_stream_4k_name].encoding = hailo_encoder_config;
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
    hailo_encoder_config_t hailo_encoder_config = read_stream_encoder_config(profile_config);
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

media_library_return BasePipeline::apply_profile(ProfileType target)
{
    const std::string profile_name = this->get_profile_name_by_type(target);
    WEBSERVER_LOG_INFO("Pipeline: Applying profile: {} ({})", static_cast<int>(target), profile_name);
    return m_app_resources->media_library->set_profile(profile_name);
}

void BasePipeline::callback_handle_profile_switch(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_INFO("Pipeline: Handling switch profile event");
    auto state = notif.getDirectResourceState<ProfileTypeState>();
    const ProfileType from = m_current_profile_type;
    const ProfileType to = state->value;
    WEBSERVER_LOG_INFO("Pipeline: Switching profile {} -> {}", static_cast<int>(from), static_cast<int>(to));

    // Gen2 <-> Gen3 must transit through Daylight (product requirement: lowlight pairs are
    // not switched directly; the intermediate Daylight stop is hidden from the user).
    if (requires_daylight_bracket(from, to))
    {
        WEBSERVER_LOG_INFO("Pipeline: Lowlight pair switch — bracketing via Daylight");
        auto res = apply_profile(ProfileType::Daylight);
        if (res != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            WEBSERVER_LOG_ERROR("Failed to bracket-switch to Daylight {} error: {}",
                                static_cast<int>(ProfileType::Daylight), res);
            throw std::runtime_error("Failed to bracket-switch to Daylight before " +
                                     this->get_profile_name_by_type(to));
        }
        m_current_profile_type = ProfileType::Daylight;
    }

    auto res = apply_profile(to);
    const std::string profile_name = this->get_profile_name_by_type(to);
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

    m_current_profile_type = to;
    WEBSERVER_LOG_DEBUG("Pipeline: Switch profile event handled");

    m_resources.m_event_bus->notify(
        EventType::PROFILE_UPDATE,
        std::make_shared<ProfileState>(ProfileStateData{m_app_resources->media_library->get_current_profile().value(),
                                                        m_current_profile_type, profile_name, m_supported_profiles}));
}

hailo_encoder_config_t BasePipeline::get_encoder_config()
{
    return read_stream_encoder_config(read_current_profile());
}

void BasePipeline::callback_handle_encoder(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_DEBUG("Pipeline: Handling encoder resource state change");
    auto state = notif.getResourceStateFromBase<EncoderResource::EncoderResourceState>();

    config_profile_t current_profile = read_current_profile();
    hailo_encoder_config_t hailo_encoder_config = read_stream_encoder_config(current_profile);
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

void BasePipeline::callback_handle_osd(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_DEBUG("Pipeline: Handling OSD resource state change");
    auto state = notif.getResourceStateFromBase<OsdResource::OsdResourceState>();

    // Update the OSD config in the current profile and apply via set_override_parameters
    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    config_profile_t current_profile = expected_profile.value();
    current_profile.encoded_output_streams[m_stream_4k_name].osd = state->osd_config;

    media_library_return ret = m_app_resources->media_library->set_override_parameters(current_profile);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Failed to apply OSD config via profile override");
        throw std::runtime_error("Failed to apply OSD config via profile override");
    }
    WEBSERVER_LOG_DEBUG("Pipeline: OSD resource state change handled");
}

void BasePipeline::callback_handle_privacy_mask(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_DEBUG("Pipeline: Handling privacy mask resource state change");
    auto state = notif.getResourceStateFromBase<PrivacyMaskResource::PrivacyMaskResourceState>();

    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    config_profile_t current_profile = expected_profile.value();
    auto &masking = current_profile.encoded_output_streams[m_stream_4k_name].masking;

    // Update color or pixelization mode — pixelization range matches the
    // pre-deprecation PrivacyMaskConfigurer::set_pixelization_size check.
    if (state->color)
    {
        masking.mask_type = PrivacyMaskType::COLOR;
        masking.color_value = state->color.value();
    }
    else if (state->pixelization_size)
    {
        size_t pixelization = state->pixelization_size.value();
        if (pixelization < PIXELIZATION_SIZE_MIN || pixelization > PIXELIZATION_SIZE_MAX)
        {
            WEBSERVER_LOG_ERROR("Invalid pixelization size {}; must be in [{}, {}]", pixelization,
                                PIXELIZATION_SIZE_MIN, PIXELIZATION_SIZE_MAX);
            throw std::runtime_error("Invalid pixelization size");
        }
        masking.mask_type = PrivacyMaskType::PIXELIZATION;
        masking.pixelization_size = pixelization;
    }

    // Ensure the static privacy mask config exists before modifying masks
    if (!masking.static_privacy_mask_config.has_value())
    {
        masking.static_privacy_mask_config = static_privacy_mask_config_t{true, {}};
    }
    auto &static_masks = masking.static_privacy_mask_config.value().masks;

    // Add newly enabled masks — vertex-count and total-mask-count checks
    // mirror the pre-deprecation PrivacyMaskConfigurer::add_static_privacy_mask
    // guards. Invalid items are skipped with a warning instead of poisoning
    // the whole batch.
    for (const std::string &id : state->changed_to_enabled)
    {
        auto it = state->masks.find(id);
        if (it == state->masks.end())
            continue;
        if (it->second.vertices.size() > MAX_NUM_OF_VERTICES_IN_POLYGON)
        {
            WEBSERVER_LOG_WARN("Pipeline: skipping mask '{}': {} vertices exceeds max {}", id,
                               it->second.vertices.size(), MAX_NUM_OF_VERTICES_IN_POLYGON);
            continue;
        }
        if (static_masks.size() >= MAX_NUM_OF_STATIC_PRIVACY_MASKS)
        {
            WEBSERVER_LOG_WARN("Pipeline: skipping mask '{}': already at max {} static masks", id,
                               MAX_NUM_OF_STATIC_PRIVACY_MASKS);
            continue;
        }
        WEBSERVER_LOG_DEBUG("Pipeline: Adding privacy mask: {}", id);
        static_masks.push_back(it->second);
    }

    // Remove disabled masks
    for (const std::string &id : state->changed_to_disabled)
    {
        WEBSERVER_LOG_DEBUG("Pipeline: Removing privacy mask: {}", id);
        std::erase_if(static_masks, [&id](const polygon &p) { return p.id == id; });
    }

    // Update polygon vertices — preserves the pre-deprecation update-only
    // semantic of PrivacyMaskConfigurer::set_static_privacy_mask: missing
    // masks are skipped with a warning, not silently added.
    for (const std::string &mask_id : state->polygon_to_update)
    {
        auto it = state->masks.find(mask_id);
        if (it == state->masks.end())
            continue;
        if (it->second.vertices.size() > MAX_NUM_OF_VERTICES_IN_POLYGON)
        {
            WEBSERVER_LOG_WARN("Pipeline: skipping update for '{}': {} vertices exceeds max {}", mask_id,
                               it->second.vertices.size(), MAX_NUM_OF_VERTICES_IN_POLYGON);
            continue;
        }
        auto mask_it = std::find_if(static_masks.begin(), static_masks.end(),
                                    [&mask_id](const polygon &p) { return p.id == mask_id; });
        if (mask_it != static_masks.end())
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Updating privacy mask: {}", mask_id);
            *mask_it = it->second;
        }
        else
        {
            WEBSERVER_LOG_WARN("Pipeline: polygon_to_update for missing mask '{}'; ignoring", mask_id);
        }
    }

    // Delete polygons
    for (const std::string &mask_id : state->polygon_to_delete)
    {
        WEBSERVER_LOG_DEBUG("Pipeline: Deleting privacy mask: {}", mask_id);
        std::erase_if(static_masks, [&mask_id](const polygon &p) { return p.id == mask_id; });
    }

    // Apply the complete masking config via set_override_parameters
    media_library_return ret = m_app_resources->media_library->set_override_parameters(current_profile);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Failed to apply privacy mask config via profile override");
        throw std::runtime_error("Failed to apply privacy mask config via profile override");
    }
    WEBSERVER_LOG_DEBUG("Pipeline: Privacy mask resource state change handled");
}
