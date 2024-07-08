#include "resources.hpp"
// #include "media_library/encoder_config.hpp"
#include <iostream>

void webserver::resources::to_json(nlohmann::json &j, const webserver::resources::EncoderResource::encoder_control_t &b)
{
    j = nlohmann::json{{"bitrate_control", b.bitrate_control},
                       {"bitrate", b.bitrate}};
}

void webserver::resources::from_json(const nlohmann::json &j, webserver::resources::EncoderResource::encoder_control_t &b)
{
    j.at("bitrate_control").get_to(b.bitrate_control);
    j.at("bitrate").get_to(b.bitrate);
}

webserver::resources::EncoderResource::EncoderResource(std::shared_ptr<webserver::resources::ConfigResource> configs) : Resource()
{
    m_config = configs->get_encoder_default_config();
}

webserver::resources::EncoderResource::encoder_control_t webserver::resources::EncoderResource::get_encoder_control()
{
    webserver::resources::EncoderResource::encoder_control_t ctrl{};
    ctrl.bitrate = m_config["rate_control"]["bitrate"]["target_bitrate"];
    ctrl.bitrate_control = m_config["rate_control"]["picture-rc"] == true ? webserver::resources::EncoderResource::CBR : webserver::resources::EncoderResource::VBR;
    return ctrl;
}

void webserver::resources::EncoderResource::set_encoder_control(webserver::resources::EncoderResource::encoder_control_t &encoder_control)
{
    m_config["rate_control"]["bitrate"]["target_bitrate"] = encoder_control.bitrate;
    m_config["rate_control"]["picture-rc"] = encoder_control.bitrate_control == webserver::resources::EncoderResource::VBR ? 0 : 1;
    m_config["rate_control"]["bitrate"]["tolerance_moving_bitrate"] = encoder_control.bitrate_control == webserver::resources::EncoderResource::VBR ? 2000 : 0;
    on_resource_change(std::make_shared<webserver::resources::ResourceState>(ConfigResourceState(this->to_string())));
}

void webserver::resources::EncoderResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->Get("/encoder/bitrate_control", std::function<nlohmann::json()>([this]()
                                                                         {
        nlohmann::json j = get_encoder_control();
        return j; }));

    srv->Post("/encoder/bitrate_control", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body)
                                                                                                {
        webserver::resources::EncoderResource::encoder_control_t encoder_control;
        try
        {
            encoder_control = j_body.get<webserver::resources::EncoderResource::encoder_control_t>();
        }
        catch (const std::exception &e)
        {
            throw std::runtime_error("Failed to parse json body to encoder_control_t");
        }

        set_encoder_control(encoder_control);
        encoder_control = get_encoder_control();
        nlohmann::json j_out = encoder_control;
        return j_out; }));
}

void webserver::resources::EncoderResource::apply_config(GstElement *encoder_element)
{
    // gpointer value = nullptr;
    // g_object_get(G_OBJECT(encoder_element), "config", &value, NULL);
    // auto enc_config = m_config;
    // encoder_config_t *config = reinterpret_cast<encoder_config_t *>(value);
    // config->rate_control.bitrate.target_bitrate = enc_config["rate_control"]["bitrate"]["target_bitrate"];
    // config->rate_control.picture_rc = enc_config["rate_control"]["picture-rc"];
    // config->rate_control.bitrate.tolerance_moving_bitrate = enc_config["rate_control"]["bitrate"]["tolerance_moving_bitrate"];
    // g_object_set(G_OBJECT(encoder_element), "config", config, NULL);
}