#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/analytics/analytic_metadata_sender.hpp"
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <cstring>
#include <sstream>

namespace hailo_analytics::analytics::analytic_metadata_sender
{

// ============================================================================
// Helper function to get local IP address
// ============================================================================

static std::string get_local_ip_address()
{
    struct ifaddrs *if_addr_struct = nullptr;
    struct ifaddrs *ifa = nullptr;
    void *tmp_addr_ptr = nullptr;
    std::string result = "10.0.0.1"; // fallback

    getifaddrs(&if_addr_struct);

    for (ifa = if_addr_struct; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_addr)
        {
            continue;
        }

        // Check for IPv4 address
        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            tmp_addr_ptr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            char address_buffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, tmp_addr_ptr, address_buffer, INET_ADDRSTRLEN);

            // Skip loopback interface
            if (std::strcmp(address_buffer, "127.0.0.1") != 0)
            {
                result = address_buffer;
                break;
            }
        }
    }

    if (if_addr_struct != nullptr)
    {
        freeifaddrs(if_addr_struct);
    }

    return result;
}

// ============================================================================
// analytic_metadata_config_t implementation
// ============================================================================

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

// ============================================================================
// zeromq_config_t implementation
// ============================================================================

void zeromq_config_t::merge_from(const zeromq_config_t &other)
{
    if (other.stage_name)
        stage_name = *other.stage_name;
    if (other.queue_size)
        queue_size = *other.queue_size;
    if (other.leaky)
        leaky = *other.leaky;
    if (other.trace)
        trace = *other.trace;
    if (other.print_fps)
        print_fps = *other.print_fps;
    if (other.mode)
        mode = *other.mode;
    if (other.pub_address)
        pub_address = *other.pub_address;
    if (other.sub_address)
        sub_address = *other.sub_address;
}

void zeromq_config_t::apply_to(sinks_stage::ZmqCommStageBuild::Builder &b) const
{
    if (stage_name)
        b.set_stage_name(*stage_name);
    if (queue_size)
        b.set_queue_size(*queue_size);
    if (leaky)
        b.set_leaky(*leaky);
    if (trace)
        b.set_trace_opt(*trace);
    if (print_fps)
        b.set_print_fps(*print_fps);
    if (mode)
        b.set_mode(*mode);
    if (pub_address)
        b.set_pub_address(*pub_address);
    if (sub_address)
        b.set_sub_address(*sub_address);
}

// ============================================================================
// analytic_metadata_zmq_sender_config_t implementation
// ============================================================================

void analytic_metadata_zmq_sender_config_t::merge_from(const analytic_metadata_zmq_sender_config_t &other)
{
    analytic_metadata_config.merge_from(other.analytic_metadata_config);
    zeromq_config.merge_from(other.zeromq_config);
}

void analytic_metadata_zmq_sender_config_t::apply_to(codecs_stage::AnalyticMetadataPackagerStageBuild::Builder &b) const
{
    analytic_metadata_config.apply_to(b);
}

void analytic_metadata_zmq_sender_config_t::apply_to(sinks_stage::ZmqCommStageBuild::Builder &b) const
{
    zeromq_config.apply_to(b);
}

// ============================================================================
// Pipeline generation functions
// ============================================================================

analytic_metadata_config_t base_analytic_metadata_packager_config()
{
    analytic_metadata_config_t config;

    config.stage_name = "analytic_metadata_stage";
    config.queue_size = 1;
    config.leaky = false;
    config.trace = true;

    return config;
}

zeromq_config_t base_zeromq_sender_config()
{
    zeromq_config_t config;

    config.stage_name = "zmq_sender_stage";
    config.queue_size = 1;
    config.leaky = false;
    config.trace = true;
    config.print_fps = false;
    config.mode = sinks_stage::ZmqCommStage::Mode::PUBLISHER;

    // Get the current system IP address and construct the address string
    std::string local_ip = get_local_ip_address();
    std::ostringstream address_stream;
    address_stream << "tcp://" << local_ip << ":7000";
    config.pub_address = address_stream.str();

    return config;
}

analytic_metadata_zmq_sender_config_t base_analytic_metadat_zmq_sender_config()
{
    analytic_metadata_zmq_sender_config_t config;

    config.analytic_metadata_config = base_analytic_metadata_packager_config();
    config.zeromq_config = base_zeromq_sender_config();

    return config;
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_analytic_metadata_sender_pipeline(const std::string &pipeline_name,
                                           std::optional<analytic_metadata_zmq_sender_config_t> user_configs)
{
    analytic_metadata_zmq_sender_config_t cfg = base_analytic_metadat_zmq_sender_config(); // default configs
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
    // Add analytic metadata stage to pipeline
    pip_builder.add_stage(analytic_metadata_stage_ptr);

    // Create zmq sender stage
    sinks_stage::ZmqCommStageBuild::Builder zmq_builder = sinks_stage::ZmqCommStageBuild::create();
    cfg.apply_to(zmq_builder);
    std::shared_ptr<sinks_stage::ZmqCommStage> zmq_stage_ptr = zmq_builder.buildptr();
    // Add zmq sender stage to pipeline
    pip_builder.add_stage(zmq_stage_ptr);

    // Connect analytic metadata stage to zero mq sender stage pipeline
    pip_builder.connect(*cfg.analytic_metadata_config.stage_name, *cfg.zeromq_config.stage_name);

    // Create the pipeline
    hailo_analytics::pipeline::PipelinePtr pipeline = pip_builder.build(pipeline_name, true);

    // Set the input and output stages
    pipeline->set_in_stage(analytic_metadata_stage_ptr);
    pipeline->set_out_stage(zmq_stage_ptr);

    return pipeline;
}

} // namespace hailo_analytics::analytics::analytic_metadata_sender
