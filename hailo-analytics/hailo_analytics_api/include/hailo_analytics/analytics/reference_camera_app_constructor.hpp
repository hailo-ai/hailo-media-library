#pragma once

// general includes
#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <type_traits>

// third-party includes
#include <tl/expected.hpp>

// medialibrary includes
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "media_library/media_library.hpp"
#include "media_library/frontend.hpp"

// hailo analytics includes
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "hailo_analytics/pipeline/codecs/encoder_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

namespace hailo_analytics::analytics::app_constructor
{

// Type aliases for convenience
using EncoderStage = hailo_analytics::pipeline::codecs::EncoderStage;
using FrontendStage = hailo_analytics::pipeline::sources::FrontendStage;
using PipelinePtr = std::shared_ptr<hailo_analytics::pipeline::Pipeline>;
using MediaLibraryPtr = std::shared_ptr<MediaLibrary>;

enum CamAppReturnCode
{
    SUCCESS = 0,

    UNINITIALIZED = -1,
    UNSUPPORTED_CLASS_INHERITANCE = -2,
    CONFIG_FILE_DOES_NO_EXIST = -3,
    MEDIA_LIBRARY_INIT_FAILED = -4,
    FRONTEND_STAGE_CONFIG_FAILED = -5,
    FRONTEND_FAILED_TO_GET_STREAM_ID = 6,
    ENCODER_STAGE_CONFIG_FAILED = -7,
    APP_EXTENSION_REGITRATION_FAILED = -8,

    FAILED = -100,
};

struct UserDataBase
{
  protected:
    UserDataBase() = default;

  public:
    virtual ~UserDataBase() = default;

    virtual const char *type_name() const;

    // This is a base class for user data that can be extended by the user
    // to store any custom data needed for the application.
    // It can be used to pass data between different stages or components.

    // Example: User can create a derived class that contains additional data
    // and assign it to the AppConfigOverride and pass it to CameraAppConstructor's create
};

struct EncoderMetaInfo
{
    std::shared_ptr<EncoderStage> encoder_stage_ptr;
    uint32_t input_stream_width;
    uint32_t input_stream_height;
};

struct MediaStageComponents
{
    std::shared_ptr<FrontendStage> m_frontend_stage;
    std::map<output_stream_id_t, EncoderMetaInfo> m_encoder_stages;
    std::shared_ptr<UserDataBase> m_user_data;
};

struct AppConfigOverride
{
    // Media config file for overriding default settings
    // If empty, the default config path given from default_media_config() will be used instead
    std::string m_media_config_path;

    // Start profile name, if empty the default profile in the config file will be used
    std::string m_start_profile_name;

    // If sensor_config source type is set to APPSRC, this file will be used for frontend playback
    std::string m_appsrc_file_path;

    // User can extend this to store custom data, see UserDataBase for detail
    std::shared_ptr<UserDataBase> m_user_data;

    // Extendable in the future
};

class CameraAppConstructor; // Forward declaration

class CameraAppExtension
{
  public:
    virtual ~CameraAppExtension() = default;

    // Called when registered with CameraAppConstructor, can be used to ontain the app instance
    // on registration for some additional initialization of the service that inherit from this class
    virtual void on_registered(CameraAppConstructor &app);

  protected:
    CameraAppConstructor *m_app = nullptr;

    friend class CameraAppConstructor;
};

class CameraAppConstructor : public std::enable_shared_from_this<CameraAppConstructor>
{
  public:
    struct InitializerParams
    {
        // When non-null, this MediaLibrary is borrowed (not owned). The caller is
        // responsible for keeping it alive and releasing it after this app is stopped.
        MediaLibrary *media_library_component;
        std::string media_library_config_path;
        std::string media_library_profile_name;
        bool initialize_media_library_configuration;
        bool initialize_media_library_profile;

        InitializerParams();
    };

    virtual ~CameraAppConstructor();

    tl::expected<bool, CamAppReturnCode> start();

    tl::expected<bool, CamAppReturnCode> stop();

    tl::expected<bool, CamAppReturnCode> release();

    // Register an extension (can be called by the app or externally)
    void register_extension(std::shared_ptr<CameraAppExtension> ext);

    template <typename T> std::shared_ptr<T> get_extension()
    {
        for (const auto &ext : m_app_extensions)
        {
            if (!ext)
            {
                continue;
            }
            if (auto casted = std::dynamic_pointer_cast<T>(ext))
            {
                return casted;
            }
        }
        return nullptr;
    }

    template <typename T>
    static tl::expected<std::shared_ptr<T>, CamAppReturnCode> create(
        std::optional<AppConfigOverride> override_config = std::nullopt, InitializerParams params = InitializerParams())
    {
        if (!std::is_base_of<CameraAppConstructor, T>::value)
        {
            HAILO_ANALYTICS_LOG_ERROR("{} must inherit from AppConstructor", demangle(typeid(T).name()));
            return tl::unexpected(CamAppReturnCode::UNSUPPORTED_CLASS_INHERITANCE);
        }

        auto instance = std::shared_ptr<T>(new T());
        instance->m_config_override = override_config;
        auto result = instance->initialize(params);
        if (!result)
            return tl::unexpected(result.error());
        return instance;
    }

    tl::expected<std::string, CamAppReturnCode> get_main_stream_encoder_id();

    tl::expected<std::string, CamAppReturnCode> get_main_stream_frontend_output_id();

    template <typename T> static std::string get_default_media_config_path()
    {
        if (!std::is_base_of<CameraAppConstructor, T>::value)
        {
            HAILO_ANALYTICS_LOG_ERROR("{} must inherit from AppConstructor", demangle(typeid(T).name()));
            return "";
        }

        // Temporarily create an object to get the virtual config path
        T temp;
        return temp.default_media_config();
    }

    tl::expected<std::string, CamAppReturnCode> get_media_config_path();

    tl::expected<MediaLibraryPtr, CamAppReturnCode> get_media_library();

    tl::expected<PipelinePtr, CamAppReturnCode> get_pipeline();

  protected:
    CameraAppConstructor();

    virtual CamAppReturnCode register_app_extensions(std::shared_ptr<UserDataBase> user_data);

    virtual std::string default_media_config() const = 0;

    virtual std::string main_stream_encoder_id(const MediaStageComponents &components) const = 0;

    virtual std::string main_stream_frontend_output_id(const MediaStageComponents &components) const = 0;

    virtual tl::expected<PipelinePtr, CamAppReturnCode> build_pipeline(const MediaStageComponents &components) = 0;

  private:
    PipelinePtr m_pipeline;
    MediaLibraryPtr m_media_library;
    MediaStageComponents m_components;
    std::vector<std::shared_ptr<CameraAppExtension>> m_app_extensions;
    std::optional<AppConfigOverride> m_config_override;
    bool m_initialized;

    tl::expected<bool, CamAppReturnCode> initialize(CameraAppConstructor::InitializerParams params);
};

// Camera App Factory class
class CameraAppClassFactory
{
  public:
    using CreatorFunc = std::shared_ptr<CameraAppConstructor> (*)();

    void register_class(const std::string &name, CreatorFunc func);

    std::shared_ptr<CameraAppConstructor> create(const std::string &name) const;

    std::vector<std::string> get_registration_name_in_order();

    std::shared_ptr<CameraAppConstructor> create_first_registered() const;

  private:
    std::unordered_map<std::string, CreatorFunc> m_registry;
    std::vector<std::string> m_registration_order;
};

// Camera App Factory Class Registration helper
template <typename T> std::shared_ptr<CameraAppConstructor> create_cam_app_instance()
{
    return CameraAppConstructor::create<T>().value();
}

} // namespace hailo_analytics::analytics::app_constructor
