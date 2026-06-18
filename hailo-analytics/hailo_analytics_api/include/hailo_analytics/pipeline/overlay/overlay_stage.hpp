#pragma once

/**
 * @file overlay_stage.hpp
 * @brief Stage that draws visual overlays on video frames (bounding boxes, landmarks, labels, etc.).
 **/

// General includes
#include <unordered_set>
#include <vector>
#include <string>
#include <functional>
#include <cstddef>
#include <atomic>

// Postprocess Tools includes
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"

#include <opencv2/core/types.hpp>

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::overlay
{

/**
 * @struct HailoOverlay
 * @brief Structure containing configuration parameters for overlay.
 */
struct HailoOverlay
{
    int line_thickness;          /**< Line thickness for overlay. */
    int font_thickness;          /**< Font thickness for overlay. */
    float landmark_point_radius; /**< Radius for landmark points. */
    bool face_blur;              /**< Enable or disable face blur. */
    bool show_confidence;        /**< Enable or disable confidence display. */
    bool local_gallery;          /**< Enable or disable local gallery usage. */
    uint mask_overlay_n_threads; /**< Number of threads for mask overlay. */
};

/**
 * @class OverlayStage
 * @brief Class responsible for handling overlay stage that will do the drawing.
 */
class OverlayStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    HailoOverlay m_hailooverlay_info;                      /**< Overlay configuration parameters. */
    std::atomic_bool m_skip;                               /**< Flag to skip drawing. */
    bool m_partial_landmarks;                              /**< Flag to enable partial landmarks. */
    std::unordered_set<size_t> m_landmark_indices_to_draw; /**< indices to draw */
    std::unordered_set<int> m_class_ids_to_draw;           /**< Label for the overlay stage. */
    std::function<cv::Scalar(const HailoDetectionPtr &)>
        m_color_selector; /**< Function to select color based on detection. */

  public:
    /**
     * @brief Constructor for OverlayStage.
     * @param name The name of the stage.
     * @param queue_size Size of the queue for this stage.
     * @param leaky Indicates if the queue is leaky.
     * @param print_fps Flag to enable or disable printing FPS information.
     */
    OverlayStage(std::string name, bool skip = false, bool partial_landmarks = false,
                 std::unordered_set<size_t> landmark_indices_to_draw = {}, size_t queue_size = 5, bool leaky = false,
                 std::unordered_set<int> class_ids_to_draw = {},
                 std::function<cv::Scalar(const HailoDetectionPtr &)> color_selector = nullptr,
                 bool trace_processing_operations = true);

    /**
     * @brief Initialize the overlay stage.
     * @return Status of the initialization.
     */
    AppStatus init() override;

    /**
     * @brief Deinitialize the overlay stage.
     * @return Status of the deinitialization.
     */
    AppStatus deinit() override;

    /**
     * @brief Process the given data buffer and apply overlay.
     * @param data The data buffer to process.
     * @return Status of the processing.
     */
    AppStatus process(BufferPtr data) override;

    /**
     * @brief Set the overlay skip flag.
     * @param skip Flag to set the skip state.
     */
    void set_skip(bool skip);

    /**
     * @brief Get the overlay skip flag.
     * @return Current skip state.
     */
    bool get_skip();
};

/**
 * @brief Builder-based overlay stage for simplified construction.
 *
 * Provides a builder pattern interface for creating overlay stages with
 * configurable parameters for drawing customization.
 */
class OverlayStageBuild : public OverlayStage
{
  public:
    /**
     * @brief Builder class for OverlayStage construction.
     */
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        bool m_skip = false;
        bool m_partial_landmarks = false;
        std::unordered_set<size_t> m_landmark_indices_to_draw = {};
        size_t m_queue_size = 5;
        bool m_leaky = false;
        std::unordered_set<int> m_class_ids_to_draw = {};
        std::function<cv::Scalar(const HailoDetectionPtr &)> m_color_selector = nullptr;
        bool m_trace = true;

      public:
        /**
         * @brief Set the stage name.
         * @param name Name for the overlay stage.
         * @return Reference to this builder for chaining.
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Set whether to skip overlay drawing (optional).
         * @param skip True to skip drawing.
         * @return Reference to this builder for chaining.
         */
        Builder &set_skip_opt(bool skip);

        /**
         * @brief Set whether to draw partial landmarks.
         * @param partial_landmarks True to enable partial landmark drawing.
         * @return Reference to this builder for chaining.
         */
        Builder &set_partial_landmarks(bool partial_landmarks);

        /**
         * @brief Set which landmark indices to draw.
         * @param indices Set of landmark indices to render.
         * @return Reference to this builder for chaining.
         */
        Builder &set_landmark_indices_to_draw(std::unordered_set<size_t> indices);

        /**
         * @brief Set the queue size.
         * @param size Queue size.
         * @return Reference to this builder for chaining.
         */
        Builder &set_queue_size(size_t size);

        /**
         * @brief Set whether the queue is leaky (optional).
         * @param activate True to enable leaky mode.
         * @return Reference to this builder for chaining.
         */
        Builder &set_leaky_opt(bool activate);

        /**
         * @brief Set which class IDs to draw.
         * @param class_ids_to_draw Set of class IDs to render.
         * @return Reference to this builder for chaining.
         */
        Builder &set_class_ids_to_draw(std::unordered_set<int> class_ids_to_draw);

        /**
         * @brief Set a custom color selector function.
         * @param color_selector Function that returns a color based on detection.
         * @return Reference to this builder for chaining.
         */
        Builder &set_color_selector(std::function<cv::Scalar(const HailoDetectionPtr &)> color_selector);

        /**
         * @brief Set whether to enable tracing (optional).
         * @param activate True to enable tracing.
         * @return Reference to this builder for chaining.
         */
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Build and return the OverlayStage.
         * @return Shared pointer to the constructed OverlayStage.
         */
        std::shared_ptr<OverlayStage> buildptr() const;
    };

    /**
     * @brief Create a new Builder for OverlayStage construction.
     * @return Builder instance.
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::overlay
