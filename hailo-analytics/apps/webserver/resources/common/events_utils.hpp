#pragma once
#include <nlohmann/json.hpp>
#include <stdint.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <variant>

#include "media_library/media_library_api_types.hpp"
#include "common/common.hpp"
#include "common/logger_macros.hpp"

namespace webserver
{
namespace resources
{
enum EventPriority
{
    EVENT_PRIORITY_VERY_HIGH = 0,
    EVENT_PRIORITY_HIGH = 1,
    EVENT_PRIORITY_MEDIUM = 2,
    EVENT_PRIORITY_LOW = 3
};

// Add JSON serialization for EventPriority
NLOHMANN_JSON_SERIALIZE_ENUM(EventPriority, {{EventPriority::EVENT_PRIORITY_VERY_HIGH, "very_high"},
                                             {EventPriority::EVENT_PRIORITY_HIGH, "high"},
                                             {EventPriority::EVENT_PRIORITY_MEDIUM, "medium"},
                                             {EventPriority::EVENT_PRIORITY_LOW, "low"}});

enum class EventType
{
    CHANGED_RESOURCE_WEBPAGE,
    CHANGED_RESOURCE_CONFIG_MANAGER,
    CHANGED_RESOURCE_FRONTEND,
    CHANGED_RESOURCE_ENCODER,
    CHANGED_RESOURCE_OSD,
    CHANGED_RESOURCE_AI,
    CHANGED_RESOURCE_ISP,
    CHANGED_RESOURCE_PRIVACY_MASK,
    CHANGED_RESOURCE_WEBRTC,
    RESTART_FRONTEND,
    ENCODER_CHANGE,
    RESET_CONFIG,
    SWITCH_PROFILE,
    PROFILE_UPDATE,
    PROFILE_UPDATE_REQUEST,
    PIPELINE_READY,
    CHANGE_FRAMERATE,
    CHANGE_RESOLUTION,
    CHANGE_FLIP,
    CHANGE_ROTATION,
    CHANGE_DEWARP,
    CHANGE_VALVE,
    CHANGE_EIS,
    CHANGE_DIS,
    CHANGE_DIGITAL_ZOOM,
    CHANGE_DIGITAL_ZOOM_ROI,
    CHANGE_GRAYSCALE,
    RESET_ISP,
    UPDATE_BLENDER,
    THROTTLING_STATE_UPDATE,
};

NLOHMANN_JSON_SERIALIZE_ENUM(EventType, {{EventType::CHANGED_RESOURCE_WEBPAGE, "webpage"},
                                         {EventType::CHANGED_RESOURCE_FRONTEND, "frontend"},
                                         {EventType::CHANGED_RESOURCE_ENCODER, "encoder"},
                                         {EventType::CHANGED_RESOURCE_OSD, "osd"},
                                         {EventType::CHANGED_RESOURCE_AI, "ai"},
                                         {EventType::CHANGED_RESOURCE_ISP, "isp"},
                                         {EventType::CHANGED_RESOURCE_PRIVACY_MASK, "privacy_mask"},
                                         {EventType::CHANGED_RESOURCE_CONFIG_MANAGER, "config"},
                                         {EventType::CHANGED_RESOURCE_WEBRTC, "webrtc"},
                                         {EventType::RESTART_FRONTEND, "restart_frontend"},
                                         {EventType::ENCODER_CHANGE, "encoder_change"},
                                         {EventType::RESET_CONFIG, "reset_config"},
                                         {EventType::SWITCH_PROFILE, "switch_profile"},
                                         {EventType::CHANGE_FRAMERATE, "change_framerate"},
                                         {EventType::CHANGE_RESOLUTION, "change_resolution"},
                                         {EventType::CHANGE_FLIP, "change_flip"},
                                         {EventType::CHANGE_ROTATION, "change_rotation"},
                                         {EventType::CHANGE_DEWARP, "change_dewarp"},
                                         {EventType::CHANGE_VALVE, "change_valve"},
                                         {EventType::CHANGE_DIS, "change_dis"},
                                         {EventType::CHANGE_EIS, "change_eis"},
                                         {EventType::CHANGE_DIGITAL_ZOOM, "change_digital_zoom"},
                                         {EventType::CHANGE_DIGITAL_ZOOM_ROI, "change_digital_zoom_roi"},
                                         {EventType::CHANGE_GRAYSCALE, "change_grayscale"},
                                         {EventType::PROFILE_UPDATE, "profile_update"},
                                         {EventType::PROFILE_UPDATE_REQUEST, "profile_update_request"},
                                         {EventType::RESET_ISP, "reset_isp"},
                                         {EventType::UPDATE_BLENDER, "update_blender"},
                                         {EventType::THROTTLING_STATE_UPDATE, "throttling_state_update"}});

class ResourceState
{
  public:
    virtual ~ResourceState() = default;
};
template <typename T> class ShareValueState : public ResourceState
{
  public:
    T value;
    ShareValueState(T value) : value(value)
    {
    }
};
template <typename T> class ValueState : public ResourceState
{
  public:
    T value;
    ValueState(T value) : value(std::move(value))
    {
    }
};

template <typename... Ts> class ValuesState : public ResourceState
{
  public:
    std::tuple<Ts...> values;

    ValuesState(Ts... args) : values(std::move(args)...)
    {
    }
};
class ProfileNameState : public ValueState<std::string>
{
  public:
    using ValueState<std::string>::ValueState;
};

class ProfileTypeState : public ValueState<ProfileType>
{
  public:
    using ValueState<ProfileType>::ValueState;
};

class ProfileFPSState : public ValueState<uint32_t>
{
  public:
    using ValueState<uint32_t>::ValueState;
};

class ProfileResolutionState : public ValueState<std::string>
{
  public:
    using ValueState<std::string>::ValueState;
};

class EncoderState : public ValueState<std::string>
{
  public:
    using ValueState<std::string>::ValueState;
};

class ProfileFlipState : public ValueState<std::string>
{
  public:
    using ValueState<std::string>::ValueState;
};

class ProfileRotationState : public ValueState<std::string>
{
  public:
    using ValueState<std::string>::ValueState;
};

class ProfileDewarpState : public ValueState<bool>
{
  public:
    using ValueState<bool>::ValueState;
};

class ProfileValveState : public ValueState<bool>
{
  public:
    using ValueState<bool>::ValueState;
};

class ProfileDisState : public ValueState<bool>
{
  public:
    using ValueState<bool>::ValueState;
};
class ProfileEisState : public ValueState<bool>
{
  public:
    using ValueState<bool>::ValueState;
};

class PipelineTypeState : public ValueState<std::string>
{
  public:
    using ValueState<std::string>::ValueState;
};

class ProfileDigitalZoomState : public ValuesState<bool, uint32_t>
{
  public:
    ProfileDigitalZoomState(bool enable, uint32_t magnification);
    bool getEnable() const;
    uint32_t getMagnification() const;
};

struct ProfileStateData
{
    config_profile_t profile_config;
    ProfileType active;
    std::string active_profile_name;
    std::vector<ProfileType> supported_profiles;
};

class ProfileState : public ValueState<ProfileStateData>
{
  public:
    using ValueState<ProfileStateData>::ValueState;
};
class EmptyState : public ResourceState
{
  public:
    EmptyState() = default;
};
class ProfileDigitalZoomRoiState : public ValuesState<bool, uint32_t, double, double, double, double>
{
  public:
    ProfileDigitalZoomRoiState(bool enable, uint32_t magnification, double x, double y, double width, double height);
    bool getEnable() const;
    uint32_t getMagnification() const;
    double getX() const;
    double getY() const;
    double getWidth() const;
    double getHeight() const;
};

class ProfileGrayscaleState : public ValueState<bool>
{
  public:
    using ValueState<bool>::ValueState;
};

class MediaLibraryThrottlingState : public ValueState<media_library_throttling_state_t>
{
  public:
    using ValueState<media_library_throttling_state_t>::ValueState;
};

enum AiApplications
{
    AI_APPLICATION_DETECTION,
    AI_APPLICATION_DENOISE,
};

class AiResourceState : public ResourceState
{
  public:
    std::vector<AiApplications> enabled;
    std::vector<AiApplications> disabled;
};
using ResourceStateVariant =
    std::variant<std::shared_ptr<ResourceState>, std::shared_ptr<ProfileNameState>, std::shared_ptr<ProfileTypeState>,
                 std::shared_ptr<ProfileFPSState>, std::shared_ptr<ProfileResolutionState>,
                 std::shared_ptr<ProfileFlipState>, std::shared_ptr<ProfileRotationState>,
                 std::shared_ptr<ProfileDewarpState>, std::shared_ptr<ProfileValveState>,
                 std::shared_ptr<ProfileDisState>, std::shared_ptr<ProfileEisState>, std::shared_ptr<AiResourceState>,
                 std::shared_ptr<ProfileDigitalZoomState>, std::shared_ptr<ProfileDigitalZoomRoiState>,
                 std::shared_ptr<ProfileGrayscaleState>>;

class ResourceStateChangeNotification
{
  public:
    EventType event_type;
    ResourceStateVariant resource_state;

    template <typename T> std::shared_ptr<T> getResourceStateFromBase()
    {
        if (std::holds_alternative<std::shared_ptr<ResourceState>>(resource_state))
        {
            auto &baseState = std::get<std::shared_ptr<ResourceState>>(resource_state);
            auto state = std::dynamic_pointer_cast<T>(baseState);
            if (state)
            {
                return state;
            }
        }
        WEBSERVER_LOG_ERROR("Failed to cast resource state from base pointer to the requested type");
        throw std::runtime_error("Failed to cast resource state from base pointer to the requested type");
    }

    template <typename T> std::shared_ptr<T> getDirectResourceState()
    {
        if (std::holds_alternative<std::shared_ptr<T>>(resource_state))
        {
            return std::get<std::shared_ptr<T>>(resource_state);
        }
        WEBSERVER_LOG_ERROR("Resource state is not stored directly as the requested type");
        throw std::runtime_error("Resource state is not stored directly as the requested type");
    }

    template <typename T> bool isResourceStateOfType()
    {
        if (std::holds_alternative<std::shared_ptr<T>>(resource_state))
        {
            return true;
        }

        return false;
    }
};

typedef std::function<void(ResourceStateChangeNotification)> ResourceChangeCallback;

typedef struct
{
    ResourceChangeCallback callback;
    EventType event_type;
    EventPriority priority;
    std::string subscriber_id;
    bool async_send;
    uint64_t registration_id; // Unique ID for each callback registration to detect stale callbacks
} resource_callback_t;

} // namespace resources
} // namespace webserver
