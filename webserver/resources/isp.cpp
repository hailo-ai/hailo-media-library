#include "resources/resources.hpp"
#include "common/common.hpp"
#include <iostream>
#include <functional>

using namespace webserver::resources;
using namespace webserver::common;

// not all gain values are valid in ISP, ISP rounds down to the nearest valid value, so we need to round up so we get the value we want
#define ROUND_GAIN_GET_U16(gain) (uint16_t)((gain - (gain % 1024)) / 1024 + 1 * !!(gain % 1024))

IspResource::IspResource(std::shared_ptr<AiResource> ai_res) : Resource(), m_baseline_stream_params(0, 0, 0, 0, 0), m_baseline_wdr_params(0), m_baseline_backlight_params(0, 0)
{
    m_v4l2 = std::make_unique<webserver::common::v4l2Control>(V4L2_DEVICE_NAME);
    m_ai_resource = ai_res;
    m_tuning_profile = webserver::common::TUNING_PROFILE_DEFAULT;
    m_ai_resource->subscribe_callback([this](ResourceStateChangeNotification notification)
                                      { this->on_ai_state_change(std::static_pointer_cast<AiResource::AiResourceState>(notification.resource_state)); });
}

void IspResource::set_tuning_profile(tuning_profile_t profile)
{
    switch (profile)
    {
    case TUNING_PROFILE_BACKLIGHT_COMPENSATION:
        isp_utils::set_backlight_configuration();
        break;
    case TUNING_PROFILE_DENOISE:
        isp_utils::set_denoise_configuration();
        break;
    default:
        isp_utils::set_default_configuration();
        break;
    }
}

void IspResource::on_ai_state_change(std::shared_ptr<AiResource::AiResourceState> state)
{
    std::cout << "IspResource::on_ai_state_change" << std::endl;
    // bool ae_enabled;

    on_resource_change(std::make_shared<ResourceState>(IspResource::IspResourceState(true)));

    // Sleep before sending any ioctl is required
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (std::find(state->enabled.begin(), state->enabled.end(), AiResource::AiApplications::AI_APPLICATION_DENOISE) != state->enabled.end()) // enabled denoise
    {
        std::cout << "enabling denoise" << std::endl;
        m_tuning_profile = webserver::common::TUNING_PROFILE_DENOISE;
        // ae_enabled = false; // previous version of the NN was trained for AE off
    }
    else if (std::find(state->disabled.begin(), state->disabled.end(), AiResource::AiApplications::AI_APPLICATION_DENOISE) != state->disabled.end()) // disabled denoise
    {

        std::cout << "enabling default configuration" << std::endl;
        m_tuning_profile = webserver::common::TUNING_PROFILE_DEFAULT;
        // ae_enabled = true;
    }
    else
    {
        return;
    }

    set_tuning_profile(m_tuning_profile);
}

void IspResource::init(bool set_auto_wb)
{
    this->set_tuning_profile(m_tuning_profile);
    this->m_baseline_backlight_params = backlight_filter_t::get_from_json();

    // make sure AE is enabled
    auto ae = this->get_auto_exposure();
    ae.enabled = true;
    this->set_auto_exposure(ae);

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
    std::cout << "Baseline backlight params: " << std::endl;
    std::cout << "\tmax: " << m_baseline_backlight_params.max << std::endl;
    std::cout << "\tmin: " << m_baseline_backlight_params.min << std::endl;
}

void IspResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->Get("/isp/refresh", std::function<void()>([this]()
                                                   { this->init(); }));

    srv->Post("/isp/powerline_frequency", [this](const nlohmann::json &req)
              {
        std::string ret_msg;
        powerline_frequency_t freq;
        bool ret = json_extract_value<powerline_frequency_t>(req, "powerline_freq", freq, &ret_msg);
        if (!ret)
        {
            throw std::runtime_error(ret_msg);
        }

        ret = m_v4l2->v4l2_ctrl_set<int>(V4L2_CTRL_POWERLINE_FREQUENCY, (uint16_t)freq);
        if (!ret)
        {
            throw std::runtime_error("Failed to set powerline frequency");
        } });

    srv->Get("/isp/powerline_frequency", std::function<nlohmann::json()>([this]()
                                                                         {
                int val;
                bool ret = m_v4l2->v4l2_ctrl_get<int>(V4L2_CTRL_POWERLINE_FREQUENCY, val);
                if (!ret)
                {
                    throw std::runtime_error("Failed to get powerline frequency");
                }
                auto freq = (powerline_frequency_t)val;
                nlohmann::json j_out;
                j_out["powerline_freq"] = freq;
                return j_out; }));

    srv->Post("/isp/noise_reduction", [this](const nlohmann::json &req)
              {
                std::string ret_msg;
                int nr;
                bool ret = json_extract_value<int>(req, "noise_reduction", nr, &ret_msg);
                if (!ret)
                {
                    throw std::runtime_error(ret_msg);
                }
                if (nr > 100 || nr < 0)
                {
                    throw std::runtime_error("Invalid noise reduction value");
                }
                ret = m_v4l2->v4l2_ctrl_set<int>(V4L2_CTRL_NOISE_REDUCTION, nr);
                if (!ret)
                {
                    throw std::runtime_error("Failed to set noise reduction");
                } });

    srv->Post("/isp/wdr", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body)
                                                                                {
                wide_dynamic_range_t wdr = j_body.get<wide_dynamic_range_t>();
                auto val = v4l2ControlHelper::calculate_value_from_precentage<int32_t>(wdr.value, V4L2_CTRL_WDR_CONTRAST, m_baseline_wdr_params);
                std::cout << "Setting WDR to: " << val << std::endl;
                bool ret = m_v4l2->v4l2_ext_ctrl_set<int16_t>(V4L2_CTRL_WDR_CONTRAST, val);
                if (!ret)
                {
                    throw std::runtime_error("Failed to set WDR");
                }
                return j_body; }));

    srv->Get("/isp/wdr", std::function<nlohmann::json()>([this]()
                                                         {
                wide_dynamic_range_t wdr;
                int32_t val;
                bool ret = m_v4l2->v4l2_ctrl_get<int32_t>(V4L2_CTRL_WDR_CONTRAST, val);
                if (!ret)
                {
                    throw std::runtime_error("Failed to get WDR");
                }
                wdr.value = v4l2ControlHelper::calculate_precentage_from_value<int32_t>(val, V4L2_CTRL_WDR_CONTRAST, m_baseline_wdr_params);
                std::cout << "Got WDR value: " << wdr.value << std::endl;
                nlohmann::json j_out = wdr;
                return j_out; }));

    srv->Post("/isp/awb", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body)
                                                                                {
                webserver::common::auto_white_balance_t awb;
                try
                {
                    awb = j_body.get<webserver::common::auto_white_balance_t>();
                }
                catch (const std::exception &e)
                {
                    throw std::runtime_error("Failed to cast JSON to auto_white_balance_t");
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
                return j_out; }));

    srv->Get("/isp/awb", std::function<nlohmann::json()>([this]()
                                                         {
                int32_t val;
                bool ret = m_v4l2->v4l2_ctrl_get<int32_t>(V4L2_CTRL_AWB_MODE, val);
                if (!ret)
                {
                    throw std::runtime_error("Failed to get AWB mode");
                }
                if (val != 1) // manual mode, get profile
                {
                    ret = m_v4l2->v4l2_ctrl_get<int32_t>(V4L2_CTRL_AWB_ILLUM_INDEX, val);
                    if (!ret)
                    {
                        throw std::runtime_error("Failed to get AWB profile");
                    }
                }
                else // automatic mode
                {
                    val = -1;
                }
                webserver::common::auto_white_balance_t awb{(webserver::common::auto_white_balance_profile)val};
                nlohmann::json j_out = awb;
                return j_out; }));

    srv->Post("/isp/tuning", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body)
                                                                                   {
                webserver::common::tuning_t tuning;
                try
                {
                    tuning = j_body.get<webserver::common::tuning_t>();
                }
                catch (const std::exception &e)
                {
                    throw std::runtime_error("Failed to cast JSON to tuning_t");
                }

                m_tuning_profile = tuning.value;
                this->init();
                nlohmann::json j_tuning = tuning;
                return j_tuning; }));

    srv->Get("/isp/tuning", std::function<nlohmann::json()>([this]()
                                                            {
                webserver::common::tuning_t tuning;
                tuning.value = this->m_tuning_profile;

                nlohmann::json j_tuning = tuning;
                j_tuning["available"] = get_enum_values<tuning_profile_t>(webserver::common::TUNING_PROFILE_MAX);

                return j_tuning; }));

    srv->Get("/isp/stream_params", std::function<nlohmann::json()>([this]()
                                                                   {
                stream_isp_params_t p(0, 0, 0, 0, 0);
                this->m_v4l2->v4l2_ext_ctrl_get<uint16_t>(webserver::common::V4L2_CTRL_SHARPNESS_DOWN, p.sharpness_down);
                this->m_v4l2->v4l2_ext_ctrl_get<uint16_t>(webserver::common::V4L2_CTRL_SHARPNESS_UP, p.sharpness_up);
                this->m_v4l2->v4l2_ctrl_get<int32_t>(webserver::common::V4L2_CTRL_BRIGHTNESS, p.brightness);
                this->m_v4l2->v4l2_ctrl_get<int32_t>(webserver::common::V4L2_CTRL_SATURATION, p.saturation);
                this->m_v4l2->v4l2_ctrl_get<int32_t>(webserver::common::V4L2_CTRL_CONTRAST, p.contrast);

                nlohmann::json j_out = m_baseline_stream_params.to_stream_params(p);
                std::cout << "Got stream params: " << j_out.dump() << std::endl;
                return j_out; }));

    srv->Post("/isp/stream_params", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body)
                                                                                          {
                std::string ret_msg;
                webserver::common::stream_params_t stream_params;
                try
                {
                    stream_params = j_body.get<webserver::common::stream_params_t>();
                }
                catch (const std::exception &e)
                {
                    throw std::runtime_error("Failed to cast JSON to stream_params_t");
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
                return j_out; }));

    srv->Post("/isp/auto_exposure", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body)
                                                                                          { return this->set_auto_exposure(j_body); }));

    srv->Patch("/isp/auto_exposure", [this](const nlohmann::json &j_body)
               {
                auto params = this->get_auto_exposure();
                nlohmann::json j_params = params;
                j_params.merge_patch(j_body);
                return this->set_auto_exposure(j_params); });

    srv->Get("/isp/auto_exposure", std::function<nlohmann::json()>([this]()
                                                                   {
                auto params = this->get_auto_exposure();
                nlohmann::json j_out = params;
                return j_out; }));
}

auto_exposure_t IspResource::get_auto_exposure()
{
    uint16_t enabled = 0;
    uint16_t integration_time = 0;
    uint32_t gain = 0;
    m_v4l2->v4l2_ctrl_get<uint16_t>(V4L2_CTRL_AE_ENABLE, enabled);
    m_v4l2->v4l2_ctrl_get<uint32_t>(V4L2_CTRL_AE_GAIN, gain);
    m_v4l2->v4l2_ctrl_get<uint16_t>(V4L2_CTRL_AE_INTEGRATION_TIME, integration_time);

    std::cout << "Got gain: " << gain << " integration time: " << integration_time << std::endl;

    backlight_filter_t current = backlight_filter_t::get_from_json();
    uint16_t backlight = m_baseline_backlight_params.to_precentage(current);

    return auto_exposure_t{(bool)enabled, ROUND_GAIN_GET_U16(gain), integration_time, backlight};
}

nlohmann::json IspResource::set_auto_exposure(const nlohmann::json &req)
{
    webserver::common::auto_exposure_t ae;
    try
    {
        ae = req.get<webserver::common::auto_exposure_t>();
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("Failed to cast JSON to auto_exposure_t");
    }

    if (!set_auto_exposure(ae))
    {
        throw std::runtime_error("Failed to set auto exposure");
    }

    // cast out to json
    nlohmann::json j_out = get_auto_exposure();
    return j_out;
}

bool IspResource::set_auto_exposure(auto_exposure_t &ae)
{
    uint32_t gain = (uint32_t)ae.gain * 1024;
    std::cout << "Setting auto exposure: " << ae.enabled << std::endl;
    m_v4l2->v4l2_ext_ctrl_set<uint16_t>(V4L2_CTRL_AE_ENABLE, ae.enabled);
    if (ae.enabled)
    {
        // sleep so auto exposure values will be updated
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    else
    {
        std::cout << "Set gain: " << gain << " integration time: " << ae.integration_time << std::endl;
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
    }

    backlight_filter_t current = m_baseline_backlight_params.from_precentage(ae.backlight);
    nlohmann::json j_3a = get_3a_config();
    auto root = j_3a["root"];
    for (auto &obj : j_3a["root"])
    {
        if (obj["classname"] == "AdaptiveAe")
        {
            obj["wdrContrast.max"] = current.max;
            obj["wdrContrast.min"] = current.min;
        }
    }
    update_3a_config(j_3a);
    return true;
}