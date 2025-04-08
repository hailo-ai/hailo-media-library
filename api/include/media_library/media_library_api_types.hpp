#pragma once
#include "media_library/media_library_types.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/config_manager.hpp"
#include "media_library/utils.hpp"

#include <string>
#include <vector>
#include <map>
#include <variant>

#include <fstream>
#include <stdexcept>
#include <iostream>
#include <iterator>

struct ProfileConfig
{
    multi_resize_config_t multi_resize_config;
    ldc_config_t ldc_config;
    hailort_t hailort_config;
    isp_t isp_config;
    hdr_config_t hdr_config;
    denoise_config_t denoise_config;
    input_video_config_t input_config;
    std::map<output_stream_id_t, encoder_config_t> encoder_configs;
    isp_config_files_t isp_config_files;

    ProfileConfig()
        : multi_resize_config(), ldc_config(), hailort_config(), isp_config(), hdr_config(), denoise_config(),
          input_config(), encoder_configs(), isp_config_files()
    {
    }

    ProfileConfig &operator=(const profile_config_t &profile_conf)
    {
        multi_resize_config = profile_conf.multi_resize_config;
        ldc_config = profile_conf.ldc_config;
        hailort_config = profile_conf.hailort_config;
        isp_config = profile_conf.isp_config;
        hdr_config = profile_conf.hdr_config;
        denoise_config = profile_conf.denoise_config;
        input_config = profile_conf.input_config;
        isp_config_files = profile_conf.isp_config_files;

        ConfigManager config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_ENCODER);
        for (const auto &codec_config : profile_conf.codec_configs)
        {
            std::string codec_config_string = read_string_from_file(codec_config.config_path);
            encoder_config_t encoder_config;
            config_manager.config_string_to_struct<encoder_config_t>(codec_config_string, encoder_config);
            encoder_configs[codec_config.stream_id] = encoder_config;
            auto &config = encoder_configs[codec_config.stream_id];
            if (std::holds_alternative<jpeg_encoder_config_t>(config))
            {
                std::get<jpeg_encoder_config_t>(config).config_path = codec_config.config_path;
            }
            else if (std::holds_alternative<hailo_encoder_config_t>(config))
            {
                std::get<hailo_encoder_config_t>(config).config_path = codec_config.config_path;
            }
        }

        return *this;
    }

    EncoderType get_type(const output_stream_id_t &output_stream_id) const
    {
        const auto &config = encoder_configs.at(output_stream_id);
        if (std::holds_alternative<jpeg_encoder_config_t>(config))
        {
            return EncoderType::Jpeg;
        }
        else if (std::holds_alternative<hailo_encoder_config_t>(config))
        {
            return EncoderType::Hailo;
        }
        else
        {
            return EncoderType::None;
        }
    }

    // verify schema
    bool verify_profile_schema()
    {
        ConfigManager multi_resize_config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_MULTI_RESIZE);
        ConfigManager ldc_config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_LDC);
        ConfigManager hailort_config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_HAILORT);
        ConfigManager isp_config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_ISP);
        ConfigManager hdr_config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_HDR);
        ConfigManager denoise_config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_DENOISE);
        ConfigManager input_video_config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_INPUT_VIDEO);
        ConfigManager encoder_config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_ENCODER);
        ConfigManager isp_new_config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_ISP_CONFIG);

        return multi_resize_config_manager.validate_configuration(
                   multi_resize_config_manager.config_struct_to_string<multi_resize_config_t>(multi_resize_config)) ==
                   MEDIA_LIBRARY_SUCCESS &&
               ldc_config_manager.validate_configuration(
                   ldc_config_manager.config_struct_to_string<ldc_config_t>(ldc_config)) == MEDIA_LIBRARY_SUCCESS &&
               hailort_config_manager.validate_configuration(hailort_config_manager.config_struct_to_string<hailort_t>(
                   hailort_config)) == MEDIA_LIBRARY_SUCCESS &&
               isp_config_manager.validate_configuration(
                   isp_config_manager.config_struct_to_string<isp_t>(isp_config)) == MEDIA_LIBRARY_SUCCESS &&
               hdr_config_manager.validate_configuration(
                   hdr_config_manager.config_struct_to_string<hdr_config_t>(hdr_config)) == MEDIA_LIBRARY_SUCCESS &&
               denoise_config_manager.validate_configuration(
                   denoise_config_manager.config_struct_to_string<denoise_config_t>(denoise_config)) ==
                   MEDIA_LIBRARY_SUCCESS &&
               input_video_config_manager.validate_configuration(
                   input_video_config_manager.config_struct_to_string<input_video_config_t>(input_config)) ==
                   MEDIA_LIBRARY_SUCCESS &&
               isp_new_config_manager.validate_configuration(
                   isp_new_config_manager.config_struct_to_string<isp_config_files_t>(isp_config_files)) ==
                   MEDIA_LIBRARY_SUCCESS &&
               std::all_of(encoder_configs.begin(), encoder_configs.end(), [&](const auto &entry) {
                   return encoder_config_manager.validate_configuration(
                              encoder_config_manager.config_struct_to_string<encoder_config_t>(entry.second)) ==
                          MEDIA_LIBRARY_SUCCESS;
               });
    }
};

struct MediaLibraryConfig
{
    std::string default_profile;
    std::map<std::string, ProfileConfig> profiles;

    MediaLibraryConfig() : default_profile(), profiles()
    {
    }

    MediaLibraryConfig &operator=(const medialib_config_t &medialib_conf)
    {
        default_profile = medialib_conf.default_profile;

        ConfigManager config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_PROFILE);
        for (const auto &profile : medialib_conf.profiles)
        {
            std::string profile_config_string = read_string_from_file(profile.config_file);
            profile_config_t profile_config;
            config_manager.config_string_to_struct<profile_config_t>(profile_config_string, profile_config);
            profiles[profile.name] = profile_config;
        }

        return *this;
    }
};
