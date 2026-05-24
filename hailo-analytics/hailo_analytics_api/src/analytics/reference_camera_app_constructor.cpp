
#include <media_library/dis_common.h>
#include <media_library/encoder_config_types.hpp>
#include <media_library/media_library.hpp>
#include <media_library/media_library_api_types.hpp>
#include <tl/expected.hpp>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/analytics/reference_camera_app_constructor.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage_from_file.hpp"
#include "hailo_analytics/pipeline/codecs/encoder_stage.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"

namespace hailo_analytics::analytics::app_constructor
{

namespace StageNames
{
constexpr const char *frontend = "frontend_stage";
}

const char *UserDataBase::type_name() const
{
    return "UserDataBase";
}

CameraAppConstructor::InitializerParams::InitializerParams()
{
    media_library_component = nullptr;
    media_library_config_path = "";
    media_library_profile_name = "";
    initialize_media_library_configuration = true;
    initialize_media_library_profile = true;
}

void CameraAppExtension::on_registered(CameraAppConstructor & /*app*/)
{
}

CameraAppConstructor::CameraAppConstructor() = default;

CameraAppConstructor::~CameraAppConstructor()
{
}

tl::expected<bool, CamAppReturnCode> CameraAppConstructor::start()
{
    if (!m_initialized)
        return tl::unexpected(CamAppReturnCode::UNINITIALIZED);

    m_media_library->start_pipeline();
    m_pipeline->start();
    std::cout << "App started." << std::endl;

    return true;
}

tl::expected<bool, CamAppReturnCode> CameraAppConstructor::stop()
{
    if (!m_initialized)
        return tl::unexpected(CamAppReturnCode::UNINITIALIZED);

    m_pipeline->stop();
    m_media_library->stop_pipeline();
    auto status = m_media_library->shutdown();
    if (status != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("media library shutdown failed at {} with return status {}", __FUNCTION__, status);
    }

    return true;
}

tl::expected<bool, CamAppReturnCode> CameraAppConstructor::release()
{
    if (!m_initialized)
        return tl::unexpected(CamAppReturnCode::UNINITIALIZED);

    m_initialized = false;
    m_pipeline = nullptr;
    m_components.m_frontend_stage = nullptr;
    m_components.m_encoder_stages.clear();
    m_app_extensions.clear();
    m_media_library = nullptr;

    return true;
}

void CameraAppConstructor::register_extension(std::shared_ptr<CameraAppExtension> ext)
{
    ext->m_app = this;
    ext->on_registered(*this);
    m_app_extensions.push_back(std::move(ext));
}

CamAppReturnCode CameraAppConstructor::register_app_extensions(std::shared_ptr<UserDataBase> /*user_data*/)
{
    return CamAppReturnCode::SUCCESS;
}

tl::expected<std::string, CamAppReturnCode> CameraAppConstructor::get_main_stream_encoder_id()
{
    std::string encoder_stream_id = main_stream_encoder_id(m_components);
    if (encoder_stream_id.empty())
        return tl::unexpected(CamAppReturnCode::FAILED);
    else
        return encoder_stream_id;
}

tl::expected<std::string, CamAppReturnCode> CameraAppConstructor::get_main_stream_frontend_output_id()
{
    std::string output_stream_id = main_stream_frontend_output_id(m_components);
    if (output_stream_id.empty())
        return tl::unexpected(CamAppReturnCode::FAILED);
    else
        return output_stream_id;
}

tl::expected<MediaLibraryPtr, CamAppReturnCode> CameraAppConstructor::get_media_library()
{
    if (!m_initialized)
        return tl::unexpected(CamAppReturnCode::UNINITIALIZED);

    return m_media_library;
}

tl::expected<PipelinePtr, CamAppReturnCode> CameraAppConstructor::get_pipeline()
{
    if (!m_initialized)
        return tl::unexpected(CamAppReturnCode::UNINITIALIZED);

    return m_pipeline;
}

tl::expected<std::string, CamAppReturnCode> CameraAppConstructor::get_media_config_path()
{
    // Get media config path - get the path from subclass override by default, if config override is available then use
    // it instead
    std::string config_path = default_media_config();
    if (m_config_override && !m_config_override->m_media_config_path.empty())
        config_path = m_config_override->m_media_config_path;

    return config_path;
}

tl::expected<bool, CamAppReturnCode> CameraAppConstructor::initialize(CameraAppConstructor::InitializerParams params)
{
    std::string config_path =
        params.media_library_config_path.empty() ? get_media_config_path().value() : params.media_library_config_path;
    if (params.media_library_component)
    {
        m_media_library = std::shared_ptr<MediaLibrary>(params.media_library_component, [](MediaLibrary *) {});
    }
    else
    {
        // Initialized media library
        auto media_lib_expected = MediaLibrary::create();
        if (!media_lib_expected.has_value())
        {
            return tl::unexpected(CamAppReturnCode::MEDIA_LIBRARY_INIT_FAILED);
        }

        m_media_library = std::move(media_lib_expected.value());
    }

    if (params.initialize_media_library_configuration)
    {
        if (m_media_library->initialize(config_path) != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            HAILO_ANALYTICS_LOG_ERROR("media library init failed at {}", __FUNCTION__);
            return tl::unexpected(CamAppReturnCode::MEDIA_LIBRARY_INIT_FAILED);
        }
    }

    // Set the start profile of the config if config override is found, otherwise use the default from media config file
    if (params.initialize_media_library_profile && m_config_override &&
        !m_config_override->m_start_profile_name.empty())
    {
        m_media_library->set_profile(m_config_override->m_start_profile_name);
    }

    // Set custom user data if available
    if (m_config_override && m_config_override->m_user_data)
    {
        m_components.m_user_data = m_config_override->m_user_data;
    }

    // Register all app extension
    if (register_app_extensions(m_components.m_user_data) != CamAppReturnCode::SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("app extension registration failed at {}", __FUNCTION__);
        return tl::unexpected(CamAppReturnCode::APP_EXTENSION_REGITRATION_FAILED);
    }

    config_profile_t profile;
    auto current_config_profile = m_media_library->get_current_profile();
    if (!current_config_profile)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get current profile at {}", __FUNCTION__);
        return tl::unexpected(CamAppReturnCode::FAILED);
    }
    else
    {
        profile = current_config_profile.value();
    }

    if (profile.sensor_config.input_video.source_type == frontend_src_element_t::APPSRC)
    {
        // Create and configure frontend from file
        auto frontend_from_file = hailo_analytics::pipeline::sources::FrontendStageFromFileBuild::create()
                                      .set_stage_name(StageNames::frontend)
                                      .set_file_location(m_config_override->m_appsrc_file_path)
                                      .set_width(profile.sensor_config.input_video.resolution.width)
                                      .set_height(profile.sensor_config.input_video.resolution.height)
                                      .set_fps(profile.sensor_config.input_video.resolution.framerate)
                                      .set_loop_enabled_opt(true)
                                      .set_buffer_pool_size(20)
                                      .buildptr();
        m_components.m_frontend_stage = std::static_pointer_cast<FrontendStage>(frontend_from_file);
        hailo_analytics::pipeline::AppStatus frontend_config_status = frontend_from_file->configure(m_media_library);
        if (frontend_config_status != hailo_analytics::pipeline::AppStatus::SUCCESS)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to configure frontend from file at {}", __FUNCTION__);
            return tl::unexpected(CamAppReturnCode::FRONTEND_STAGE_CONFIG_FAILED);
        }

        // Make sure some feature is disabled when using frontend from file
        profile.iq_settings.dewarp.enabled = false;
        profile.stabilizer_settings.dis.enabled = false;
        profile.stabilizer_settings.eis.enabled = false;
        profile.stabilizer_settings.gyro.enabled = false;
        profile.application_settings.optical_zoom.enabled = false;
        if (m_media_library->set_override_parameters(profile) != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to set override parameters at {}", __FUNCTION__);
            return tl::unexpected(CamAppReturnCode::FRONTEND_STAGE_CONFIG_FAILED);
        }
    }
    else // frontend source from camera
    {
        // Create and configure frontend
        m_components.m_frontend_stage = hailo_analytics::pipeline::sources::FrontendStageBuild::create()
                                            .set_stage_name(StageNames::frontend)
                                            .buildptr();
        hailo_analytics::pipeline::AppStatus frontend_config_status =
            m_components.m_frontend_stage->configure(m_media_library);
        if (frontend_config_status != hailo_analytics::pipeline::AppStatus::SUCCESS)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to configure frontend at {}", __FUNCTION__);
            return tl::unexpected(CamAppReturnCode::FRONTEND_STAGE_CONFIG_FAILED);
        }
    }

    // Get frontend output streams
    auto streams = m_components.m_frontend_stage->get_outputs_streams();
    if (!streams.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get stream ids at {}", __FUNCTION__);
        return tl::unexpected(CamAppReturnCode::FRONTEND_FAILED_TO_GET_STREAM_ID);
    }

    // Get encoder configuration from profile
    auto expected_profile = m_media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get current profile at {}", __FUNCTION__);
        return tl::unexpected(CamAppReturnCode::ENCODER_STAGE_CONFIG_FAILED);
    }
    profile = expected_profile.value();
    auto encoder_config_map = profile.to_encoder_config_map();

    // Create and configure encoders
    for (const auto &[stream_id, encoder_config] : encoder_config_map)
    {
        std::cout << "Configuring encoder for stream id: " << stream_id << std::endl;
        // Create and configure encoder
        std::shared_ptr<EncoderStage> encoder_stage =
            hailo_analytics::pipeline::codecs::EncoderStageBuild::create().set_stage_name(stream_id).buildptr();
        m_components.m_encoder_stages[stream_id].encoder_stage_ptr = encoder_stage;

        hailo_analytics::pipeline::AppStatus enc_config_status = encoder_stage->configure(m_media_library, stream_id);
        if (enc_config_status != hailo_analytics::pipeline::AppStatus::SUCCESS)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to configure encoder at {}", __FUNCTION__);
            return tl::unexpected(CamAppReturnCode::ENCODER_STAGE_CONFIG_FAILED);
        }

        // Get the input size
        input_config_t input_stream;
        encoder_config_t config = encoder_config;
        if (auto jpeg_cfg = std::get_if<jpeg_encoder_config_t>(&config))
        {
            input_stream = jpeg_cfg->input_stream;
        }
        else if (auto hailo_cfg = std::get_if<hailo_encoder_config_t>(&config))
        {
            input_stream = hailo_cfg->input_stream;
        }
        else
        {
            HAILO_ANALYTICS_LOG_ERROR("Unsupported encoder config at {}", __FUNCTION__);
            return tl::unexpected(CamAppReturnCode::ENCODER_STAGE_CONFIG_FAILED);
        }

        m_components.m_encoder_stages[stream_id].input_stream_width = input_stream.width;
        m_components.m_encoder_stages[stream_id].input_stream_height = input_stream.height;
    }

    // Build the user pipeline
    auto build_result = build_pipeline(m_components);
    if (!build_result)
        return tl::unexpected(build_result.error());

    m_pipeline = *build_result;
    m_initialized = true;
    return m_initialized;
}

void CameraAppClassFactory::register_class(const std::string &name, CreatorFunc func)
{
    if (m_registry.find(name) == m_registry.end())
    {
        m_registry[name] = func;
        m_registration_order.push_back(name); // Record the order
    }
}

std::shared_ptr<CameraAppConstructor> CameraAppClassFactory::create(const std::string &name) const
{
    auto it = m_registry.find(name);
    if (it != m_registry.end())
    {
        return it->second();
    }
    return nullptr;
}

std::vector<std::string> CameraAppClassFactory::get_registration_name_in_order()
{
    return m_registration_order;
}

std::shared_ptr<CameraAppConstructor> CameraAppClassFactory::create_first_registered() const
{
    if (!m_registration_order.empty())
    {
        return create(m_registration_order.front());
    }
    return nullptr;
}

} // namespace hailo_analytics::analytics::app_constructor
