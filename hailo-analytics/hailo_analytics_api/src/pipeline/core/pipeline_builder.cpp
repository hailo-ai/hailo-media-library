#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"

#include <stdexcept>
#include <memory>

#include "hailo_analytics/pipeline/core/pipeline_exporter.hpp"
#include "hailo_analytics/utils/env_utils.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"

namespace hailo_analytics::pipeline
{

PipelineBuilder &PipelineBuilder::add_stage(const std::string &name, StagePtr stage, StageType type)
{
    if (!stage)
    {
        throw std::invalid_argument("Stage is null for name: " + name);
    }

    validate_and_add_stage(name, stage, type);

    return *this;
}

PipelineBuilder &PipelineBuilder::add_stage(StagePtr stage, StageType type)
{
    if (!stage)
    {
        throw std::invalid_argument("Stage pointer is null.");
    }

    validate_and_add_stage(stage->get_name(), stage, type);

    return *this;
}

PipelineBuilder &PipelineBuilder::connect(const std::string &sourceName, const std::string &targetName)
{
    if (m_allStages.find(sourceName) == m_allStages.end())
    {
        HAILO_ANALYTICS_LOG_ERROR("Source stage not found in all stages: {}", sourceName);
        throw std::invalid_argument("Source stage not found in all stages: " + sourceName);
    }
    if (m_allStages.find(targetName) == m_allStages.end())
    {
        HAILO_ANALYTICS_LOG_ERROR("Target stage not found in all stages: {}", targetName);
        throw std::invalid_argument("Target stage not found in all stages: " + targetName);
    }
    m_connections.emplace_back(sourceName, targetName);
    HAILO_ANALYTICS_LOG_INFO("Established generic connection: {} -> {}", sourceName, targetName);
    return *this;
}

PipelineBuilder &PipelineBuilder::connect(const std::string &sourceName, const std::string &streamId,
                                          const std::string &targetName)
{
    if (m_allStages.find(sourceName) == m_allStages.end())
    {
        HAILO_ANALYTICS_LOG_ERROR("Source stage not found in all stages: {}", sourceName);
        throw std::invalid_argument("Source stage not found in all stages: " + sourceName);
    }
    if (m_allStages.find(targetName) == m_allStages.end())
    {
        HAILO_ANALYTICS_LOG_ERROR("Target stage not found in all stages: {}", targetName);
        throw std::invalid_argument("Target stage not found in all stages: " + targetName);
    }
    m_streamIdConnections.emplace_back(sourceName, streamId, targetName);
    HAILO_ANALYTICS_LOG_INFO("Established stream-id connection: {} (stream: {}) -> {}", sourceName, streamId,
                             targetName);
    return *this;
}

PipelineBuilder &PipelineBuilder::connect_frontend(const std::string &frontendName, const std::string &streamId,
                                                   const std::string &targetName)
{
    if (m_allStages.find(frontendName) == m_allStages.end())
    {
        HAILO_ANALYTICS_LOG_ERROR("Source stage not found in all stages: {}", frontendName);
        throw std::invalid_argument("Source stage not found in all stages: " + frontendName);
    }
    if (m_allStages.find(targetName) == m_allStages.end())
    {
        HAILO_ANALYTICS_LOG_ERROR("Target stage not found in all stages: {}", targetName);
        throw std::invalid_argument("Target stage not found in all stages: " + targetName);
    }
    m_frontendSubscriptions.emplace_back(frontendName, streamId, targetName);
    HAILO_ANALYTICS_LOG_INFO("Established frontend subscription: {} (stream: {}) -> {}", frontendName, streamId,
                             targetName);
    return *this;
}

void PipelineBuilder::log_diagnostics() const
{
    // Single-stage pipelines are always valid - no connections needed
    if (m_allStages.size() == 1)
    {
        HAILO_ANALYTICS_LOG_DEBUG("Single-stage pipeline, no connectivity diagnostics needed");
        return;
    }

    // For multi-stage pipelines, check if any stages are disconnected
    for (const auto &pair : m_allStages)
    {
        const std::string &stageName = pair.first;
        bool used = false;

        // Check if the stage is used in generic connections
        for (const auto &conn : m_connections)
        {
            if (conn.first == stageName || conn.second == stageName)
            {
                used = true;
                break;
            }
        }

        // If not found in generic connections, check stream-id connections
        if (!used)
        {
            for (const auto &conn : m_streamIdConnections)
            {
                if (std::get<0>(conn) == stageName || std::get<2>(conn) == stageName)
                {
                    used = true;
                    break;
                }
            }
        }

        // If not found in generic connections, check frontend subscriptions
        if (!used)
        {
            for (const auto &sub : m_frontendSubscriptions)
            {
                if (std::get<0>(sub) == stageName || std::get<2>(sub) == stageName)
                {
                    used = true;
                    break;
                }
            }
        }

        if (!used)
        {
            HAILO_ANALYTICS_LOG_DEBUG("Stage '{}' is not connected to any other stage. "
                                      "This may be intentional for your pipeline design, or it could indicate "
                                      "a missing connection.",
                                      stageName);
        }
    }
}

void PipelineBuilder::validate_and_add_stage(const std::string &name, StagePtr stage, StageType type)
{
    if (m_allStages.find(name) != m_allStages.end())
    {
        throw std::invalid_argument("Stage already exists: " + name);
    }

    if (type != StageType::GENERAL)
    {
        m_stageTypes[name] = type;
    }

    m_allStages[name] = stage;
}

std::shared_ptr<Pipeline> PipelineBuilder::build(std::string name, bool trace_processing_operations)
{
    // Check if DOT graph export is enabled via environment variable
    if (utils::is_env_variable_on("HAILO_ANALYTICS_DUMP_DOT"))
    {
        // Get output directory (default to /tmp)
        std::string dot_dir = utils::get_env_variable("HAILO_ANALYTICS_DUMP_DOT_DIR", "/tmp");

        // Build output filename: <dir>/<pipeline_name>.dot
        std::string dot_filename = dot_dir + "/" + name + ".dot";

        HAILO_ANALYTICS_LOG_INFO("HAILO_ANALYTICS_DUMP_DOT enabled, exporting pipeline graph to: {}", dot_filename);
        export_to_dot(dot_filename);
    }

    HAILO_ANALYTICS_LOG_INFO("Building pipeline with {} stages", m_allStages.size());
    auto pipeline = std::make_shared<Pipeline>(name, trace_processing_operations);

    // Add all stages to the pipeline.
    for (auto &pair : m_allStages)
    {
        const std::string &name = pair.first;
        auto stage = pair.second;
        auto it = m_stageTypes.find(name);
        if (it != m_stageTypes.end())
        {
            pipeline->add_stage(stage, it->second);
            HAILO_ANALYTICS_LOG_DEBUG("Added stage {} with specific type", name);
        }
        else
        {
            pipeline->add_stage(pair.second);
            HAILO_ANALYTICS_LOG_DEBUG("Added stage {} with default type", name);
        }
    }

    // Establish generic connections.
    for (const auto &conn : m_connections)
    {
        const std::string &srcName = conn.first;
        const std::string &tgtName = conn.second;
        auto source = m_allStages.at(srcName);
        auto target = m_allStages.at(tgtName);
        source->add_subscriber(target);
        HAILO_ANALYTICS_LOG_INFO("Established generic connection: {} -> {}", srcName, tgtName);
    }

    // Establish stream-id-keyed connections.
    for (const auto &entry : m_streamIdConnections)
    {
        const std::string &srcName = std::get<0>(entry);
        const std::string &streamId = std::get<1>(entry);
        const std::string &tgtName = std::get<2>(entry);
        auto source = m_allStages.at(srcName);
        auto target = m_allStages.at(tgtName);
        source->add_subscriber(target, streamId);
        HAILO_ANALYTICS_LOG_INFO("Established stream-id connection: {} (stream: {}) -> {}", srcName, streamId, tgtName);
    }

    // Establish frontend subscriptions.
    for (const auto &entry : m_frontendSubscriptions)
    {
        const std::string &frontendName = std::get<0>(entry);
        const std::string &streamId = std::get<1>(entry);
        const std::string &targetName = std::get<2>(entry);

        auto frontendStage = m_allStages.at(frontendName);
        auto targetStage = m_allStages.at(targetName);
        frontendStage->add_subscriber(targetStage, streamId);
        HAILO_ANALYTICS_LOG_INFO("Established frontend subscription: {} (stream: {}) -> {}", frontendName, streamId,
                                 targetName);
    }

    // Log diagnostic warnings for potentially unintended configurations
    log_diagnostics();

    HAILO_ANALYTICS_LOG_INFO("Pipeline build completed with {} stages, {} connections, {} frontend subscriptions.",
                             m_allStages.size(), m_connections.size(), m_frontendSubscriptions.size());
    return pipeline;
}

PipelineBuilder &PipelineBuilder::export_to_dot(const std::string &filename)
{
    auto labeled_connections = m_frontendSubscriptions;
    labeled_connections.insert(labeled_connections.end(), m_streamIdConnections.begin(), m_streamIdConnections.end());
    PipelineExporter::export_to_dot(filename, m_allStages, m_connections, labeled_connections, m_stageTypes);
    return *this;
}

} // namespace hailo_analytics::pipeline
