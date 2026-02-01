#include "isp.hpp"

#include "common/isp/v4l2_ctrl.hpp"
#include <functional>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

using namespace webserver::resources;
using namespace webserver::common;
using namespace webserver;

// TODO PULL min max from 3aconfig
//  not all gain values are valid in ISP, ISP rounds down to the nearest valid value, so we need to round up so we get
//  the value we want
#define ROUND_GAIN_GET_U16(gain) (uint16_t)((gain - (gain % 1024)) / 1024 + 1 * !!(gain % 1024))
#define EXCLUEDED_TUNING_PROFILES ({webserver::common::TUNING_PROFILE_MAX, webserver::common::TUNING_PROFILE_HDR_FHD})
#define ISP_FILTERS_MANUAL_STATE_IS_AUTO(state)                                                                        \
    (state == IspResource::FiltersManualState::FILTER_STATE_AUTO ||                                                    \
     state == IspResource::FiltersManualState::FILTER_STATE_FORCE_AUTO)
#define ISP_FILTERS_MANUAL_STATE_IS_MANUAL(state) (state == IspResource::FiltersManualState::FILTER_STATE_MANUAL)

IspResource::IspResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<ConfigResourceMedialib> config_res)
    : Resource(event_bus), m_baseline_stream_params(0, 0, 0, 0, 0), m_baseline_wdr_params(0),
      m_baseline_backlight_params(0, 0), m_isp_filters_manual_state(IspResource::FiltersManualState::FILTER_STATE_AUTO),
      m_config_res(config_res)
{

    subscribe_callback(EventType::RESET_ISP, [this](ResourceStateChangeNotification notification) {
        WEBSERVER_LOG_INFO("Received configure isp notification");
        this->init();
    });
    subscribe_callback(EventType::SWITCH_PROFILE, [this, config_res](ResourceStateChangeNotification notification) {
        m_isp_filters_manual_state = config_res->get_denoise_default_config()["enabled"].get<bool>()
                                         ? IspResource::FiltersManualState::FILTER_STATE_FORCE_AUTO
                                         : IspResource::FiltersManualState::FILTER_STATE_AUTO;

        this->init();
    });
    subscribe_callback(EventType::UPDATE_BLENDER, [this](ResourceStateChangeNotification notification) {
        WEBSERVER_LOG_DEBUG("Received update blender notification");
        auto state =
            notification.getResourceStateFromBase<ShareValueState<std::shared_ptr<webserver::pipeline::IspBlender>>>();
        m_isp_blender_ptr = state->value;
        WEBSERVER_LOG_DEBUG("Isp blender updated");
    });
}

void IspResource::reset_config()
{
    this->init();
}

backlight_filter_t IspResource::get_blacklight()
{
    automatic_algorithms_config_t config = m_isp_blender_ptr->get_current_automatic_algorithms_config();
    uint16_t max;
    uint16_t min;
    min = config.adaptive_ae.wdrContrast.min;
    max = config.adaptive_ae.wdrContrast.max;
    return backlight_filter_t(max, min);
}
void IspResource::init(bool set_auto_wb)
{
    this->m_baseline_backlight_params = get_blacklight();
    WEBSERVER_LOG_INFO("ISP: Baseline backlight params: \n\tmax level: {}, \tmin level: {}",
                       m_baseline_backlight_params.max_level, m_baseline_backlight_params.min_level);

    // make sure AE is enabled
    auto ae = this->get_auto_exposure();
    if (!ae.enabled)
    {
        WEBSERVER_LOG_DEBUG("ISP: Auto exposure is disabled, enabling it");
        ae.enabled = true;
        this->set_auto_exposure(ae);
    }

    if (set_auto_wb)
    {
        // set auto white balance
        WEBSERVER_LOG_DEBUG("ISP: Setting auto white balance to auto");
        v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::AWB_MODE, 1);
    }

    m_isp_converge = false;
    WEBSERVER_LOG_DEBUG("ISP: enable 3a config auto algos");
    m_isp_blender_ptr->set_auto_configs(true);
    if (m_isp_filters_manual_state != IspResource::FiltersManualState::FILTER_STATE_MANUAL)
    {
        return; // baseline params are not needed in manual mode
    }

    wait_isp_converge(50, 1000);
    WEBSERVER_LOG_DEBUG("ISP: disable 3a config");
    m_isp_blender_ptr->set_auto_configs(false);
    m_isp_converge = true;

    uint16_t *sharpness_down = &m_baseline_stream_params.sharpness_down;
    uint16_t *sharpness_up = &m_baseline_stream_params.sharpness_up;
    v4l2_ctrl::get<uint16_t *>(v4l2_ctrl::Video0Ctrl::SHARPNESS_DOWN, sharpness_down);
    v4l2_ctrl::get<uint16_t *>(v4l2_ctrl::Video0Ctrl::SHARPNESS_UP, sharpness_up);
    v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::BRIGHTNESS, m_baseline_stream_params.brightness);
    v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::SATURATION, m_baseline_stream_params.saturation);
    v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::CONTRAST, m_baseline_stream_params.contrast);
    v4l2_ctrl::get<int16_t>(v4l2_ctrl::Video0Ctrl::WDR_CONTRAST, m_baseline_wdr_params);

    WEBSERVER_LOG_INFO("ISP: Baseline stream params: \n\tSharpness Down: {}\n\tSharpness Up: {}\n\tSaturation: "
                       "{}\n\tBrightness: {}\n\tContrast: {}\n\tWDR: {}",
                       m_baseline_stream_params.sharpness_down, m_baseline_stream_params.sharpness_up,
                       m_baseline_stream_params.saturation, m_baseline_stream_params.brightness,
                       m_baseline_stream_params.contrast, m_baseline_wdr_params);
}

bool IspResource::get_isp_converge()
{
    int converged = 0;
    bool ret = v4l2_ctrl::get<int>(v4l2_ctrl::Video0Ctrl::AE_CONVERGED, converged);
    if (!ret)
    {
        WEBSERVER_LOG_ERROR("Failed to get AE converged");
        throw std::runtime_error("Failed to get AE converged");
    }
    WEBSERVER_LOG_DEBUG("Got AE converged: {}", converged);
    if (converged != 1 && converged != 0)
    {
        WEBSERVER_LOG_ERROR("Invalid AE converged value");
        throw std::runtime_error("Invalid AE converged value");
    }
    return converged == 1;
}

void IspResource::wait_isp_converge(int polling_interval, int delay_after_polling)
{
    int watchdog_timeout = 2000;
    while (!get_isp_converge())
    {
        watchdog_timeout -= polling_interval;
        std::this_thread::sleep_for(std::chrono::milliseconds(polling_interval));
        if (watchdog_timeout <= 0)
        {
            WEBSERVER_LOG_WARN("ISP: AE did not converge");
            break;
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_after_polling));
}

void IspResource::wait_safe_to_pull()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_isp_converge)
    {
        this->init(false);
        m_isp_converge = true;
    }
}

void IspResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->Get("/isp/refresh", std::function<void()>([this]() {
                 if (ISP_FILTERS_MANUAL_STATE_IS_MANUAL(m_isp_filters_manual_state))
                     m_isp_filters_manual_state =
                         IspResource::FiltersManualState::FILTER_STATE_AUTO; // reset to auto state on refresh
                 this->init();
             }));

    srv->Get("/isp/filters_manual_state", std::function<nlohmann::json()>([this]() {
                 nlohmann::json j_out;
                 j_out["auto"] = ISP_FILTERS_MANUAL_STATE_IS_AUTO(m_isp_filters_manual_state);
                 return j_out;
             }));

    srv->Post(
        "/isp/filters_manual_state",
        std::function<std::pair<nlohmann::json, int>(const nlohmann::json &req)>([this](const nlohmann::json &req) {
            std::string ret_msg;
            bool state = false;
            bool ret = json_extract_value<bool>(req, "auto", state, &ret_msg);
            if (!ret)
            {
                WEBSERVER_LOG_ERROR("Failed to extract filters manual state from JSON: {}", ret_msg);
                throw std::runtime_error(ret_msg);
            }
            if (m_isp_filters_manual_state == IspResource::FiltersManualState::FILTER_STATE_FORCE_AUTO)
            {
                WEBSERVER_LOG_WARN("Cannot set filters manual state to auto when force_auto is enabled");
                return std::pair<nlohmann::json, int>({{"auto", true}},
                                                      400); // return 400 Bad Request if force_auto is enabled
            }
            m_isp_filters_manual_state = state ? IspResource::FiltersManualState::FILTER_STATE_AUTO
                                               : IspResource::FiltersManualState::FILTER_STATE_MANUAL;
            this->init(false);
            nlohmann::json j_out;
            j_out["auto"] = state;
            return std::pair<nlohmann::json, int>(j_out, 200);
        }));

    srv->Post("/isp/powerline_frequency",
              std::function<nlohmann::json(const nlohmann::json &req)>([this](const nlohmann::json &req) {
                  std::string ret_msg;
                  powerline_frequency_t freq = POWERLINE_FREQUENCY_OFF;
                  bool ret = json_extract_value<powerline_frequency_t>(req, "powerline_freq", freq, &ret_msg);
                  if (!ret)
                  {
                      WEBSERVER_LOG_ERROR("Failed to extract powerline frequency from JSON: {}", ret_msg);
                      throw std::runtime_error(ret_msg);
                  }
                  // When adaptive_ae is enabled, the anti-flicker setting will be overridden by its configuration.
                  // Adaptive_ae includes a field that dictates the anti-flicker behavior, bypassing the ioctl-defined
                  // value. If adaptive_ae is disabled, the ioctl setting will take effect. Therefore, both
                  // configurations are updated to ensure consistency.
                  automatic_algorithms_config_t config = m_isp_blender_ptr->get_current_automatic_algorithms_config();
                  // update in v4l2 ctrl only if adaptive AE is disabled
                  WEBSERVER_LOG_DEBUG("Setting powerline frequency to: {}", freq);
                  ret = v4l2_ctrl::set<int>(v4l2_ctrl::Video0Ctrl::POWERLINE_FREQUENCY, (uint16_t)freq);
                  if (!ret)
                  {
                      WEBSERVER_LOG_ERROR("Failed to set powerline frequency");
                      throw std::runtime_error("Failed to set powerline frequency");
                  }
                  // update in 3a config if adaptive AE is enabled
                  config.adaptive_ae.flicker_period = (u_int16_t)freq;
                  config.hdr_adaptive_ae.flicker_period = (u_int16_t)freq;
                  m_isp_blender_ptr->set_automatic_algorithms_config(config);

                  nlohmann::json j_out;
                  j_out["powerline_freq"] = freq;
                  return j_out;
              }));

    srv->Get("/isp/powerline_frequency", std::function<nlohmann::json()>([this]() {
                 wait_safe_to_pull();
                 powerline_frequency_t freq;
                 automatic_algorithms_config_t config = m_isp_blender_ptr->get_current_automatic_algorithms_config();
                 if (!config.adaptive_ae.enabled)
                 {
                     int val;
                     bool ret = v4l2_ctrl::get<int>(v4l2_ctrl::Video0Ctrl::POWERLINE_FREQUENCY, val);
                     if (!ret)
                     {
                         WEBSERVER_LOG_ERROR("Failed to get powerline frequency");
                         throw std::runtime_error("Failed to get powerline frequency");
                     }
                     freq = (powerline_frequency_t)val;
                 }
                 else
                 {
                     freq = (powerline_frequency_t)config.adaptive_ae.flicker_period;
                 }
                 nlohmann::json j_out;
                 j_out["powerline_freq"] = freq;
                 WEBSERVER_LOG_DEBUG("Got powerline frequency: {}", freq);
                 return j_out;
             }));

    srv->Post("/isp/noise_reduction", [this](const nlohmann::json &req) {
        std::string ret_msg;
        int nr = 0;
        bool ret = json_extract_value<int>(req, "noise_reduction", nr, &ret_msg);
        if (!ret)
        {
            WEBSERVER_LOG_ERROR("Failed to extract noise reduction from JSON: {}", ret_msg);
            throw std::runtime_error(ret_msg);
        }
        if (nr > 100 || nr < 0)
        {
            WEBSERVER_LOG_ERROR("Invalid noise reduction value");
            throw std::runtime_error("Invalid noise reduction value");
        }
        WEBSERVER_LOG_DEBUG("Setting noise reduction to: {}", nr);
        ret = v4l2_ctrl::set<int>(v4l2_ctrl::Video0Ctrl::NOISE_REDUCTION, nr);
        if (!ret)
        {
            WEBSERVER_LOG_ERROR("Failed to set noise reduction");
            throw std::runtime_error("Failed to set noise reduction");
        }
    });

    srv->Post("/isp/wdr", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                  if (ISP_FILTERS_MANUAL_STATE_IS_AUTO(m_isp_filters_manual_state))
                  {
                      WEBSERVER_LOG_ERROR("WDR can only be set in manual mode");
                      throw std::runtime_error("WDR can only be set in manual mode");
                  }

                  wide_dynamic_range_t wdr = j_body.get<wide_dynamic_range_t>();
                  auto val = v4l2_ctrl::calculate_value_from_precentage<int32_t>(
                      wdr.value, v4l2_ctrl::Video0Ctrl::WDR_CONTRAST, m_baseline_wdr_params);
                  WEBSERVER_LOG_INFO("Setting WDR to: {}", val);
                  bool ret = v4l2_ctrl::set<int16_t>(v4l2_ctrl::Video0Ctrl::WDR_CONTRAST, val);
                  if (!ret)
                  {
                      WEBSERVER_LOG_ERROR("Failed to set WDR");
                      throw std::runtime_error("Failed to set WDR");
                  }
                  return j_body;
              }));

    srv->Get("/isp/wdr", std::function<nlohmann::json()>([this]() {
                 if (ISP_FILTERS_MANUAL_STATE_IS_AUTO(m_isp_filters_manual_state))
                 {
                     wide_dynamic_range_t wdr;
                     wdr.value = 50; // default value for auto mode
                     nlohmann::json j_out = wdr;
                     WEBSERVER_LOG_DEBUG("WDR is in auto mode, returning default value: 50");
                     return j_out;
                 }

                 wait_safe_to_pull();
                 wide_dynamic_range_t wdr;
                 int32_t val;
                 bool ret = v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::WDR_CONTRAST, val);
                 if (!ret)
                 {
                     WEBSERVER_LOG_ERROR("Failed to get WDR");
                     throw std::runtime_error("Failed to get WDR");
                 }
                 wdr.value = v4l2_ctrl::calculate_precentage_from_value<int32_t>(
                     val, v4l2_ctrl::Video0Ctrl::WDR_CONTRAST, m_baseline_wdr_params);
                 WEBSERVER_LOG_INFO("Got WDR value: {}", wdr.value);
                 nlohmann::json j_out = wdr;
                 return j_out;
             }));

    srv->Post("/isp/awb", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                  auto awb_type_names = get_illumination_names();
                  webserver::common::auto_white_balance_t awb;
                  try
                  {
                      awb = j_body.get<webserver::common::auto_white_balance_t>();
                  }
                  catch (const std::exception &e)
                  {
                      WEBSERVER_LOG_ERROR("Failed to cast JSON to auto_white_balance_t: {}", e.what());
                      throw std::runtime_error("Failed to cast JSON to auto_white_balance_t");
                  }

                  WEBSERVER_LOG_DEBUG("AWB POST request, logical profile = {}", static_cast<int>(awb.value));

                  if (awb.value == AUTO_WHITE_BALANCE_PROFILE_AUTO)
                  {
                      WEBSERVER_LOG_DEBUG("Setting AWB to auto");
                      v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::AWB_MODE, 1);
                  }
                  else
                  {
                      std::string target_keyword = get_awb_target_keyword(awb.value);

                      int32_t found_index = -1;
                      const bool exact = (target_keyword == "A" || target_keyword == "B");

                      for (size_t i = 0; i < awb_type_names.size(); ++i)
                      {
                          if ((exact && awb_type_names[i] == target_keyword) ||
                              (!exact && awb_type_names[i].find(target_keyword) != std::string::npos))
                          {
                              found_index = static_cast<int32_t>(i);
                              WEBSERVER_LOG_DEBUG("Mapped logical profile '{}' to HW index {} (Name: {})",
                                                  target_keyword, found_index, awb_type_names[i]);
                              break;
                          }
                      }

                      if (found_index == -1)
                      {
                          WEBSERVER_LOG_ERROR("Target AWB profile '{}' not found in current calibration file!",
                                              target_keyword);
                          throw std::runtime_error("AWB profile not supported by current sensor calibration");
                      }

                      WEBSERVER_LOG_DEBUG("Setting AWB to manual, logical_profile = {}, hw_illum_index = {}",
                                          static_cast<int>(awb.value), found_index);

                      v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::AWB_MODE, 0);
                      v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::AWB_ILLUM_INDEX, found_index);

                      int32_t current_idx = -1;
                      bool ret = v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::AWB_ILLUM_INDEX, current_idx);
                      if (ret)
                      {
                          WEBSERVER_LOG_DEBUG("AWB_ILLUM_INDEX readback from driver = {}", current_idx);
                      }
                      else
                      {
                          WEBSERVER_LOG_ERROR("Failed to read AWB_ILLUM_INDEX back from driver");
                      }
                  }

                  nlohmann::json j_out = awb;
                  return j_out;
              }));

    srv->Get("/isp/awb", std::function<nlohmann::json()>([this]() {
                 wait_safe_to_pull();
                 int32_t val;
                 bool ret = v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::AWB_MODE, val);
                 if (!ret)
                 {
                     WEBSERVER_LOG_ERROR("Failed to get AWB mode");
                     throw std::runtime_error("Failed to get AWB mode");
                 }
                 if (val != 1) // manual mode, get profile
                 {
                     ret = v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::AWB_ILLUM_INDEX, val);
                     if (!ret)
                     {
                         WEBSERVER_LOG_ERROR("Failed to get AWB profile");
                         throw std::runtime_error("Failed to get AWB profile");
                     }
                 }
                 else // automatic mode
                 {
                     val = -1;
                 }
                 webserver::common::auto_white_balance_t awb{(webserver::common::auto_white_balance_profile)val};
                 nlohmann::json j_out = awb;
                 return j_out;
             }));

    srv->Get("/isp/stream_params", std::function<nlohmann::json()>([this]() {
                 if (ISP_FILTERS_MANUAL_STATE_IS_AUTO(m_isp_filters_manual_state))
                 {
                     webserver::common::stream_params_t p{50, 50, 50, 50};
                     nlohmann::json j_out = p;
                     return j_out;
                 }

                 wait_safe_to_pull();
                 stream_isp_params_t p(0, 0, 0, 0, 0);
                 uint16_t *sharpness_down = &p.sharpness_down;
                 uint16_t *sharpness_up = &p.sharpness_up;
                 v4l2_ctrl::get<uint16_t *>(v4l2_ctrl::Video0Ctrl::SHARPNESS_DOWN, sharpness_down);
                 v4l2_ctrl::get<uint16_t *>(v4l2_ctrl::Video0Ctrl::SHARPNESS_UP, sharpness_up);
                 v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::BRIGHTNESS, p.brightness);
                 v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::SATURATION, p.saturation);
                 v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::CONTRAST, p.contrast);
                 nlohmann::json j_out = m_baseline_stream_params.to_stream_params(p);
                 WEBSERVER_LOG_INFO("Got stream params: {}", j_out.dump());
                 return j_out;
             }));

    srv->Post(
        "/isp/stream_params",
        std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
            if (ISP_FILTERS_MANUAL_STATE_IS_AUTO(m_isp_filters_manual_state))
            {
                WEBSERVER_LOG_ERROR("Stream params can only be set in manual mode");
                throw std::runtime_error("Stream params can only be set in manual mode");
            }

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
            WEBSERVER_LOG_INFO("Setting stream params to: \n\tSharpness Down: {}\n\tSharpness Up: {}\n\tSaturation: "
                               "{}\n\tBrightness: {}\n\tContrast: {}",
                               isp_params.sharpness_down, isp_params.sharpness_up, isp_params.saturation,
                               isp_params.brightness, isp_params.contrast);
            v4l2_ctrl::set<int32_t>(v4l2_ctrl::Video0Ctrl::SATURATION, isp_params.saturation);
            v4l2_ctrl::set<int32_t>(v4l2_ctrl::Video0Ctrl::BRIGHTNESS, static_cast<int8_t>(isp_params.brightness));
            v4l2_ctrl::set<int32_t>(v4l2_ctrl::Video0Ctrl::CONTRAST, isp_params.contrast);

            v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::EE_ENABLE, 0);

            v4l2_ctrl::set<uint16_t *>(v4l2_ctrl::Video0Ctrl::SHARPNESS_DOWN, &isp_params.sharpness_down);
            v4l2_ctrl::set<uint16_t *>(v4l2_ctrl::Video0Ctrl::SHARPNESS_UP, &isp_params.sharpness_up);

            v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::EE_ENABLE, 1);

            // cast out to json
            nlohmann::json j_out = stream_params;
            return j_out;
        }));

    srv->Post("/isp/auto_exposure",
              std::function<nlohmann::json(const nlohmann::json &)>(
                  [this](const nlohmann::json &j_body) { return this->set_auto_exposure(j_body); }));

    srv->Patch("/isp/auto_exposure", [this](const nlohmann::json &j_body) {
        auto params = this->get_auto_exposure();
        nlohmann::json j_params = params;
        j_params.merge_patch(j_body);
        return this->set_auto_exposure(j_params);
    });

    srv->Get("/isp/auto_exposure", std::function<nlohmann::json()>([this]() {
                 wait_safe_to_pull();
                 auto params = this->get_auto_exposure();
                 nlohmann::json j_out = params;
                 return j_out;
             }));

    srv->Get("/isp/ranges/auto_exposure", std::function<nlohmann::json()>([this]() {
                 nlohmann::json j_out = get_auto_exposure_ranges();
                 return j_out;
             }));

    srv->Get("/isp/safe_to_pull", std::function<nlohmann::json()>([this]() {
                 nlohmann::json j_out;
                 j_out["safe_to_pull"] = get_isp_converge();
                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
                 return j_out;
             }));

    srv->Get("/isp/sensor_model", std::function<nlohmann::json()>([this]() {
                 nlohmann::json j_out;
                 j_out["sensor_model"] = get_sensor_type();
                 j_out["available_resolutions"] = webserver::common::sensor_resolutions_for_user.at(get_sensor_type());
                 WEBSERVER_LOG_DEBUG("Got sensor model: {}", j_out["sensor_model"]);
                 return j_out;
             }));
}

ae_ranges_t IspResource::get_auto_exposure_ranges()
{
    ae_ranges_t ranges;
    ranges.ae_gain = v4l2_ctrl::min_max_isp_params.at(v4l2_ctrl::Video0Ctrl::AE_GAIN);
    ranges.ae_gain.min = ROUND_GAIN_GET_U16(ranges.ae_gain.min);
    ranges.ae_gain.max = ROUND_GAIN_GET_U16(ranges.ae_gain.max);
    ranges.ae_integration_time = v4l2_ctrl::min_max_isp_params.at(v4l2_ctrl::Video0Ctrl::AE_INTEGRATION_TIME);
    return ranges;
}

auto_exposure_t IspResource::get_auto_exposure()
{
    uint16_t enabled = 0;
    uint16_t integration_time = 0;
    uint32_t gain = 0;
    v4l2_ctrl::get<uint16_t>(v4l2_ctrl::Video0Ctrl::AE_ENABLE, enabled);
    v4l2_ctrl::get<uint32_t>(v4l2_ctrl::Video0Ctrl::AE_GAIN, gain);
    v4l2_ctrl::get<uint16_t>(v4l2_ctrl::Video0Ctrl::AE_INTEGRATION_TIME, integration_time);

    WEBSERVER_LOG_DEBUG("Got auto exposure: enabled: {}, gain: {}, integration_time: {}", enabled, gain,
                        integration_time);

    backlight_filter_t current = get_blacklight();
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
        WEBSERVER_LOG_ERROR("Failed to cast JSON to auto_exposure_t");
        throw std::runtime_error("Failed to cast JSON to auto_exposure_t");
    }

    if (!set_auto_exposure(ae))
    {
        WEBSERVER_LOG_ERROR("Failed to set auto exposure");
        throw std::runtime_error("Failed to set auto exposure");
    }

    // cast out to json
    nlohmann::json j_out = get_auto_exposure();
    return j_out;
}

bool IspResource::set_auto_exposure(auto_exposure_t &ae)
{
    uint32_t gain = (uint32_t)ae.gain * 1024;
    WEBSERVER_LOG_DEBUG("Setting auto exposure enabled: {}", ae.enabled);
    v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::AE_ENABLE, ae.enabled);
    if (ae.enabled)
    {
        // sleep so auto exposure values will be updated
        std::this_thread::sleep_for(std::chrono::seconds(1));
        backlight_filter_t current = m_baseline_backlight_params.from_precentage(ae.backlight);
        automatic_algorithms_config_t config = m_isp_blender_ptr->get_current_automatic_algorithms_config();
        config.adaptive_ae.wdrContrast.max = current.max_level;
        config.adaptive_ae.wdrContrast.min = current.min_level;
        m_isp_blender_ptr->set_automatic_algorithms_config(config);
    }
    else
    {
        WEBSERVER_LOG_DEBUG("AutoExposure is on manual mode, setting gain {} and integration time {}", gain,
                            ae.integration_time);
        bool ret = v4l2_ctrl::set<uint32_t>(v4l2_ctrl::Video0Ctrl::AE_GAIN, gain);
        if (!ret)
        {
            return false;
        }
        ret = v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::AE_INTEGRATION_TIME, ae.integration_time);
        if (!ret)
        {
            return false;
        }
    }

    return true;
}

std::string IspResource::get_awb_target_keyword(webserver::common::auto_white_balance_profile profile)
{
    switch (profile)
    {
    case AUTO_WHITE_BALANCE_PROFILE_A:
        return "A";
    case AUTO_WHITE_BALANCE_PROFILE_D50:
        return "D50";
    case AUTO_WHITE_BALANCE_PROFILE_D65:
        return "D65";
    case AUTO_WHITE_BALANCE_PROFILE_D75:
        return "D75";
    case AUTO_WHITE_BALANCE_PROFILE_TL84:
        return "TL84"; // Matches "F11 (TL84)"
    case AUTO_WHITE_BALANCE_PROFILE_F12:
        return "B"; // Matches "B"
    case AUTO_WHITE_BALANCE_PROFILE_CWF:
        return "CWF"; // Matches "F2 (CWF)"
    default:
        WEBSERVER_LOG_ERROR("Invalid AWB profile enum value: {}", static_cast<int>(profile));
        throw std::runtime_error("Invalid AWB profile enum value");
    }
}

std::vector<std::string> IspResource::get_illumination_names()
{
    std::string calib_file_path;
    if (m_config_res)
    {
        nlohmann::json profile_json = m_config_res->get_current_profile();

        if (profile_json.contains("sensor_config"))
        {
            std::string sensor_config_path = profile_json["sensor_config"].get<std::string>();
            auto sensor_expected = m_config_res->load_config_from_file(sensor_config_path);
            if (sensor_expected)
            {
                nlohmann::json sensor_json = *sensor_expected;

                if (sensor_json.contains("sensor_calibration_file"))
                {
                    calib_file_path = sensor_json["sensor_calibration_file"].get<std::string>();
                    WEBSERVER_LOG_DEBUG("Calibration file path: {}", calib_file_path);
                }
            }
        }
        else
        {
            WEBSERVER_LOG_ERROR("Key 'sensor_config' not found in current profile");
        }
    }

    std::vector<std::string> illumination_names;

    if (calib_file_path.empty())
    {
        return illumination_names;
    }

    try
    {
        auto calib_expected = m_config_res->load_config_from_file(calib_file_path);

        if (!calib_expected)
        {
            WEBSERVER_LOG_ERROR("Failed to load calibration config: {}", calib_expected.error());
            return illumination_names;
        }

        nlohmann::json calib_json = *calib_expected;

        if (calib_json.contains("matfile") && calib_json["matfile"].contains("sensor") &&
            calib_json["matfile"]["sensor"].contains("AWB") &&
            calib_json["matfile"]["sensor"]["AWB"].contains("illumination"))
        {
            const auto &illumination_array = calib_json["matfile"]["sensor"]["AWB"]["illumination"];

            for (const auto &illum : illumination_array)
            {
                if (illum.contains("name"))
                {
                    std::string name = illum["name"].get<std::string>();
                    illumination_names.push_back(name);
                    WEBSERVER_LOG_DEBUG("Found illumination: {}", name);
                }
            }
        }
        else
        {
            WEBSERVER_LOG_ERROR("Invalid calib.json structure - missing illumination data");
        }
    }
    catch (const nlohmann::json::exception &e)
    {
        WEBSERVER_LOG_ERROR("JSON parsing error: {}", e.what());
    }
    catch (const std::exception &e)
    {
        WEBSERVER_LOG_ERROR("Error reading calib file: {}", e.what());
    }

    return illumination_names;
}
