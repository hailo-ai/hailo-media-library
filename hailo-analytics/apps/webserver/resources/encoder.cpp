#include "encoder.hpp"
#include <iostream>

const std::unordered_map<rc_mode_t, std::string> webserver::resources::EncoderResource::rc_mode_to_str{
    {VBR, "VBR"}, {CVBR, "CVBR"}, {CBR, "CBR"}};

const std::unordered_map<std::string, rc_mode_t> webserver::resources::EncoderResource::str_to_rc_mode{
    {"VBR", VBR}, {"CVBR", CVBR}, {"CBR", CBR}};

webserver::resources::EncoderResource::encoder_control_t webserver::resources::EncoderResource::encoder_control_t::
    from_encoder_element_config(const hailo_encoder_config_t &encoder_config)
{
    encoder_control_t control{
        .intra_pic_rate = encoder_config.rate_control.intra_pic_rate,
        .quantization =
            {
                .rc_mode = encoder_config.rate_control.rc_mode,
                .bitrate = encoder_config.rate_control.bitrate.target_bitrate != 0
                               ? std::optional<uint32_t>(encoder_config.rate_control.bitrate.target_bitrate)
                               : std::nullopt,
                .qp_min = encoder_config.rate_control.quantization.qp_min,
                .qp_max = encoder_config.rate_control.quantization.qp_max,
                .intra_qp_delta = encoder_config.rate_control.quantization.intra_qp_delta,
                .fixed_intra_qp = encoder_config.rate_control.quantization.fixed_intra_qp,
                .qp_hdr = encoder_config.rate_control.quantization.qp_hdr,
            },
    };
    return control;
}

void webserver::resources::EncoderResource::encoder_control_t::fill_encoder_element_config(
    hailo_encoder_config_t &encoder_config)
{
    encoder_config.rate_control.intra_pic_rate = this->intra_pic_rate;
    encoder_config.rate_control.rc_mode = this->quantization.rc_mode;
    encoder_config.rate_control.bitrate.target_bitrate =
        this->quantization.bitrate.has_value() ? this->quantization.bitrate.value() : 0;
    encoder_config.rate_control.quantization.qp_min = this->quantization.qp_min;
    encoder_config.rate_control.quantization.qp_max = this->quantization.qp_max;
    encoder_config.rate_control.quantization.intra_qp_delta = this->quantization.intra_qp_delta;
    encoder_config.rate_control.quantization.fixed_intra_qp = this->quantization.fixed_intra_qp;
    encoder_config.rate_control.quantization.qp_hdr = this->quantization.qp_hdr;

    // When setting rate control to CBR or CVBR, ensure picture_rc and ctb_rc are enabled
    if ((this->quantization.rc_mode == CBR) || (this->quantization.rc_mode == CVBR))
    {
        encoder_config.rate_control.picture_rc = true;
        encoder_config.rate_control.ctb_rc = true;
    }
    else
    { // For VBR, disable picture_rc and ctb_rc
        encoder_config.rate_control.picture_rc = false;
        encoder_config.rate_control.ctb_rc = false;
    }
}

webserver::resources::EncoderResource::EncoderResource(
    std::shared_ptr<EventBus> event_bus, std::shared_ptr<webserver::resources::ConfigResourceBase> configs)
    : Resource(event_bus)
{
}

void webserver::resources::EncoderResource::set_encoder_query(
    std::function<hailo_encoder_config_t()> get_encoder_config)
{
    WEBSERVER_LOG_INFO("Setting encoder query");
    m_get_encoder_config = get_encoder_config;
}

void webserver::resources::EncoderResource::to_json(nlohmann::json &j, const EncoderResource::encoder_control_t &b)
{
    j = nlohmann::json{
        {"intra_pic_rate", b.intra_pic_rate},
        {"quantization", {{"rc_mode", rc_mode_to_str.at(b.quantization.rc_mode)}}},
    };

    if (b.quantization.bitrate.has_value())
    {
        j["quantization"]["bitrate"] = b.quantization.bitrate.value();
    }
    if (b.quantization.qp_min.has_value())
    {
        j["quantization"]["qp_min"] = b.quantization.qp_min.value();
    }
    if (b.quantization.qp_max.has_value())
    {
        j["quantization"]["qp_max"] = b.quantization.qp_max.value();
    }
    if (b.quantization.intra_qp_delta.has_value())
    {
        j["quantization"]["intra_qp_delta"] = b.quantization.intra_qp_delta.value();
    }
    if (b.quantization.fixed_intra_qp.has_value())
    {
        j["quantization"]["fixed_intra_qp"] = b.quantization.fixed_intra_qp.value();
    }
    if (b.quantization.qp_hdr.has_value())
    {
        j["quantization"]["qp_hdr"] = b.quantization.qp_hdr.value();
    }
}

void webserver::resources::EncoderResource::from_json(const nlohmann::json &j, EncoderResource::encoder_control_t &b)
{
    j.at("intra_pic_rate").get_to(b.intra_pic_rate);

    const auto &quant = j.at("quantization");
    std::string rc_mode_str = quant.at("rc_mode").get<std::string>();
    b.quantization.rc_mode = str_to_rc_mode.at(rc_mode_str);

    if (quant.contains("bitrate"))
    {
        b.quantization.bitrate = quant.at("bitrate").get<uint32_t>();
    }
    if (quant.contains("qp_min"))
    {
        b.quantization.qp_min = quant.at("qp_min").get<uint32_t>();
    }
    if (quant.contains("qp_max"))
    {
        b.quantization.qp_max = quant.at("qp_max").get<uint32_t>();
    }
    if (quant.contains("intra_qp_delta"))
    {
        b.quantization.intra_qp_delta = quant.at("intra_qp_delta").get<int32_t>();
    }
    if (quant.contains("fixed_intra_qp"))
    {
        b.quantization.fixed_intra_qp = quant.at("fixed_intra_qp").get<uint32_t>();
    }
    if (quant.contains("qp_hdr"))
    {
        b.quantization.qp_hdr = quant.at("qp_hdr").get<int32_t>();
    }
}

void webserver::resources::EncoderResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    WEBSERVER_LOG_INFO("Registering HTTP endpoints for EncoderResource");
    srv->Get("/encoder", std::function<nlohmann::json()>([this]() {
                 WEBSERVER_LOG_INFO("HTTP GET /encoder called");

                 hailo_encoder_config_t config = m_get_encoder_config();
                 encoder_control_t encoder_control = encoder_control_t::from_encoder_element_config(config);
                 nlohmann::json j;
                 to_json(j, encoder_control);
                 WEBSERVER_LOG_INFO("HTTP GET /encoder completed");
                 return j;
             }));

    srv->Post("/encoder", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                  WEBSERVER_LOG_INFO("HTTP POST /encoder called");
                  webserver::resources::EncoderResource::encoder_control_t encoder_control;
                  try
                  {
                      from_json(j_body, encoder_control);
                  }
                  catch (const std::exception &e)
                  {
                      WEBSERVER_LOG_ERROR("Failed to parse json body to encoder_control_t");
                  }
                  on_resource_change(EventType::CHANGED_RESOURCE_ENCODER,
                                     std::make_shared<webserver::resources::EncoderResource::EncoderResourceState>(
                                         EncoderResourceState(encoder_control)));
                  WEBSERVER_LOG_INFO("HTTP POST /encoder completed");
                  return j_body;
              }));
}
