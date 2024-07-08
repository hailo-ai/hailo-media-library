#pragma once

#include <malloc.h>
#include "common/common.hpp"
#include <nlohmann/json.hpp>
#include <thread>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include "media_library/v4l2_ctrl.hpp"

#ifndef MEDIALIB_LOCAL_SERVER
#include "media_library/isp_utils.hpp"
#else
// functions so isp_utils wont break local compilcation
namespace isp_utils
{
    void set_default_configuration();
    void set_denoise_configuration();
    void set_backlight_configuration();
    void set_hdr_configuration();
}
#endif

using namespace isp_utils::ctrl;

namespace webserver
{
    namespace common
    {
        void update_3a_config(bool enabled);
        void update_3a_config(nlohmann::json config);
        nlohmann::json get_3a_config();

        class v4l2ControlHelper
        {
        private:
            struct min_max_isp_params
            {
                int32_t min;
                int32_t max;
                v4l2_ctrl_id ctrl_id;
            };

            static std::unordered_map<v4l2_ctrl_id, min_max_isp_params> m_min_max_isp_params;

        public:
            template <typename T>
            static T calculate_value_from_precentage(uint16_t precentage, v4l2_ctrl_id ctrl_id, T calib_var)
            {
                auto min_max_params = m_min_max_isp_params[ctrl_id];
                float val_after_formula;
                if (precentage >= 50)
                {
                    val_after_formula = (precentage - 50) / 50.0f * (min_max_params.max - calib_var) + calib_var;
                }
                else
                {
                    val_after_formula = (50 - precentage) / 50.0f * (min_max_params.min - calib_var) + calib_var;
                }

                return static_cast<T>(val_after_formula);
            }

            template <typename T>
            static uint16_t calculate_precentage_from_value(T value, v4l2_ctrl_id ctrl_id, T calib_var)
            {
                auto min_max_params = m_min_max_isp_params[ctrl_id];
                float precentage = 50;
                if (value > calib_var)
                {
                    precentage += ((value - calib_var) / static_cast<float>(min_max_params.max - calib_var) * 50.0f);
                }
                else
                {
                    precentage -= ((calib_var - value) / static_cast<float>(calib_var - min_max_params.min) * 50.0f);
                }

                return static_cast<uint16_t>(precentage);
            }
        };

        struct stream_params_t
        {
            uint16_t saturation;
            uint16_t brightness;
            uint16_t contrast;
            uint16_t sharpness;
        };

        class stream_isp_params_t
        {
        public:
            int32_t saturation = 0;
            int32_t brightness = 0;
            int32_t contrast = 0;
            uint16_t sharpness_down = 0;
            uint16_t sharpness_up = 0;

            stream_isp_params_t(int32_t saturation, int32_t brightness, int32_t contrast, uint16_t sharpness_down, uint16_t sharpness_up) : saturation(saturation), brightness(brightness), contrast(contrast), sharpness_down(sharpness_down), sharpness_up(sharpness_up) {}

            stream_isp_params_t from_stream_params(const stream_params_t &params)
            {
                int32_t v_brightness = v4l2ControlHelper::calculate_value_from_precentage<int32_t>(params.brightness, V4L2_CTRL_BRIGHTNESS, brightness);
                int32_t v_contrast = v4l2ControlHelper::calculate_value_from_precentage<int32_t>(params.contrast, V4L2_CTRL_CONTRAST, contrast);
                int32_t v_saturation = v4l2ControlHelper::calculate_value_from_precentage<int32_t>(params.saturation, V4L2_CTRL_SATURATION, saturation);
                int32_t v_sharpness_down = v4l2ControlHelper::calculate_value_from_precentage<int32_t>(params.sharpness, V4L2_CTRL_SHARPNESS_DOWN, sharpness_down);
                int32_t v_sharpness_up = v4l2ControlHelper::calculate_value_from_precentage<int32_t>(params.sharpness, V4L2_CTRL_SHARPNESS_UP, sharpness_up);

                return stream_isp_params_t(v_saturation, v_brightness, v_contrast, v_sharpness_down, v_sharpness_up);
            }

            stream_params_t to_stream_params(const stream_isp_params_t &params)
            {
                uint16_t p_brightness = v4l2ControlHelper::calculate_precentage_from_value<int32_t>(params.brightness, V4L2_CTRL_BRIGHTNESS, brightness);
                uint16_t p_contrast = v4l2ControlHelper::calculate_precentage_from_value<int32_t>(params.contrast, V4L2_CTRL_CONTRAST, contrast);
                uint16_t p_saturation = v4l2ControlHelper::calculate_precentage_from_value<int32_t>(params.saturation, V4L2_CTRL_SATURATION, saturation);
                uint16_t p_sharpness = v4l2ControlHelper::calculate_precentage_from_value<uint16_t>(params.sharpness_down, V4L2_CTRL_SHARPNESS_DOWN, sharpness_down);

                return stream_params_t{p_saturation, p_brightness, p_contrast, p_sharpness};
            }
        };

        class backlight_filter_t
        {
        public:
            uint16_t max;
            uint16_t min;

            backlight_filter_t(uint16_t max, uint16_t min) : max(max), min(min) {}

            backlight_filter_t from_precentage(uint16_t precentage)
            {
                auto new_max = v4l2ControlHelper::calculate_value_from_precentage<int32_t>(precentage, V4L2_CTRL_AE_WDR_VALUES, max);
                auto new_min = v4l2ControlHelper::calculate_value_from_precentage<int32_t>(precentage, V4L2_CTRL_AE_WDR_VALUES, min);
                return backlight_filter_t(new_max, new_min);
            }

            uint16_t to_precentage(const backlight_filter_t &filter)
            {
                return v4l2ControlHelper::calculate_precentage_from_value<int32_t>(filter.max, V4L2_CTRL_AE_WDR_VALUES, max);
            }

            static backlight_filter_t get_from_json()
            {
                uint16_t max;
                uint16_t min;
                nlohmann::json config = get_3a_config();

                auto root = config["root"];
                for (auto &obj : config["root"])
                {
                    if (obj["classname"] == "AdaptiveAe")
                    {
                        max = obj["wdrContrast.max"];
                        min = obj["wdrContrast.min"];
                        return backlight_filter_t(max, min);
                    }
                }
                return backlight_filter_t(0, 0);
            }
        };

        enum binning_mode_t
        {
            BINNING_MODE_OFF = 0,
            BINNING_MODE_4_BY_4 = 1,
            BINNING_MODE_9_BY_9 = 2,
            BINNING_MODE_16_BY_16 = 3,
            BINNING_MODE_MAX
        };

        enum tuning_profile_t
        {
            TUNING_PROFILE_DEFAULT = 0,
            TUNING_PROFILE_DENOISE = 1,
            TUNING_PROFILE_BACKLIGHT_COMPENSATION = 2,
            TUNING_PROFILE_HDR_FHD = 3,
            TUNING_PROFILE_HDR_4K = 4,
            TUNING_PROFILE_MAX
        };

        struct tuning_t
        {
            tuning_profile_t value;
        };

        enum powerline_frequency_t
        {
            POWERLINE_FREQUENCY_OFF = 0,
            POWERLINE_FREQUENCY_50 = 1,
            POWERLINE_FREQUENCY_60 = 2,
            POWERLINE_FREQUENCY_MAX
        };

        struct auto_exposure_t
        {
            bool enabled;
            uint16_t gain;
            uint16_t integration_time;
            uint16_t backlight;
        };

        struct wide_dynamic_range_t
        {
            uint16_t value;
        };

        enum auto_white_balance_profile
        {
            AUTO_WHITE_BALANCE_PROFILE_AUTO = -1,

            AUTO_WHITE_BALANCE_PROFILE_A = 0,
            AUTO_WHITE_BALANCE_PROFILE_D50 = 1,
            AUTO_WHITE_BALANCE_PROFILE_D65 = 2,
            AUTO_WHITE_BALANCE_PROFILE_D75 = 3,
            AUTO_WHITE_BALANCE_PROFILE_TL84 = 4,
            AUTO_WHITE_BALANCE_PROFILE_F12 = 5,
            AUTO_WHITE_BALANCE_PROFILE_CWF = 6,
            AUTO_WHITE_BALANCE_PROFILE_MAX
        };

        struct auto_white_balance_t
        {
            auto_white_balance_profile value;
        };

        NLOHMANN_JSON_SERIALIZE_ENUM(tuning_profile_t, {
                                                           {TUNING_PROFILE_DEFAULT, "Default"},
                                                           {TUNING_PROFILE_DENOISE, "Denoise"},
                                                           {TUNING_PROFILE_BACKLIGHT_COMPENSATION, "Backlight Compensation"},
                                                           {TUNING_PROFILE_HDR_FHD, "HDR (FHD)"},
                                                           {TUNING_PROFILE_HDR_4K, "HDR (4K)"},
                                                       })

        NLOHMANN_JSON_SERIALIZE_ENUM(powerline_frequency_t, {
                                                                {POWERLINE_FREQUENCY_OFF, "Off"},
                                                                {POWERLINE_FREQUENCY_50, "50"},
                                                                {POWERLINE_FREQUENCY_60, "60"},
                                                            })

        NLOHMANN_JSON_SERIALIZE_ENUM(binning_mode_t, {
                                                         {BINNING_MODE_OFF, "off"},
                                                         {BINNING_MODE_4_BY_4, "4x4"},
                                                         {BINNING_MODE_9_BY_9, "9x9"},
                                                         {BINNING_MODE_16_BY_16, "16x16"},
                                                     })
        NLOHMANN_JSON_SERIALIZE_ENUM(auto_white_balance_profile, {
                                                                     {AUTO_WHITE_BALANCE_PROFILE_AUTO, "auto"},
                                                                     {AUTO_WHITE_BALANCE_PROFILE_A, "A"},
                                                                     {AUTO_WHITE_BALANCE_PROFILE_D50, "D50"},
                                                                     {AUTO_WHITE_BALANCE_PROFILE_D65, "D65"},
                                                                     {AUTO_WHITE_BALANCE_PROFILE_D75, "D75"},
                                                                     {AUTO_WHITE_BALANCE_PROFILE_TL84, "TL84"},
                                                                     {AUTO_WHITE_BALANCE_PROFILE_F12, "F12"},
                                                                     {AUTO_WHITE_BALANCE_PROFILE_CWF, "CWF"},
                                                                 })

        void from_json(const nlohmann::json &json, stream_isp_params_t &params);
        void to_json(nlohmann::json &json, const stream_isp_params_t &params);
        void from_json(const nlohmann::json &json, stream_params_t &params);
        void to_json(nlohmann::json &json, const stream_params_t &params);
        void from_json(const nlohmann::json &json, auto_exposure_t &params);
        void to_json(nlohmann::json &json, const auto_exposure_t &params);
        void from_json(const nlohmann::json &json, wide_dynamic_range_t &params);
        void to_json(nlohmann::json &json, const wide_dynamic_range_t &params);
        void from_json(const nlohmann::json &json, auto_white_balance_t &params);
        void to_json(nlohmann::json &json, const auto_white_balance_t &params);
        void from_json(const nlohmann::json &json, tuning_t &params);
        void to_json(nlohmann::json &json, const tuning_t &params);

        template <typename T>
        std::vector<T> get_enum_values(T last_value, std::vector<T> excluded_values = {})
        {
            std::vector<T> values;
            for (int i = 0; i < static_cast<int>(last_value); i++)
            {
                T value = static_cast<T>(i);
                if (excluded_values.empty() || std::find(excluded_values.begin(), excluded_values.end(), value) == excluded_values.end())
                {
                    values.push_back(value);
                }
            }
            return values;
        }
    }
}