#include <stddef.h>
#include <media_library/frontend.hpp>
#include <media_library/media_library.hpp>
#include <tl/expected.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include "hailo_analytics/analytics/overlay.hpp"
#include "hailo_analytics/analytics/vision.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/overlay/overlay_stage.hpp"

namespace hailo_analytics::analytics::overlay
{

// ============================================================================
// overlay_config_t implementation
// ============================================================================

void overlay_config_t::merge_from(const overlay_config_t &other)
{
    if (other.stage_name)
        stage_name = *other.stage_name;
    if (other.skip)
        skip = *other.skip;
    if (other.partial_landmarks)
        partial_landmarks = *other.partial_landmarks;
    if (other.landmark_indices_to_draw)
        landmark_indices_to_draw = *other.landmark_indices_to_draw;
    if (other.queue_size)
        queue_size = *other.queue_size;
    if (other.leaky)
        leaky = *other.leaky;
    if (other.class_ids_to_draw)
        class_ids_to_draw = *other.class_ids_to_draw;
    if (other.color_selector)
        color_selector = *other.color_selector;
    if (other.trace)
        trace = *other.trace;
}

void overlay_config_t::apply_to(overlay_stage::OverlayStageBuild::Builder &b) const
{
    if (stage_name)
        b.set_stage_name(*stage_name);
    if (skip)
        b.set_skip_opt(*skip);
    if (partial_landmarks)
        b.set_partial_landmarks(*partial_landmarks);
    if (landmark_indices_to_draw)
        b.set_landmark_indices_to_draw(*landmark_indices_to_draw);
    if (queue_size)
        b.set_queue_size(*queue_size);
    if (leaky)
        b.set_leaky_opt(*leaky);
    if (class_ids_to_draw)
        b.set_class_ids_to_draw(*class_ids_to_draw);
    if (color_selector)
        b.set_color_selector(*color_selector);
    if (trace)
        b.set_trace_opt(*trace);
}

// ============================================================================
// overlay_vision_output_config_t implementation
// ============================================================================

void overlay_vision_output_config_t::merge_from(const overlay_vision_output_config_t &other)
{
    overlay_config.merge_from(other.overlay_config);
    vision_output_config.merge_from(other.vision_output_config);
}

void overlay_vision_output_config_t::apply_to(overlay_stage::OverlayStageBuild::Builder &b) const
{
    overlay_config.apply_to(b);
}

// ============================================================================
// Pipeline generation functions
// ============================================================================

overlay_config_t base_overlay_config()
{
    overlay_config_t config;

    config.stage_name = "overlay_stage";
    config.skip = false;
    config.partial_landmarks = false;
    config.queue_size = 3;
    config.leaky = false;
    config.trace = true;

    return config;
}

overlay_vision_output_config_t base_overlay_vision_output_config(std::string output_stream_id)
{
    overlay_vision_output_config_t config;

    config.overlay_config = base_overlay_config();

    config.vision_output_config = vision::base_vision_output_config(output_stream_id);

    return config;
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> generate_overlay_pipeline(
    MediaLibraryInterfacePtr media_library, const output_stream_id_t &stream_id, const std::string &pipeline_name,
    std::optional<overlay_vision_output_config_t> user_configs)
{
    overlay_vision_output_config_t cfg = base_overlay_vision_output_config(); // default configs
    if (user_configs.has_value())
    {
        cfg.merge_from(user_configs.value()); // user overrides
    }

    // Create pipeline builder
    hailo_analytics::pipeline::PipelineBuilder pip_builder;

    // Create overlay stage
    overlay_stage::OverlayStageBuild::Builder overlay_builder = overlay_stage::OverlayStageBuild::create();
    cfg.apply_to(overlay_builder);
    std::shared_ptr<overlay_stage::OverlayStage> overlay_stage_ptr = overlay_builder.buildptr();

    // Add overlay stage to pipeline
    pip_builder.add_stage(overlay_stage_ptr);

    // Generate the vision output pipeline (encoder -> UDP)
    std::string vision_output_pipeline_name = pipeline_name + "_vision_output";
    auto vision_result = vision::generate_vision_output_pipeline(media_library, stream_id, vision_output_pipeline_name,
                                                                 cfg.vision_output_config);
    if (!vision_result.has_value())
    {
        return tl::unexpected(vision_result.error());
    }

    hailo_analytics::pipeline::PipelinePtr vision_output_pipeline = vision_result.value();

    // Add vision output pipeline to the main pipeline builder
    pip_builder.add_stage(vision_output_pipeline);

    // Connect overlay stage to vision output pipeline
    pip_builder.connect(*cfg.overlay_config.stage_name, vision_output_pipeline_name);

    // Create the pipeline
    hailo_analytics::pipeline::PipelinePtr pipeline = pip_builder.build(pipeline_name, true);

    // Set the input and output stages
    pipeline->set_in_stage(overlay_stage_ptr);
    pipeline->set_out_stage(vision_output_pipeline);

    return pipeline;
}

} // namespace hailo_analytics::analytics::overlay
