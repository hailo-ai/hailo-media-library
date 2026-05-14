#include "hailo_analytics/pipeline/core/pipeline_exporter.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include <iostream>

namespace hailo_analytics::pipeline
{

// Private helper functions
std::string PipelineExporter::build_full_stage_name(const std::string &parent_prefix, const std::string &stage_name)
{
    return parent_prefix.empty() ? stage_name : parent_prefix + "::" + stage_name;
}

void PipelineExporter::get_node_style(const std::unordered_map<std::string, StageType> &stageTypes,
                                      const std::string &stage_name, std::string &shape, std::string &color)
{
    shape = "box";
    color = "black";

    auto type_it = stageTypes.find(stage_name);
    if (type_it != stageTypes.end())
    {
        if (type_it->second == StageType::SOURCE)
        {
            shape = "ellipse";
            color = "green";
        }
        else if (type_it->second == StageType::SINK)
        {
            shape = "ellipse";
            color = "red";
        }
    }
}

void PipelineExporter::collect_stage_subscribers(const StagePtr &stage, const std::string &full_stage_name,
                                                 DotExportContext &context)
{
    // Check if it's a FrontendStage with stream-based subscribers
    auto frontend_stage = std::dynamic_pointer_cast<hailo_analytics::pipeline::sources::FrontendStage>(stage);
    if (frontend_stage)
    {
        // Mark this as a frontend stage for later stream connection identification
        context.frontend_stages.insert(stage);

        // Collect all stream subscribers with their stream labels
        for (const auto &stream_subs : frontend_stage->get_stream_subscribers())
        {
            const std::string &stream_id = stream_subs.first;
            for (const auto &subscriber : stream_subs.second)
            {
                context.discovered_internal_connections.push_back(
                    std::make_tuple(full_stage_name, subscriber, stream_id));
            }
        }
        return;
    }

    // Regular ThreadedStage — preserve per-subscriber stream-id labels when present (e.g. for
    // SplitStreamsStage, which dispatches to subscribers by stream id).
    auto threaded_stage = std::dynamic_pointer_cast<ThreadedStage>(stage);
    if (threaded_stage)
    {
        const auto &subscribers = threaded_stage->get_subscribers();
        const auto &subscriber_stream_ids = threaded_stage->get_subscriber_stream_ids();
        for (size_t i = 0; i < subscribers.size(); ++i)
        {
            const std::string label = (i < subscriber_stream_ids.size() && subscriber_stream_ids[i].has_value())
                                          ? *subscriber_stream_ids[i]
                                          : std::string();
            context.discovered_internal_connections.push_back(std::make_tuple(full_stage_name, subscribers[i], label));
        }
    }
}

void PipelineExporter::write_cluster_header(std::ofstream &dotfile, const std::string &indent,
                                            const std::string &stage_name)
{
    dotfile << indent << "subgraph cluster_" << stage_name << " {" << std::endl;
    dotfile << indent << "  label=\"" << stage_name << "\";" << std::endl;
    dotfile << indent << "  style=filled;" << std::endl;
    dotfile << indent << "  color=lightgrey;" << std::endl;
    dotfile << indent << "  fontsize=12;" << std::endl;
    dotfile << indent << "  labeljust=l;" << std::endl;
    dotfile << indent << "  node [style=filled,color=white];" << std::endl;
    dotfile << std::endl;
}

void PipelineExporter::write_leaf_stage_node(std::ofstream &dotfile, const std::string &indent,
                                             const std::string &full_name, const std::string &stage_name,
                                             const std::string &shape, const std::string &color)
{
    dotfile << indent << "\"" << full_name << "\" [label=\"" << stage_name << "\""
            << ", shape=" << shape << ", color=" << color << ", fillcolor=white, style=filled];" << std::endl;
}

void PipelineExporter::export_nested_pipeline(std::ofstream &dotfile, const std::string &stage_name,
                                              const std::shared_ptr<Pipeline> &pipeline,
                                              const std::unordered_map<std::string, StageType> &stageTypes,
                                              const std::string &parent_prefix, DotExportContext &context,
                                              int indent_level)
{
    std::string indent(indent_level * 2, ' ');

    // Create GraphViz cluster for the nested pipeline
    write_cluster_header(dotfile, indent, stage_name);

    // Build map of stages within this pipeline
    std::unordered_map<std::string, StagePtr> nested_stages;
    for (const auto &internal_stage : pipeline->get_stages())
    {
        nested_stages[internal_stage->get_name()] = internal_stage;
    }

    // Recurse into the nested pipeline to export its stages
    std::string nested_prefix = build_full_stage_name(parent_prefix, stage_name);
    export_stages_recursively(dotfile, nested_stages, stageTypes, nested_prefix, context, indent_level + 1);

    // Close the cluster
    dotfile << indent << "}" << std::endl;

    // Register pipeline's output stage for connection resolution
    // When connecting TO this pipeline, we actually connect to its output stage
    if (pipeline->get_out_stage())
    {
        std::string out_stage_name = pipeline->get_out_stage()->get_name();
        std::string full_out_stage_name = nested_prefix + "::" + out_stage_name;
        context.short_name_to_full_name[stage_name] = full_out_stage_name;
    }
}

void PipelineExporter::export_leaf_stage(std::ofstream &dotfile, const std::string &stage_name, const StagePtr &stage,
                                         const std::unordered_map<std::string, StageType> &stageTypes,
                                         const std::string &parent_prefix, DotExportContext &context, int indent_level)
{
    std::string indent(indent_level * 2, ' ');
    std::string full_name = build_full_stage_name(parent_prefix, stage_name);

    // Determine node appearance based on stage type (SOURCE/SINK/default)
    std::string shape, color;
    get_node_style(stageTypes, stage_name, shape, color);

    // Write the node to DOT file
    write_leaf_stage_node(dotfile, indent, full_name, stage_name, shape, color);

    // Register this stage in our lookup maps
    context.short_name_to_full_name[stage_name] = full_name; // Short name lookup
    context.short_name_to_full_name[full_name] = full_name;  // Full name lookup (idempotent)
    context.stage_pointer_to_full_name[stage] = full_name;   // Pointer-based lookup

    // Discover and track connections from this stage to its subscribers
    collect_stage_subscribers(stage, full_name, context);
}

void PipelineExporter::export_stages_recursively(std::ofstream &dotfile,
                                                 const std::unordered_map<std::string, StagePtr> &stages,
                                                 const std::unordered_map<std::string, StageType> &stageTypes,
                                                 const std::string &parent_prefix, DotExportContext &context,
                                                 int indent_level)
{
    for (const auto &stage_pair : stages)
    {
        const std::string &stage_name = stage_pair.first;
        const StagePtr &stage = stage_pair.second;

        // Check if this stage is actually a pipeline containing other stages
        auto nested_pipeline = std::dynamic_pointer_cast<Pipeline>(stage);

        if (nested_pipeline && nested_pipeline->get_stages().size() > 0)
        {
            export_nested_pipeline(dotfile, stage_name, nested_pipeline, stageTypes, parent_prefix, context,
                                   indent_level);
        }
        else
        {
            export_leaf_stage(dotfile, stage_name, stage, stageTypes, parent_prefix, context, indent_level);
        }
    }
}

std::string PipelineExporter::resolve_target_stage_pointer(const StagePtr &target_stage,
                                                           const DotExportContext &context)
{
    // If target is a pipeline, resolve to its input stage (pipelines are entered through their in_stage)
    auto target_pipeline = std::dynamic_pointer_cast<Pipeline>(target_stage);
    if (target_pipeline && target_pipeline->get_in_stage())
    {
        auto it = context.stage_pointer_to_full_name.find(target_pipeline->get_in_stage());
        if (it != context.stage_pointer_to_full_name.end())
        {
            return it->second;
        }
    }

    // Regular stage - look up its full hierarchical name
    auto it = context.stage_pointer_to_full_name.find(target_stage);
    if (it != context.stage_pointer_to_full_name.end())
    {
        return it->second;
    }

    // Fallback if not found in mapping
    return target_stage->get_name();
}

std::string PipelineExporter::resolve_stage_name_through_pipeline(
    const std::string &stage_name, const std::unordered_map<std::string, StagePtr> &allStages, bool is_source)
{
    auto stage_it = allStages.find(stage_name);
    if (stage_it == allStages.end())
    {
        return stage_name;
    }

    auto pipeline = std::dynamic_pointer_cast<Pipeline>(stage_it->second);
    if (!pipeline)
    {
        return stage_name;
    }

    // For source connections, use output stage; for target connections, use input stage
    StagePtr resolved_stage = is_source ? pipeline->get_out_stage() : pipeline->get_in_stage();
    if (resolved_stage)
    {
        return stage_name + "::" + resolved_stage->get_name();
    }

    return stage_name;
}

std::string PipelineExporter::lookup_full_stage_name(const std::string &short_or_partial_name,
                                                     const DotExportContext &context)
{
    auto it = context.short_name_to_full_name.find(short_or_partial_name);
    return (it != context.short_name_to_full_name.end()) ? it->second : short_or_partial_name;
}

void PipelineExporter::write_edge(std::ofstream &dotfile, const std::string &source, const std::string &target,
                                  const std::string &attributes)
{
    dotfile << "  \"" << source << "\" -> \"" << target << "\"";
    if (!attributes.empty())
    {
        dotfile << " [" << attributes << "]";
    }
    dotfile << ";" << std::endl;
}

void PipelineExporter::write_internal_connections(std::ofstream &dotfile, const DotExportContext &context)
{
    for (const auto &conn : context.discovered_internal_connections)
    {
        const std::string &source_full_name = std::get<0>(conn);
        const StagePtr &target_stage_ptr = std::get<1>(conn);
        const std::string &stream_label = std::get<2>(conn);

        std::string target_full_name = resolve_target_stage_pointer(target_stage_ptr, context);

        // Add stream label for frontend connections
        std::string attributes = "";
        if (!stream_label.empty())
        {
            attributes = "label=\"" + stream_label + "\"";
        }

        write_edge(dotfile, source_full_name, target_full_name, attributes);
    }
}

void PipelineExporter::write_regular_connections(std::ofstream &dotfile,
                                                 const std::vector<std::pair<std::string, std::string>> &connections,
                                                 const std::unordered_map<std::string, StagePtr> &allStages,
                                                 const DotExportContext &context)
{
    for (const auto &conn : connections)
    {
        // Resolve pipeline names to actual entry/exit stages
        std::string source_name = resolve_stage_name_through_pipeline(conn.first, allStages, true);
        std::string target_name = resolve_stage_name_through_pipeline(conn.second, allStages, false);

        // Look up full hierarchical names
        source_name = lookup_full_stage_name(source_name, context);
        target_name = lookup_full_stage_name(target_name, context);

        write_edge(dotfile, source_name, target_name);
    }
}

void PipelineExporter::write_frontend_subscriptions(
    std::ofstream &dotfile, const std::vector<std::tuple<std::string, std::string, std::string>> &subscriptions,
    const std::unordered_map<std::string, StagePtr> &allStages, const DotExportContext &context)
{
    for (const auto &sub : subscriptions)
    {
        const std::string &stream_id = std::get<1>(sub);

        // Resolve pipeline names to actual entry/exit stages
        std::string source_name = resolve_stage_name_through_pipeline(std::get<0>(sub), allStages, true);
        std::string target_name = resolve_stage_name_through_pipeline(std::get<2>(sub), allStages, false);

        // Look up full hierarchical names
        source_name = lookup_full_stage_name(source_name, context);
        target_name = lookup_full_stage_name(target_name, context);

        write_edge(dotfile, source_name, target_name, "label=\"" + stream_id + "\"");
    }
}

// Main export function
void PipelineExporter::export_to_dot(
    const std::string &filename, const std::unordered_map<std::string, StagePtr> &allStages,
    const std::vector<std::pair<std::string, std::string>> &connections,
    const std::vector<std::tuple<std::string, std::string, std::string>> &frontendSubscriptions,
    const std::unordered_map<std::string, StageType> &stageTypes)
{
    std::ofstream dotfile(filename);
    if (!dotfile.is_open())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to open file for writing: {}", filename);
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
        return;
    }

    // Write DOT header
    dotfile << "digraph pipeline {" << std::endl;
    dotfile << "  rankdir=LR;" << std::endl;
    dotfile << "  node [shape=box];" << std::endl;
    dotfile << std::endl;

    DotExportContext context;

    export_stages_recursively(dotfile, allStages, stageTypes, "", context);
    dotfile << std::endl;

    write_internal_connections(dotfile, context);
    dotfile << std::endl;

    write_regular_connections(dotfile, connections, allStages, context);
    write_frontend_subscriptions(dotfile, frontendSubscriptions, allStages, context);

    // Close DOT file
    dotfile << "}" << std::endl;
    dotfile.close();

    HAILO_ANALYTICS_LOG_INFO("Pipeline graph exported to: {}", filename);
    std::cout << "Pipeline graph exported to: " << filename << std::endl;
}

} // namespace hailo_analytics::pipeline
