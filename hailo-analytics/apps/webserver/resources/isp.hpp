#pragma once
#include "common/resources.hpp"
#include "osd.hpp"
#include "configs.hpp"
#include "common/isp/common.hpp"
#include "pipeline/isp_blender.hpp"
#include <atomic>
#include <fstream>
#include <nlohmann/json.hpp>

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

  public:
    class IspResourceState : public ResourceState
    {
      public:
        bool isp_3aconfig_updated;
        IspResourceState(bool isp_3aconfig_updated) : isp_3aconfig_updated(isp_3aconfig_updated)
        {
        }
    };

    IspResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<ConfigResourceMedialib> config_res);
    void http_register(std::shared_ptr<HTTPServer> srv) override;
    std::string name() override
    {
        return "isp";
    }
    ResourceType get_type() override
    {
        return ResourceType::RESOURCE_ISP;
    }
    void init(bool set_auto_wb = true);
    std::vector<std::string> get_illumination_names();
    std::string get_awb_target_keyword(webserver::common::auto_white_balance_profile profile);
};
} // namespace resources
} // namespace webserver
