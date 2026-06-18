#pragma once
#include <nlohmann/json.hpp>
#include <stdint.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/resources.hpp"
#include "configs.hpp"
#include "common/isp/common.hpp"
#include "pipeline/isp_blender.hpp"
#include "common/httplib/httplib_utils.hpp"
#include "resources/common/event_bus.hpp"
#include "resources/common/events_utils.hpp"

namespace webserver
{
namespace resources
{
class IspResource : public Resource
{
  private:
    enum class FiltersManualState
    {
        FILTER_STATE_AUTO = 0,
        FILTER_STATE_MANUAL = 1,
        FILTER_STATE_FORCE_AUTO = 2
    };

    std::mutex m_mutex;
    common::stream_isp_params_t m_baseline_stream_params;
    int16_t m_baseline_wdr_params;
    common::backlight_filter_t m_baseline_backlight_params;
    FiltersManualState m_isp_filters_manual_state;
    std::atomic<bool> m_isp_converge;
    std::shared_ptr<webserver::pipeline::IspBlender> m_isp_blender_ptr;
    std::shared_ptr<ConfigResourceMedialib> m_config_res;
    common::auto_exposure_t get_auto_exposure();
    common::ae_ranges_t get_auto_exposure_ranges();
    common::backlight_filter_t get_blacklight();
    nlohmann::json set_auto_exposure(const nlohmann::json &req);
    bool set_auto_exposure(common::auto_exposure_t &ae);
    void set_tuning_profile(webserver::common::tuning_profile_t);
    void reset_config() override;
    bool get_isp_converge();
    void wait_isp_converge(int polling_interval, int delay_after_polling);
    void wait_safe_to_pull();
    void register_refresh(HTTPServer &srv);
    void register_filters_manual_state(HTTPServer &srv);
    void register_powerline_frequency(HTTPServer &srv);
    void register_noise_reduction(HTTPServer &srv);
    void register_wdr(HTTPServer &srv);
    void register_awb(HTTPServer &srv);
    void register_stream_params(HTTPServer &srv);
    void register_auto_exposure(HTTPServer &srv);
    void register_safe_to_pull(HTTPServer &srv);
    void register_sensor_model(HTTPServer &srv);

  public:
    class IspResourceState : public ResourceState
    {
      public:
        bool isp_3aconfig_updated;
        IspResourceState(bool isp_3aconfig_updated);
    };

    IspResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<ConfigResourceMedialib> config_res);
    void http_register(HTTPServer &srv) override;
    std::string name() override;
    ResourceType get_type() override;
    void init(bool set_auto_wb = true);
    std::vector<std::string> get_illumination_names();
    std::string get_awb_target_keyword(webserver::common::auto_white_balance_profile profile);
};
} // namespace resources
} // namespace webserver
