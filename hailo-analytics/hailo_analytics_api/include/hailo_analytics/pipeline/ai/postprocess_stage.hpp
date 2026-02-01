#pragma once

/**
 * @file postprocess_stage.hpp
 * @brief Stage that performs post-processing on video frames.
 **/

// General includes
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <iostream>
#include <dlfcn.h>
#include <vector>

// HailoRT includes
#include "hailo/hailort.hpp"

// Postporcess Tools includes
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// Media library includes
#include "media_library/media_library_types.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::ai
{

inline constexpr std::string_view DEFAULT_FUNC_NAME = "filter";
inline constexpr std::string_view INIT_FUNC_NAME = "init";
inline constexpr std::string_view FREE_FUNC_NAME = "free_resources";

/**
 * @brief Class representing a post-processing stage in the connected stage pipeline.
 *
 * This class is responsible for loading a shared object library, initializing it, and
 * applying post-processing functions to the data.
 */
class PostprocessStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    // Library info
    std::string m_so_path;       ///< Path to the shared object file.
    std::string m_config_path;   ///< Path to the configuration file.
    std::string m_function_name; ///< Name of the function to be executed from the shared object file.

    // Loaded libraries and params
    void *m_loaded_lib; ///< Handle to the loaded shared object library.
    void *m_params;     ///< Parameters for the post-processing function.

    // Function handlers
    void (*m_handler)(HailoROIPtr, void *);   ///< Function pointer to the post-processing function with parameters.
    void (*m_handler_no_config)(HailoROIPtr); ///< Function pointer to the post-processing function without parameters.

  public:
    /**
     * @brief Construct a new Postprocess Stage object.
     *
     * @param name Name of the stage.
     * @param so_path Path to the shared object file.
     * @param function_name Name of the function to be executed from the shared object file.
     * @param config_path Path to the configuration file.
     * @param queue_size Size of the processing queue.
     * @param leaky Whether the queue is leaky.
     * @param print_fps Whether to print frames per second information.
     */
    PostprocessStage(std::string name, std::string so_path, std::string function_name = std::string(DEFAULT_FUNC_NAME),
                     std::string config_path = "", size_t queue_size = 5, bool leaky = false,
                     bool trace_processing_operations = true);

    /**
     * @brief Initialize the post-processing stage. by loading the provided so file with dlsym.
     * If the binary has an init function to load parameters then it is called.
     *
     * @return AppStatus Status of the initialization.
     */
    AppStatus init() override;

    /**
     * @brief Deinitialize the post-processing stage loaded library.
     *
     * @return AppStatus Status of the deinitialization.
     */
    AppStatus deinit() override;

    /**
     * @brief Process the data in the buffer using the loaded library
     *
     * @param data Buffer containing the data to be processed.
     * @return AppStatus Status of the processing.
     */
    AppStatus process(BufferPtr data) override;
};

class PostprocessStageBuild : public PostprocessStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_so_path;
        std::string m_function_name = std::string(DEFAULT_FUNC_NAME);
        std::string m_config_path = "";
        size_t m_queue_size = 5;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_so_path(std::string path);
        Builder &set_function_name_opt(std::string func_name);
        Builder &set_config_path_opt(std::string path);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_trace_opt(bool activate);

        std::shared_ptr<PostprocessStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::ai
