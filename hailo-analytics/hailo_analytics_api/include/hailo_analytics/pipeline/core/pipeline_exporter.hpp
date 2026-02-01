#pragma once

#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <tuple>
#include <unordered_set>
#include <fstream>

namespace hailo_analytics::pipeline
{

/**
 * @brief Internal utility class for exporting pipeline graphs to DOT format.
 *
 * This class handles the visualization of pipeline structures by exporting them
 * to GraphViz DOT format. It supports nested pipelines, different stage types,
 * and multiple connection types (regular and frontend subscriptions).
 *
 * @note This is an internal-only API used by PipelineBuilder.
 */
class PipelineExporter
{
  public:
    /**
     * @brief Export a pipeline graph to a DOT file.
     *
     * @param filename Path to the output DOT file
     * @param allStages Map of stage names to stage pointers
     * @param connections Vector of regular connections (source, target)
     * @param frontendSubscriptions Vector of frontend subscriptions (frontend, stream_id, target)
     * @param stageTypes Map of stage names to their types (SOURCE, SINK, GENERAL)
     */
    static void export_to_dot(
        const std::string &filename, const std::unordered_map<std::string, StagePtr> &allStages,
        const std::vector<std::pair<std::string, std::string>> &connections,
        const std::vector<std::tuple<std::string, std::string, std::string>> &frontendSubscriptions,
        const std::unordered_map<std::string, StageType> &stageTypes);

  private:
    // Context for DOT export: tracks stage name mappings and discovered connections
    struct DotExportContext
    {
        // Maps short stage names to their full hierarchical names (e.g., "tiling_stage" -> "pipeline::tiling_stage")
        std::unordered_map<std::string, std::string> short_name_to_full_name;

        // Maps stage pointers to their full hierarchical names for reverse lookup
        std::unordered_map<StagePtr, std::string> stage_pointer_to_full_name;

        // Discovered internal connections from traversing subscriber lists (source_name, target_stage_ptr,
        // optional_stream_label)
        std::vector<std::tuple<std::string, StagePtr, std::string>> discovered_internal_connections;

        // Track which stages are FrontendStages to properly identify their stream connections
        std::unordered_set<StagePtr> frontend_stages;
    };

    // Helper functions for DOT export
    static std::string build_full_stage_name(const std::string &parent_prefix, const std::string &stage_name);

    static void get_node_style(const std::unordered_map<std::string, StageType> &stageTypes,
                               const std::string &stage_name, std::string &shape, std::string &color);

    static void collect_stage_subscribers(const StagePtr &stage, const std::string &full_stage_name,
                                          DotExportContext &context);

    static void write_cluster_header(std::ofstream &dotfile, const std::string &indent, const std::string &stage_name);

    static void write_leaf_stage_node(std::ofstream &dotfile, const std::string &indent, const std::string &full_name,
                                      const std::string &stage_name, const std::string &shape,
                                      const std::string &color);

    static void export_stages_recursively(std::ofstream &dotfile,
                                          const std::unordered_map<std::string, StagePtr> &stages,
                                          const std::unordered_map<std::string, StageType> &stageTypes,
                                          const std::string &parent_prefix, DotExportContext &context,
                                          int indent_level = 1);

    static void export_nested_pipeline(std::ofstream &dotfile, const std::string &stage_name,
                                       const std::shared_ptr<Pipeline> &pipeline,
                                       const std::unordered_map<std::string, StageType> &stageTypes,
                                       const std::string &parent_prefix, DotExportContext &context, int indent_level);

    static void export_leaf_stage(std::ofstream &dotfile, const std::string &stage_name, const StagePtr &stage,
                                  const std::unordered_map<std::string, StageType> &stageTypes,
                                  const std::string &parent_prefix, DotExportContext &context, int indent_level);

    static std::string resolve_target_stage_pointer(const StagePtr &target_stage, const DotExportContext &context);

    static std::string resolve_stage_name_through_pipeline(const std::string &stage_name,
                                                           const std::unordered_map<std::string, StagePtr> &allStages,
                                                           bool is_source);

    static std::string lookup_full_stage_name(const std::string &short_or_partial_name,
                                              const DotExportContext &context);

    static void write_edge(std::ofstream &dotfile, const std::string &source, const std::string &target,
                           const std::string &attributes = "");

    static void write_internal_connections(std::ofstream &dotfile, const DotExportContext &context);

    static void write_regular_connections(std::ofstream &dotfile,
                                          const std::vector<std::pair<std::string, std::string>> &connections,
                                          const std::unordered_map<std::string, StagePtr> &allStages,
                                          const DotExportContext &context);

    static void write_frontend_subscriptions(
        std::ofstream &dotfile, const std::vector<std::tuple<std::string, std::string, std::string>> &subscriptions,
        const std::unordered_map<std::string, StagePtr> &allStages, const DotExportContext &context);
};

} // namespace hailo_analytics::pipeline
