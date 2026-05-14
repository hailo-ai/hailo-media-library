#include "hailo_analytics/analytics/analytic_metadata_ws_sender.hpp"

namespace hailo_analytics::analytics::analytic_metadata_ws_sender
{

void analytic_metadata_config_t::merge_from(const analytic_metadata_config_t &other)
{
    if (other.stage_name)
        stage_name = *other.stage_name;
    if (other.queue_size)
        queue_size = *other.queue_size;
    if (other.leaky)
        leaky = *other.leaky;
    if (other.trace)
        trace = *other.trace;
}

void analytic_metadata_config_t::apply_to(codecs_stage::AnalyticMetadataPackagerStageBuild::Builder &b) const
{
    if (stage_name)
        b.set_stage_name(*stage_name);
    if (queue_size)
        b.set_queue_size_opt(*queue_size);
    if (leaky)
        b.set_leaky_opt(*leaky);
    if (trace)
        b.set_trace_opt(*trace);
}

analytic_metadata_config_t base_analytic_metadata_packager_config()
{
    analytic_metadata_config_t config;

    config.stage_name = "analytic_metadata_stage";
    config.queue_size = 1;
    config.leaky = false;
    config.trace = true;

    return config;
}

void websocket_config_t::merge_from(const websocket_config_t &other)
{
    if (other.stage_name)
        stage_name = *other.stage_name;
    if (other.port)
        port = *other.port;
    if (other.host)
        host = *other.host;
    if (other.queue_size)
        queue_size = *other.queue_size;
    if (other.leaky)
        leaky = *other.leaky;
    if (other.max_message_size)
        max_message_size = *other.max_message_size;
}

void websocket_config_t::apply_to(sinks_stage::WebSocketSinkStageBuild::Builder &b) const
{
    if (stage_name)
        b.set_stage_name(*stage_name);
    if (port)
        b.set_port_opt(*port);
    if (host)
        b.set_host_opt(*host);
    if (queue_size)
        b.set_queue_size_opt(*queue_size);
    if (leaky)
        b.set_leaky_opt(*leaky);
    if (max_message_size)
        b.set_max_message_size_opt(*max_message_size);
}

void analytic_metadata_ws_sender_config_t::merge_from(const analytic_metadata_ws_sender_config_t &other)
{
    analytic_metadata_config.merge_from(other.analytic_metadata_config);
    websocket_config.merge_from(other.websocket_config);
}

void analytic_metadata_ws_sender_config_t::apply_to(codecs_stage::AnalyticMetadataPackagerStageBuild::Builder &b) const
{
    analytic_metadata_config.apply_to(b);
}

void analytic_metadata_ws_sender_config_t::apply_to(sinks_stage::WebSocketSinkStageBuild::Builder &b) const
{
    websocket_config.apply_to(b);
}

websocket_config_t base_websocket_sender_config()
{
    websocket_config_t config;

    config.stage_name = "ws_sender_stage";
    config.port = 8765;
    config.host = "0.0.0.0";
    config.queue_size = 1;
    config.leaky = true;

    return config;
}

analytic_metadata_ws_sender_config_t base_analytic_metadata_ws_sender_config()
{
    analytic_metadata_ws_sender_config_t config;

    config.analytic_metadata_config = base_analytic_metadata_packager_config();
    config.websocket_config = base_websocket_sender_config();

    return config;
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_analytic_metadata_ws_sender_pipeline(const std::string &pipeline_name,
                                              std::optional<analytic_metadata_ws_sender_config_t> user_configs)
{
    analytic_metadata_ws_sender_config_t cfg = base_analytic_metadata_ws_sender_config(); // default configs
    if (user_configs.has_value())
    {
        cfg.merge_from(user_configs.value()); // user overrides
    }

    hailo_analytics::pipeline::PipelineBuilder pip_builder;

    // Create analytic metadata packager stage
    codecs_stage::AnalyticMetadataPackagerStageBuild::Builder analytic_metadata_builder =
        codecs_stage::AnalyticMetadataPackagerStageBuild::create();
    cfg.apply_to(analytic_metadata_builder);
    std::shared_ptr<codecs_stage::AnalyticMetadataPackagerStage> analytic_metadata_stage_ptr =
        analytic_metadata_builder.buildptr();
    pip_builder.add_stage(analytic_metadata_stage_ptr);

    // Create WebSocket sink stage
    sinks_stage::WebSocketSinkStageBuild::Builder ws_builder = sinks_stage::WebSocketSinkStageBuild::create();
    cfg.apply_to(ws_builder);
    std::shared_ptr<sinks_stage::WebSocketSinkStage> ws_stage_ptr = ws_builder.buildptr();
    pip_builder.add_stage(ws_stage_ptr);

    // Connect analytic metadata stage to WebSocket sink stage
    pip_builder.connect(*cfg.analytic_metadata_config.stage_name, *cfg.websocket_config.stage_name);

    // Create the pipeline
    hailo_analytics::pipeline::PipelinePtr pipeline = pip_builder.build(pipeline_name, true);

    // Set the input and output stages
    pipeline->set_in_stage(analytic_metadata_stage_ptr);
    pipeline->set_out_stage(ws_stage_ptr);

    return pipeline;
}

} // namespace hailo_analytics::analytics::analytic_metadata_ws_sender
