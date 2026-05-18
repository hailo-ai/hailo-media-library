#pragma once
#include "media_library/encoder_config.hpp"
#include "common/resources.hpp"
#include "configs.hpp"

namespace webserver
{
namespace resources
{
class EncoderResource : public Resource
{
  public:
    // This struct contains subset of encoder configuration parameters that is exposed to the Frontend
    struct encoder_control_t
    {
        uint32_t intra_pic_rate; // 0 to 300

        struct
        {
            rc_mode_t rc_mode;
            std::optional<uint32_t> bitrate;        // 10k to 40,000k
            std::optional<uint32_t> qp_min;         // 0 to 51
            std::optional<uint32_t> qp_max;         // 0 to 51
            std::optional<int32_t> intra_qp_delta;  // -12 to 12
            std::optional<uint32_t> fixed_intra_qp; // 0 to 51
            std::optional<int32_t> qp_hdr;          // 0 to 51 or -1 for disabled
        } quantization;

        struct roi_ui_t
        {
            std::string id;
            std::string name;
            bool status; // enabled/disabled
            normalized_roi_t roi;
        };

        struct smart_encoder_ui_t
        {
            bool global_enable;
            std::string quality; // "LOW", "MEDIUM", "HIGH"
            std::vector<roi_ui_t> rois;
        };

        std::optional<smart_encoder_ui_t> smart_encoder;

        // Create encoder_control_t by extracting values from hailo_encoder_config_t
        static encoder_control_t from_encoder_element_config(const hailo_encoder_config_t &encoder_config);

        // Update the hailo_encoder_config_t based on the values in this struct
        void fill_encoder_element_config(hailo_encoder_config_t &encoder_config);
    };

    struct EncoderResourceState : public ValueState<encoder_control_t>
    {
        using ValueState::ValueState;
    };

    EncoderResource(std::shared_ptr<EventBus> event_bus,
                    std::shared_ptr<webserver::resources::ConfigResourceBase> configs);

    void http_register(HTTPServer &srv) override;

    std::string name() override;
    ResourceType get_type() override;

    void set_encoder_query(std::function<hailo_encoder_config_t()> get_encoder_config);

    void fill_encoder_element_config(hailo_encoder_config_t &encoder_config);

    // Sync m_config from encoder on first call
    void sync_config_from_encoder();

  private:
    static const std::unordered_map<std::string, uint8_t> quality_to_qp_delta;

    std::function<hailo_encoder_config_t()> m_get_encoder_config;
    static void to_json(nlohmann::json &j, const EncoderResource::encoder_control_t &b);
    static void from_json(const nlohmann::json &j, EncoderResource::encoder_control_t &b);
};
} // namespace resources
} // namespace webserver
