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

webserver::resources::EncoderResource::EncoderResource() : Resource()
{
    m_default_config = R"({
        "config": {
            "output_stream": {
                "codec": "CODEC_TYPE_H264",
                "profile": "VCENC_H264_MAIN_PROFILE",
                "level": "5.0",
                "bit_depth_luma": 8,
                "bit_depth_chroma": 8,
                "stream_type": "bytestream"
            }
        },
        "gop_config": {
            "gop_size": 1,
            "b_frame_qp_delta": 0
        },
        "coding_control": {
            "sei_messages": true,
            "deblocking_filter": {
                "type": "DEBLOCKING_FILTER_ENABLED",
                "tc_offset": -2,
                "beta_offset": 5,
                "deblock_override": false
            },
            "intra_area": {
                "enable": false,
                "top": 0,
                "left": 0,
                "bottom": 0,
                "right": 0
            },
            "ipcm_area1": {
                "enable": false,
                "top": 0,
                "left": 0,
                "bottom": 0,
                "right": 0
            },
            "ipcm_area2": {
                "enable": false,
                "top": 0,
                "left": 0,
                "bottom": 0,
                "right": 0
            },
            "roi_area1": {
                "enable": false,
                "top": 0,
                "left": 0,
                "bottom": 0,
                "right": 0,
                "qp_delta": 0
            },
            "roi_area2": {
                "enable": false,
                "top": 0,
                "left": 0,
                "bottom": 0,
                "right": 0,
                "qp_delta": 0
            }
        },
        "rate_control": {
            "picture_rc": true,
            "picture_skip": false,
            "ctb_rc": true,
            "block_rc_size": 64,
            "hrd": false,
            "hrd_cpb_size": 0,
            "monitor_frames": 30,
            "gop_length": 30,
            "quantization": {
                "qp_min": 15,
                "qp_max": 48,
                "qp_hdr": 26,
                "intra_qp_delta": 0,
                "fixed_intra_qp": 0
            },
            "bitrate": {
                "target_bitrate": 10000000,
                "bit_var_range_i": 10,
                "bit_var_range_p": 10,
                "bit_var_range_b": 10,
                "tolerance_moving_bitrate": 2000
            }
        }
    })";
    m_config = nlohmann::json::parse(m_default_config);
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