#include "encoder.hpp"
#include "common/logger_macros.hpp"
#include <iostream>

// Quality preset to background_qp_delta mapping
const std::unordered_map<std::string, uint8_t> webserver::resources::EncoderResource::quality_to_qp_delta{
    {"LOW", 5},     // Minimal degradation for background
    {"MEDIUM", 10}, // Moderate degradation
    {"HIGH", 15}    // Maximal degradation
};

// ---- State management ----
// On initialization (set_encoder_query), m_config is synced from the encoder element's
// current configuration. For most parameters (rate control, quantization, intra_pic_rate),
// the encoder element is always the source of truth - GET reads live values from it.
//
// The exception is smart_encoder: m_config is the source of truth for ROI metadata
// (id, name, enabled/disabled status) because the encoder element only knows about
// active ROI rectangles - it has no concept of UI identity or disabled ROIs.
// On GET, basic encoder params come from the encoder element, but the smart_encoder
// section is overridden from m_config. On POST, the full smart_encoder state
// (including disabled ROIs) is persisted to m_config, and only enabled ROIs are
// forwarded to the encoder element via fill_encoder_element_config.

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

    // Extract smart encoder configuration
    // Note: This method is used during initial sync. ROI metadata (id/name) will be properly set in m_config.
    if (encoder_config.smart_encoder.enabled)
    {
        smart_encoder_ui_t smart_encoder_ui;
        smart_encoder_ui.global_enable = true;

        // Map background_qp_delta to quality preset (reverse lookup on single map)
        smart_encoder_ui.quality = "MEDIUM";
        for (const auto &[quality, delta] : quality_to_qp_delta)
        {
            if (delta == encoder_config.smart_encoder.background_qp_delta)
            {
                smart_encoder_ui.quality = quality;
                break;
            }
        }

        // Convert ROIs - generate temporary IDs if not in m_config yet
        for (size_t i = 0; i < encoder_config.smart_encoder.rois.size(); ++i)
        {
            const auto &roi = encoder_config.smart_encoder.rois[i];
            roi_ui_t roi_ui;
            roi_ui.id = "ROI " + std::to_string(i + 1);
            roi_ui.name = "ROI " + std::to_string(i + 1);
            roi_ui.status = true;
            roi_ui.roi = roi;
            smart_encoder_ui.rois.push_back(roi_ui);
        }

        control.smart_encoder = smart_encoder_ui;
    }

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

    // Handle Smart Encoder configuration
    if (this->smart_encoder.has_value() && this->smart_encoder->global_enable)
    {
        encoder_config.smart_encoder.enabled = true;

        // Map quality preset to background_qp_delta
        auto qp_it = quality_to_qp_delta.find(this->smart_encoder->quality);
        encoder_config.smart_encoder.background_qp_delta = (qp_it != quality_to_qp_delta.end()) ? qp_it->second : 10;

        // Filter and add only enabled ROIs
        encoder_config.smart_encoder.rois.clear();
        for (const auto &roi_ui : this->smart_encoder->rois)
        {
            if (roi_ui.status) // Only add enabled ROIs
            {
                encoder_config.smart_encoder.rois.push_back(roi_ui.roi);
            }
        }

        WEBSERVER_LOG_INFO("Smart Encoder configured: quality={}, background_qp_delta={}, enabled ROIs={}/{}",
                           this->smart_encoder->quality, encoder_config.smart_encoder.background_qp_delta,
                           encoder_config.smart_encoder.rois.size(), this->smart_encoder->rois.size());
    }
    else
    {
        encoder_config.smart_encoder.enabled = false;
        encoder_config.smart_encoder.rois.clear();
    }
}

std::string webserver::resources::EncoderResource::name()
{
    return "encoder";
}

webserver::resources::ResourceType webserver::resources::EncoderResource::get_type()
{
    return ResourceType::RESOURCE_ENCODER;
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

    // Sync initial state from encoder to m_config
    sync_config_from_encoder();
}

void webserver::resources::EncoderResource::sync_config_from_encoder()
{
    if (!m_get_encoder_config)
    {
        WEBSERVER_LOG_WARNING("Cannot sync config - encoder query not set");
        return;
    }

    hailo_encoder_config_t encoder_config = m_get_encoder_config();
    encoder_control_t control = encoder_control_t::from_encoder_element_config(encoder_config);

    to_json(m_config, control);

    WEBSERVER_LOG_INFO("Synced smart_encoder config from encoder to m_config");
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

    // Serialize smart encoder configuration
    if (b.smart_encoder.has_value())
    {
        j["smart_encoder"] = nlohmann::json{{"global_enable", b.smart_encoder->global_enable},
                                            {"quality", b.smart_encoder->quality},
                                            {"rois", nlohmann::json::array()}};

        for (const auto &roi : b.smart_encoder->rois)
        {
            j["smart_encoder"]["rois"].push_back(nlohmann::json{
                {"id", roi.id},
                {"name", roi.name},
                {"status", roi.status},
                {"roi", {{"x", roi.roi.x}, {"y", roi.roi.y}, {"width", roi.roi.width}, {"height", roi.roi.height}}}});
        }
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

    // Parse smart encoder configuration from UI
    if (j.contains("smart_encoder"))
    {
        const auto &smart = j.at("smart_encoder");
        EncoderResource::encoder_control_t::smart_encoder_ui_t smart_ui;
        smart_ui.global_enable = smart.at("global_enable").get<bool>();
        smart_ui.quality = smart.at("quality").get<std::string>();

        if (smart.contains("rois") && smart.at("rois").is_array())
        {
            for (const auto &roi_json : smart.at("rois"))
            {
                EncoderResource::encoder_control_t::roi_ui_t roi_ui;
                roi_ui.id = roi_json.at("id").get<std::string>();
                roi_ui.name = roi_json.at("name").get<std::string>();
                roi_ui.status = roi_json.at("status").get<bool>();

                const auto &roi_rect = roi_json.at("roi");
                roi_ui.roi.x = roi_rect.at("x").get<float>();
                roi_ui.roi.y = roi_rect.at("y").get<float>();
                roi_ui.roi.width = roi_rect.at("width").get<float>();
                roi_ui.roi.height = roi_rect.at("height").get<float>();

                smart_ui.rois.push_back(roi_ui);
            }
        }

        b.smart_encoder = smart_ui;
        WEBSERVER_LOG_INFO("Parsed smart encoder from UI: global_enable={}, quality={}, total_rois={}",
                           smart_ui.global_enable, smart_ui.quality, smart_ui.rois.size());
    }
}

void webserver::resources::EncoderResource::http_register(HTTPServer &srv)
{
    WEBSERVER_LOG_INFO("Registering HTTP endpoints for EncoderResource");
    srv.Get("/encoder", std::function<nlohmann::json()>([this]() {
                WEBSERVER_LOG_INFO("HTTP GET /encoder called");

                if (!m_get_encoder_config)
                {
                    WEBSERVER_LOG_ERROR("HTTP GET /encoder: encoder config query not set");
                    return nlohmann::json{{"error", "encoder config query not available"}};
                }

                // Get basic encoder params from encoder
                hailo_encoder_config_t config = m_get_encoder_config();
                encoder_control_t encoder_control = encoder_control_t::from_encoder_element_config(config);

                // Override smart_encoder section from m_config (source of truth for ROI metadata)
                if (m_config.contains("smart_encoder"))
                {
                    encoder_control_t temp_control;
                    from_json(m_config, temp_control);
                    encoder_control.smart_encoder = temp_control.smart_encoder;
                }

                nlohmann::json j;
                to_json(j, encoder_control);
                WEBSERVER_LOG_INFO("HTTP GET /encoder completed");
                return j;
            }));

    srv.Post("/encoder", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                 WEBSERVER_LOG_INFO("HTTP POST /encoder called");
                 webserver::resources::EncoderResource::encoder_control_t encoder_control;
                 try
                 {
                     from_json(j_body, encoder_control);

                     // Persist to m_config (source of truth for ROI metadata)
                     m_config.merge_patch(j_body);
                     WEBSERVER_LOG_INFO("Persisted encoder config to m_config");
                 }
                 catch (const std::exception &e)
                 {
                     WEBSERVER_LOG_ERROR("Failed to parse json body to encoder_control_t: {}", e.what());
                     return nlohmann::json{{"error", e.what()}};
                 }
                 on_resource_change(EventType::CHANGED_RESOURCE_ENCODER,
                                    std::make_shared<webserver::resources::EncoderResource::EncoderResourceState>(
                                        EncoderResourceState(encoder_control)));
                 WEBSERVER_LOG_INFO("HTTP POST /encoder completed");
                 return j_body;
             }));
}
