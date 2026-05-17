#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/analytics/vision.hpp"

namespace hailo_analytics::analytics::vision
{

// ============================================================================
// frontend_config_t implementation
// ============================================================================

void frontend_config_t::merge_from(const frontend_config_t &other)
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

void frontend_config_t::apply_to(sources::FrontendStageBuild::Builder &b) const
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

// ============================================================================
// encoder_config_t implementation
// ============================================================================

void encoder_config_t::merge_from(const encoder_config_t &other)
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

void encoder_config_t::apply_to(codecs::EncoderStageBuild::Builder &b) const
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

// ============================================================================
// udp_config_t implementation
// ============================================================================

void udp_config_t::merge_from(const udp_config_t &other)
{
    if (other.stage_name)
        stage_name = *other.stage_name;
    if (other.queue_size)
        queue_size = *other.queue_size;
    if (other.leaky)
        leaky = *other.leaky;
    if (other.print_fps)
        print_fps = *other.print_fps;
    if (other.trace)
        trace = *other.trace;
    if (other.host)
        host = *other.host;
    if (other.port)
        port = *other.port;
    if (other.encoding_type)
        encoding_type = *other.encoding_type;
}

void udp_config_t::apply_to(sinks::UdpStageBuild::Builder &b) const
{
    if (stage_name)
        b.set_stage_name(*stage_name);
    if (queue_size)
        b.set_queue_size_opt(*queue_size);
    if (leaky)
        b.set_leaky_opt(*leaky);
    if (print_fps)
        b.set_printfps_opt(*print_fps);
    if (trace)
        b.set_trace_opt(*trace);
}

// ============================================================================
// vision_output_config_t implementation
// ============================================================================

void vision_output_config_t::merge_from(const vision_output_config_t &other)
{
    encoder_config.merge_from(other.encoder_config);
    udp_config.merge_from(other.udp_config);
}

void vision_output_config_t::apply_to(codecs::EncoderStageBuild::Builder &b) const
{
    encoder_config.apply_to(b);
}

void vision_output_config_t::apply_to(sinks::UdpStageBuild::Builder &b) const
{
    udp_config.apply_to(b);
}

// ============================================================================
// vision_config_t implementation
// ============================================================================

void vision_config_t::merge_from(const vision_config_t &other)
{
    frontend_config.merge_from(other.frontend_config);

    // If other has any outputs specified, it becomes the authoritative list
    // Only outputs present in 'other' will remain, others are removed
    // Example:
    //   Current outputs: { "sink0", "sink1", "sink2" }
    //   Other outputs: { "sink1", "sink2" }
    //   Resulting outputs (merged): { "sink1", "sink2" }
    // Create a new map with only the outputs from 'other'
    std::map<output_stream_id_t, vision_output_config_t> new_outputs;

    for (const auto &[stream_id, output_config] : other.outputs)
    {
        // If this stream_id exists in our current outputs, merge the configs
        if (outputs.find(stream_id) != outputs.end())
        {
            new_outputs[stream_id] = outputs[stream_id];
            new_outputs[stream_id].merge_from(output_config);
        }
        else
        {
            // Otherwise, just use the config from 'other'
            new_outputs[stream_id] = output_config;
        }
    }

    // Replace our outputs with the new filtered map
    outputs = std::move(new_outputs);
}

void vision_config_t::apply_to(sources::FrontendStageBuild::Builder &b) const
{
    frontend_config.apply_to(b);
}

void vision_config_t::apply_to(codecs::EncoderStageBuild::Builder &b, const output_stream_id_t &stream_id) const
{
    auto it = outputs.find(stream_id);
    if (it != outputs.end())
        it->second.apply_to(b);
}

void vision_config_t::apply_to(sinks::UdpStageBuild::Builder &b, const output_stream_id_t &stream_id) const
{
    auto it = outputs.find(stream_id);
    if (it != outputs.end())
        it->second.apply_to(b);
}

// ============================================================================
// Pipeline generation functions
// ============================================================================

vision_output_config_t base_vision_output_config(std::string output_stream_id, int base_port)
{
    vision_output_config_t config;

    config.encoder_config.stage_name = "enc_" + output_stream_id;
    config.encoder_config.queue_size = 5;
    config.encoder_config.leaky = true;
    config.encoder_config.trace = true;

    config.udp_config.stage_name = "udp_" + output_stream_id;
    config.udp_config.queue_size = 5;
    config.udp_config.leaky = true;
    config.udp_config.print_fps = true;
    config.udp_config.trace = true;

    config.udp_config.host = "10.0.0.2";
    config.udp_config.port = PORT_FROM_ID(output_stream_id, base_port);
    config.udp_config.encoding_type = hailo_analytics::pipeline::sinks::EncodingType::H264;

    return config;
}

vision_config_t base_vision_config(std::vector<frontend_output_stream_t> frontend_streams, int base_port)
{
    vision_config_t config;

    config.frontend_config.stage_name = "frontend_stage";
    config.frontend_config.queue_size = 10;
    config.frontend_config.leaky = false;
    config.frontend_config.trace = true;

    // Add one default output for each frontend stream
    for (const auto &stream : frontend_streams)
    {
        config.outputs[stream.id] = base_vision_output_config(stream.id, base_port);
    }

    return config;
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_vision_output_pipeline(MediaLibraryInterfacePtr media_library, const output_stream_id_t &stream_id,
                                const std::string &pipeline_name, std::optional<vision_output_config_t> user_configs)
{
    vision_output_config_t cfg = base_vision_output_config(); // default configs
    if (user_configs.has_value())
    {
        cfg.merge_from(user_configs.value()); // user overrides
    }

    // Create pipeline builder
    hailo_analytics::pipeline::PipelineBuilder pip_builder;

    // Create encoder stage
    codecs::EncoderStageBuild::Builder encoder_builder = codecs::EncoderStageBuild::create();
    cfg.apply_to(encoder_builder);
    std::shared_ptr<codecs::EncoderStage> encoder_stage = encoder_builder.buildptr();

    // Configure the encoder stage
    hailo_analytics::pipeline::AppStatus status = encoder_stage->configure(media_library, stream_id);
    if (status != hailo_analytics::pipeline::AppStatus::SUCCESS)
    {
        return tl::unexpected(status);
    }

    // Create UDP sink stage
    sinks::UdpStageBuild::Builder udp_builder = sinks::UdpStageBuild::create();
    cfg.apply_to(udp_builder);
    std::shared_ptr<sinks::UdpStage> udp_stage = udp_builder.buildptr();

    // Configure the UDP stage
    status = udp_stage->configure(*cfg.udp_config.host, *cfg.udp_config.port, *cfg.udp_config.encoding_type);
    if (status != hailo_analytics::pipeline::AppStatus::SUCCESS)
    {
        return tl::unexpected(status);
    }

    // Add stages to pipeline builder
    pip_builder.add_stage(encoder_stage).add_stage(udp_stage);

    // Connect encoder to UDP sink
    pip_builder.connect(*cfg.encoder_config.stage_name, *cfg.udp_config.stage_name);

    // Create the pipeline
    hailo_analytics::pipeline::PipelinePtr pipeline = pip_builder.build(pipeline_name, true);

    // Set the input and output stages
    pipeline->set_in_stage(encoder_stage);
    pipeline->set_out_stage(udp_stage);

    return pipeline;
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> generate_vision_pipeline(
    MediaLibraryInterfacePtr media_library, const std::string &pipeline_name,
    std::optional<vision_config_t> user_configs)
{
    // Get output streams from frontend
    auto output_streams = media_library->get_frontend_output_streams();
    if (!output_streams.has_value())
    {
        return tl::unexpected(hailo_analytics::pipeline::AppStatus::CONFIGURATION_ERROR);
    }

    vision_config_t cfg = base_vision_config(output_streams.value()); // default configs
    if (user_configs.has_value())
    {
        cfg.merge_from(user_configs.value()); // user overrides
    }

    // Create pipeline builder
    hailo_analytics::pipeline::PipelineBuilder pip_builder;

    // Create frontend stage
    sources::FrontendStageBuild::Builder frontend_builder = sources::FrontendStageBuild::create();
    cfg.apply_to(frontend_builder);
    std::shared_ptr<sources::FrontendStage> frontend_stage = frontend_builder.buildptr();

    // Configure the frontend stage
    hailo_analytics::pipeline::AppStatus status = frontend_stage->configure(media_library);
    if (status != hailo_analytics::pipeline::AppStatus::SUCCESS)
    {
        return tl::unexpected(status);
    }

    // Add frontend to pipeline
    pip_builder.add_stage(frontend_stage, hailo_analytics::pipeline::StageType::SOURCE);

    // Create all output branches as separate pipelines
    for (const auto &[stream_id, output_config] : cfg.outputs)
    {
        // Generate output pipeline (encoder -> UDP)
        std::string output_pipeline_name = pipeline_name + "_output_" + stream_id;
        auto output_result =
            generate_vision_output_pipeline(media_library, stream_id, output_pipeline_name, output_config);
        if (!output_result.has_value())
        {
            return tl::unexpected(output_result.error());
        }

        hailo_analytics::pipeline::PipelinePtr output_pipeline = output_result.value();

        pip_builder.add_stage(output_pipeline, hailo_analytics::pipeline::StageType::SINK);

        // Connect the frontend to the output pipeline using pip_builder
        HAILO_ANALYTICS_LOG_INFO("subscribing to frontend for {}", stream_id);
        pip_builder.connect_frontend(*cfg.frontend_config.stage_name, stream_id, output_pipeline_name);
    }

    // Create the main pipeline
    hailo_analytics::pipeline::PipelinePtr pipeline = pip_builder.build(pipeline_name, true);

    // Set the input stage (frontend)
    pipeline->set_in_stage(frontend_stage);
    // The frontend broadcasts to any future subscribers
    pipeline->set_out_stage(frontend_stage);

    return pipeline;
}

} // namespace hailo_analytics::analytics::vision
