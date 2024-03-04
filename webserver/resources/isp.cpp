#include "resources/resources.hpp"
#include "common/common.hpp"
#include <iostream>

using namespace webserver::resources;
using namespace webserver::common;

IspResource::IspResource(std::shared_ptr<AiResource> ai_res) : Resource(), m_baseline_stream_params(0, 0, 0, 0, 0), m_baseline_wdr_params(0)
{
    m_v4l2 = std::make_unique<webserver::common::v4l2Control>(V4L2_DEVICE_NAME);
    m_ai_resource = ai_res;
    m_tuning_profile = {};
    m_ai_resource->subscribe_callback([this](ResourceStateChangeNotification notification)
                                      { this->on_ai_state_change(std::static_pointer_cast<AiResource::AiResourceState>(notification.resource_state)); });
}

void IspResource::on_ai_state_change(std::shared_ptr<AiResource::AiResourceState> state)
{
    bool ae_enabled;
    // enabled denoise
    if (std::find(state->enabled.begin(), state->enabled.end(), AiResource::AiApplications::AI_APPLICATION_DENOISE) != state->enabled.end())
    {
        override_file(ISP_DENOISE_3A_CONFIG, TRIPLE_A_CONFIG_PATH);
        override_file(ISP_DENOISE_SONY_CONFIG, SONY_CONFIG_PATH);
        m_tuning_profile.current = webserver::common::TUNING_PROFILE_DENOISE;
        ae_enabled = false;
    }

    // disabled denoise
    else if (std::find(state->disabled.begin(), state->disabled.end(), AiResource::AiApplications::AI_APPLICATION_DENOISE) != state->disabled.end())
    {
        auto file = (m_tuning_profile.fallback == webserver::common::TUNING_PROFILE_BACKLIGHT_COMPENSATION) ? ISP_BACKLIGHT_COMPENSATION_3A_CONFIG : ISP_DEFAULT_3A_CONFIG;
        override_file(file, TRIPLE_A_CONFIG_PATH);
        override_file(ISP_DEFAULT_SONY_CONFIG, SONY_CONFIG_PATH);
        m_tuning_profile.current = m_tuning_profile.fallback;
        ae_enabled = true;
    }
    else
    {
        return;
    }

    on_resource_change(std::make_shared<ResourceState>(IspResource::IspResourceState(true)));
    this->init(ae_enabled);
}

void IspResource::override_configurations()
{
    auto enabled_applications = m_ai_resource->get_enabled_applications();
    bool is_denoise_enabled = std::find(enabled_applications.begin(), enabled_applications.end(), webserver::resources::AiResource::AiApplications::AI_APPLICATION_DENOISE) != enabled_applications.end();

    std::string triple_a_config_path = is_denoise_enabled ? ISP_DENOISE_3A_CONFIG : ISP_BACKLIGHT_COMPENSATION_3A_CONFIG;
    std::string sony_config_path = is_denoise_enabled ? ISP_DENOISE_SONY_CONFIG : ISP_DEFAULT_SONY_CONFIG;

    override_file(triple_a_config_path, TRIPLE_A_CONFIG_PATH);
    override_file(sony_config_path, SONY_CONFIG_PATH);
    m_tuning_profile.current = is_denoise_enabled ? webserver::common::TUNING_PROFILE_DENOISE : webserver::common::tuning_profile_t::TUNING_PROFILE_BACKLIGHT_COMPENSATION;
    m_tuning_profile.fallback = webserver::common::TUNING_PROFILE_BACKLIGHT_COMPENSATION;
    this->init(false, false);
}

void IspResource::init(bool set_auto_exposure, bool set_auto_wb)
{
    if (set_auto_exposure)
    {
        // set auto exposure
        auto ae = this->get_auto_exposure();
        ae.enabled = true;
        this->set_auto_exposure(ae);
    }

    if (set_auto_wb)
    {
        // set auto white balance
        this->m_v4l2->v4l2_ext_ctrl_set<uint16_t>(V4L2_CTRL_AWB_MODE, 1);
    }

    std::cout << "enable 3a config" << std::endl;
    update_3a_config(true);

    // sleep 1 second to let values settle
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // disable to control values manually
    std::cout << "disable 3a config" << std::endl;
    update_3a_config(false);

    this->m_v4l2->v4l2_ext_ctrl_get<uint16_t>(webserver::common::V4L2_CTRL_SHARPNESS_DOWN, m_baseline_stream_params.sharpness_down);
    this->m_v4l2->v4l2_ext_ctrl_get<uint16_t>(webserver::common::V4L2_CTRL_SHARPNESS_UP, m_baseline_stream_params.sharpness_up);
    this->m_v4l2->v4l2_ctrl_get<int32_t>(webserver::common::V4L2_CTRL_BRIGHTNESS, m_baseline_stream_params.brightness);
    this->m_v4l2->v4l2_ctrl_get<int32_t>(webserver::common::V4L2_CTRL_SATURATION, m_baseline_stream_params.saturation);
    this->m_v4l2->v4l2_ctrl_get<int32_t>(webserver::common::V4L2_CTRL_CONTRAST, m_baseline_stream_params.contrast);
    this->m_v4l2->v4l2_ctrl_get<int16_t>(webserver::common::V4L2_CTRL_WDR_CONTRAST, m_baseline_wdr_params);

    std::cout << "Baseline stream params: " << std::endl;
    std::cout << "\tSharpness Down: " << m_baseline_stream_params.sharpness_down << std::endl;
    std::cout << "\tSharpness Up: " << m_baseline_stream_params.sharpness_up << std::endl;
    std::cout << "\tSaturation: " << m_baseline_stream_params.saturation << std::endl;
    std::cout << "\tBrightness: " << m_baseline_stream_params.brightness << std::endl;
    std::cout << "\tContrast: " << m_baseline_stream_params.contrast << std::endl;
    std::cout << "\tWDR: " << m_baseline_wdr_params << std::endl;
}

void IspResource::http_register(std::shared_ptr<httplib::Server> srv)
{
    srv->Get("/isp/refresh", [this](const httplib::Request &req, httplib::Response &res)
             { this->init(); });

    srv->Post("/isp/powerline_frequency", [this](const httplib::Request &req, httplib::Response &res)
              {
                std::string ret_msg;
                powerline_frequency_t freq;
                bool ret = http_request_extract_value<powerline_frequency_t>(req, "powerline_freq", freq, &ret_msg);
                if (!ret)
                {
                    res.set_content(ret_msg, "text/plain");
                    res.status = 400;
                    return;
                } 
                
                ret = m_v4l2->v4l2_ctrl_set<int>(V4L2_CTRL_POWERLINE_FREQUENCY, (uint16_t)freq);
                if (!ret)
                {
                    res.set_content("Failed to set powerline frequency", "text/plain");
                    res.status = 500;
                    return;
                } });

    srv->Get("/isp/powerline_frequency", [this](const httplib::Request &req, httplib::Response &res)
             {                
                int val;
                bool ret = m_v4l2->v4l2_ctrl_get<int>(V4L2_CTRL_POWERLINE_FREQUENCY, val);
                if (!ret)
                {
                    res.set_content("Failed to get powerline frequency", "text/plain");
                    res.status = 500;
                    return;
                }
                auto freq = (powerline_frequency_t)val;
                nlohmann::json j_out;
                j_out["powerline_freq"] = freq;
                res.set_content(j_out.dump(), "application/json"); });

    srv->Post("/isp/noise_reduction", [this](const httplib::Request &req, httplib::Response &res)
              {
                std::string ret_msg;
                int nr;
                bool ret = http_request_extract_value<int>(req, "noise_reduction", nr, &ret_msg);
                if (!ret)
                {
                    res.set_content(ret_msg, "text/plain");
                    res.status = 400;
                    return;
                } 
                if (nr > 100 || nr < 0)
                {
                    res.set_content("Invalid noise reduction value", "text/plain");
                    res.status = 400;
                    return;
                }
                ret = m_v4l2->v4l2_ctrl_set<int>(V4L2_CTRL_NOISE_REDUCTION, nr);
                if (!ret)
                {
                    res.set_content("Failed to set noise reduction", "text/plain");
                    res.status = 500;
                    return;
                } });

    srv->Post("/isp/wdr", [this](const httplib::Request &req, httplib::Response &res)
              {
                auto j_body = nlohmann::json::parse(req.body);
                wide_dynamic_range_t wdr = j_body.get<wide_dynamic_range_t>();
                auto val = v4l2ControlHelper::calculate_value_from_precentage<int32_t>(wdr.value, V4L2_CTRL_WDR_CONTRAST, m_baseline_wdr_params);
                std::cout << "Setting WDR to: " << val << std::endl;
                bool ret = m_v4l2->v4l2_ext_ctrl_set<int16_t>(V4L2_CTRL_WDR_CONTRAST, val);
                if (!ret)
                {
                    res.set_content("Failed to set WDR", "text/plain");
                    res.status = 500;
                    return;
                } 
                res.set_content(j_body.dump(), "application/json"); });

    srv->Get("/isp/wdr", [this](const httplib::Request &req, httplib::Response &res)
             {
                wide_dynamic_range_t wdr;
                int32_t val;
                bool ret = m_v4l2->v4l2_ctrl_get<int32_t>(V4L2_CTRL_WDR_CONTRAST, val);
                if (!ret)
                {
                    res.set_content("Failed to get WDR", "text/plain");
                    res.status = 500;
                    return;
                } 
                wdr.value = v4l2ControlHelper::calculate_precentage_from_value<int32_t>(val, V4L2_CTRL_WDR_CONTRAST, m_baseline_wdr_params);
                std::cout << "Got WDR value: " << wdr.value << std::endl;
                nlohmann::json j_out = wdr;
                res.set_content(j_out.dump(), "application/json"); });

    srv->Post("/isp/awb", [this](const httplib::Request &req, httplib::Response &res)
              {
                webserver::common::auto_white_balance_t awb;
                auto j_body = nlohmann::json::parse(req.body);
                try
                {
                    awb = j_body.get<webserver::common::auto_white_balance_t>();
                }
                catch (const std::exception &e)
                {
                    res.set_content("Failed to cast JSON to auto_white_balance_t", "text/plain");
                    res.status = 500;
                    return;
                }

                if (awb.value == AUTO_WHITE_BALANCE_PROFILE_AUTO)
                {
                    m_v4l2->v4l2_ext_ctrl_set<uint16_t>(V4L2_CTRL_AWB_MODE, 1);
                }
                else
                {
                    m_v4l2->v4l2_ext_ctrl_set<uint16_t>(V4L2_CTRL_AWB_MODE, 0);
                    m_v4l2->v4l2_ext_ctrl_set<uint16_t>(V4L2_CTRL_AWB_ILLUM_INDEX, awb.value);
                }

                nlohmann::json j_out = awb;
                res.set_content(j_out.dump(), "application/json"); });

    srv->Get("/isp/awb", [this](const httplib::Request &req, httplib::Response &res)
             {
                int32_t val;
                bool ret = m_v4l2->v4l2_ctrl_get<int32_t>(V4L2_CTRL_AWB_MODE, val);
                if (!ret)
                {
                    res.set_content("Failed to get AWB Mode", "text/plain");
                    res.status = 500;
                    return;
                } 
                if (val != 1) // manual mode, get profile
                {
                    ret = m_v4l2->v4l2_ctrl_get<int32_t>(V4L2_CTRL_AWB_ILLUM_INDEX, val);
                    if (!ret)
                    {
                        res.set_content("Failed to get AWB profile", "text/plain");
                        res.status = 500;
                        return;
                    } 
                }
                else // automatic mode
                {
                    val = -1;
                }
                webserver::common::auto_white_balance_t awb{(webserver::common::auto_white_balance_profile)val};
                nlohmann::json j_out = awb;
                res.set_content(j_out.dump(), "application/json"); });

    srv->Post("/isp/tuning", [this](const httplib::Request &req, httplib::Response &res)
              {
                webserver::common::tuning_t tuning;
                auto j_body = nlohmann::json::parse(req.body);
                try
                {
                    tuning = j_body.get<webserver::common::tuning_t>();
                }
                catch (const std::exception &e)
                {
                    res.set_content("Failed to cast JSON to tuning_t", "text/plain");
                    res.status = 500;
                    return;
                }

                m_tuning_profile.fallback = tuning.value;
                if (m_tuning_profile.current == webserver::common::TUNING_PROFILE_DENOISE)
                {
                    std::cout << "skipping file override, denoise is enabled" << std::endl;
                    nlohmann::json j_out = tuning;
                    res.set_content(j_out.dump(), "application/json");
                    return;
                }
                
                m_tuning_profile.current = tuning.value;
                auto file = (tuning.value == webserver::common::TUNING_PROFILE_BACKLIGHT_COMPENSATION) ? ISP_BACKLIGHT_COMPENSATION_3A_CONFIG : ISP_DEFAULT_3A_CONFIG;
                override_file(file, TRIPLE_A_CONFIG_PATH); 
                
                tuning.value = this->m_tuning_profile.current; 
                if (tuning.value == webserver::common::TUNING_PROFILE_DENOISE)
                {
                    tuning.value = this->m_tuning_profile.fallback; 
                }
                nlohmann::json j_tuning = tuning;
                res.set_content(j_tuning.dump(), "application/json"); });

    srv->Get("/isp/tuning", [this](const httplib::Request &req, httplib::Response &res)
             { 
                webserver::common::tuning_t tuning;
                tuning.value = this->m_tuning_profile.current; 
                if (tuning.value == webserver::common::TUNING_PROFILE_DENOISE)
                {
                    tuning.value = this->m_tuning_profile.fallback; 
                }
                nlohmann::json j_tuning = tuning;
                res.set_content(j_tuning.dump(), "application/json"); });

    srv->Get("/isp/stream_params", [this](const httplib::Request &req, httplib::Response &res)
             {
                stream_isp_params_t p(0, 0, 0, 0, 0);
                this->m_v4l2->v4l2_ext_ctrl_get<uint16_t>(webserver::common::V4L2_CTRL_SHARPNESS_DOWN, p.sharpness_down);
                this->m_v4l2->v4l2_ext_ctrl_get<uint16_t>(webserver::common::V4L2_CTRL_SHARPNESS_UP, p.sharpness_up);
                this->m_v4l2->v4l2_ctrl_get<int32_t>(webserver::common::V4L2_CTRL_BRIGHTNESS, p.brightness);
                this->m_v4l2->v4l2_ctrl_get<int32_t>(webserver::common::V4L2_CTRL_SATURATION, p.saturation);
                this->m_v4l2->v4l2_ctrl_get<int32_t>(webserver::common::V4L2_CTRL_CONTRAST, p.contrast);

                nlohmann::json j_out = m_baseline_stream_params.to_stream_params(p);
                std::cout << "Got stream params: " << j_out.dump() << std::endl;
                res.set_content(j_out.dump(), "application/json"); });

    srv->Post("/isp/stream_params", [this](const httplib::Request &req, httplib::Response &res)
              {
                std::string ret_msg;
                webserver::common::stream_params_t stream_params;
                auto j_body = nlohmann::json::parse(req.body);
                try
                {
                    stream_params = j_body.get<webserver::common::stream_params_t>();
                }
                catch (const std::exception &e)
                {
                    res.set_content("Failed to cast JSON to stream_params_t", "text/plain");
                    res.status = 500;
                    return;
                }
                auto isp_params = m_baseline_stream_params.from_stream_params(stream_params);

                this->m_v4l2->v4l2_ext_ctrl_set<int32_t>(V4L2_CTRL_SATURATION, isp_params.saturation);
                this->m_v4l2->v4l2_ext_ctrl_set<int32_t>(V4L2_CTRL_BRIGHTNESS, static_cast<int8_t>(isp_params.brightness));
                this->m_v4l2->v4l2_ext_ctrl_set<int32_t>(V4L2_CTRL_CONTRAST, isp_params.contrast);

                this->m_v4l2->v4l2_ext_ctrl_set<uint16_t>(V4L2_CTRL_EE_ENABLE, 0);

                this->m_v4l2->v4l2_ext_ctrl_set2<uint16_t>(V4L2_CTRL_SHARPNESS_DOWN, isp_params.sharpness_down);
                this->m_v4l2->v4l2_ext_ctrl_set2<uint16_t>(V4L2_CTRL_SHARPNESS_UP, isp_params.sharpness_up);

                this->m_v4l2->v4l2_ext_ctrl_set<uint16_t>(V4L2_CTRL_EE_ENABLE, 1);

                // cast out to json
                nlohmann::json j_out = stream_params;
                res.set_content(j_out.dump(), "application/json"); });

    srv->Post("/isp/auto_exposure", [this](const httplib::Request &req, httplib::Response &res)
              {
                auto j_body = nlohmann::json::parse(req.body);
                this->set_auto_exposure(j_body, res); });

    srv->Patch("/isp/auto_exposure", [this](const httplib::Request &req, httplib::Response &res)
               {
                auto params = this->get_auto_exposure();
                nlohmann::json j_params = params;
                auto j_body = nlohmann::json::parse(req.body);
                j_params.merge_patch(j_body);
                this->set_auto_exposure(j_params, res); });

    srv->Get("/isp/auto_exposure", [this](const httplib::Request &req, httplib::Response &res)
             {
                auto params = this->get_auto_exposure();
                nlohmann::json j_out = params;
                res.set_content(j_out.dump(), "application/json"); });
}

auto_exposure_t IspResource::get_auto_exposure()
{
    uint16_t enabled = 0;
    uint16_t integration_time = 0;
    uint32_t gain = 0;
    m_v4l2->v4l2_ctrl_get<uint16_t>(V4L2_CTRL_AE_ENABLE, enabled);
    m_v4l2->v4l2_ctrl_get<uint32_t>(V4L2_CTRL_AE_GAIN, gain);
    m_v4l2->v4l2_ctrl_get<uint16_t>(V4L2_CTRL_AE_INTEGRATION_TIME, integration_time);
    return auto_exposure_t{(bool)enabled, (uint16_t)(gain / 1024), integration_time};
}

void IspResource::set_auto_exposure(nlohmann::json &req, httplib::Response &res)
{
    webserver::common::auto_exposure_t ae;
    try
    {
        ae = req.get<webserver::common::auto_exposure_t>();
    }
    catch (const std::exception &e)
    {
        res.set_content("Failed to cast JSON to auto_exposure_t", "text/plain");
        res.status = 500;
        return;
    }

    if (!set_auto_exposure(ae))
    {
        res.set_content("Failed to set auto exposure", "text/plain");
        res.status = 500;
        return;
    }

    // cast out to json
    nlohmann::json j_out = get_auto_exposure();
    res.set_content(j_out.dump(), "application/json");
}

bool IspResource::set_auto_exposure(auto_exposure_t &ae)
{
    uint32_t gain = (uint32_t)ae.gain * 1024;
    std::cout << "Setting AE to: " << ae.enabled << " with gain: " << gain << " and integration time: " << ae.integration_time << std::endl;
    m_v4l2->v4l2_ext_ctrl_set<uint16_t>(V4L2_CTRL_AE_ENABLE, ae.enabled);
    if (ae.enabled)
    {
        // sleep so auto exposure values will be updated
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return true;
    }
    bool ret = m_v4l2->v4l2_ext_ctrl_set<uint32_t>(V4L2_CTRL_AE_GAIN, gain);
    if (!ret)
    {
        return false;
    }
    ret = m_v4l2->v4l2_ext_ctrl_set<uint16_t>(V4L2_CTRL_AE_INTEGRATION_TIME, ae.integration_time);
    if (!ret)
    {
        return false;
    }
    return true;
}