#include <dlfcn.h>
#include <stddef.h>
#include <hailo_postprocess_tools/objects/hailo_objects.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/ai/postprocess_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::ai
{

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
PostprocessStage::PostprocessStage(std::string name, std::string so_path, std::string function_name,
                                   std::string config_path, size_t queue_size, bool leaky,
                                   bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
      m_so_path(so_path), m_config_path(config_path), m_function_name(function_name)
{
}

/**
 * @brief Initialize the post-processing stage. by loading the provided so file with dlsym.
 * If the binary has an init function to load parameters then it is called.
 *
 * @return AppStatus Status of the initialization.
 */
AppStatus PostprocessStage::init()
{
    // Load the give SO using dlopen.
    m_loaded_lib = dlopen(m_so_path.c_str(), RTLD_LAZY);
    if (!m_loaded_lib)
    {
        HAILO_ANALYTICS_LOG_ERROR("Could not load lib {}", m_so_path.c_str());
        return AppStatus::CONFIGURATION_ERROR;
    }
    // Reset errors
    dlerror();

    // Expected .so can have an init function, get it if there is any
    auto init_func = (void *(*)(std::string, std::string))dlsym(m_loaded_lib, std::string(INIT_FUNC_NAME).c_str());
    if (init_func == nullptr)
    {
        // Set the library function handler with the requested function name
        m_handler_no_config = (void (*)(HailoROIPtr))dlsym(m_loaded_lib, m_function_name.c_str());
        m_params = nullptr;
    }
    else
    {
        // Call the init function to get the params
        m_params = init_func(m_config_path, m_function_name);
        // Set the library function handler with the requested function name
        m_handler = (void (*)(HailoROIPtr, void *))dlsym(m_loaded_lib, m_function_name.c_str());
    }

    // If there was an error loading one of the symbols, close the dl and break.
    const char *dlsym_error = dlerror();
    if (dlsym_error)
    {
        HAILO_ANALYTICS_LOG_ERROR("Cannot load symbol: ", dlsym_error);
        dlclose(m_loaded_lib);
        return AppStatus::CONFIGURATION_ERROR;
    }

    return AppStatus::SUCCESS;
}

/**
 * @brief Deinitialize the post-processing stage loaded library.
 *
 * @return AppStatus Status of the deinitialization.
 */
AppStatus PostprocessStage::deinit()
{
    // Call the free function if there is any
    if (m_params != nullptr)
    {
        auto delete_func = (void (*)(void *))dlsym(m_loaded_lib, std::string(FREE_FUNC_NAME).c_str());
        if (delete_func != nullptr)
            delete_func(m_params);
        m_params = nullptr;
    }
    // close the loaded library
    if (m_loaded_lib != nullptr)
    {
        dlclose(m_loaded_lib);
        m_loaded_lib = nullptr;
    }
    for (auto &queue : m_queues)
    {
        queue->flush();
    }

    return AppStatus::SUCCESS;
}

/**
 * @brief Process the data in the buffer using the loaded library
 *
 * @param data Buffer containing the data to be processed.
 * @return AppStatus Status of the processing.
 */
AppStatus PostprocessStage::process(BufferPtr data)
{
    // Get the roi from the buffer
    HailoROIPtr hailo_roi = data->get_roi();

    // Call the handler with the roi and the params (if any)
    if (m_params != nullptr)
    {
        m_handler(hailo_roi, m_params);
    }
    else
    {
        m_handler_no_config(hailo_roi);
    }

    // Push the buffer to the next stage
    send_to_subscribers(data);

    return AppStatus::SUCCESS;
}

PostprocessStageBuild::Builder &PostprocessStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}
PostprocessStageBuild::Builder &PostprocessStageBuild::Builder::set_so_path(std::string path)
{
    m_so_path = path;
    return *this;
}
PostprocessStageBuild::Builder &PostprocessStageBuild::Builder::set_function_name_opt(std::string func_name)
{
    m_function_name = func_name;
    return *this;
}
PostprocessStageBuild::Builder &PostprocessStageBuild::Builder::set_config_path_opt(std::string path)
{
    m_config_path = path;
    return *this;
}
PostprocessStageBuild::Builder &PostprocessStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}
PostprocessStageBuild::Builder &PostprocessStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}
PostprocessStageBuild::Builder &PostprocessStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<PostprocessStage> PostprocessStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING(m_so_path.has_value(), "set_so_path");

    return std::make_shared<PostprocessStage>(m_stage_name.value(), m_so_path.value(), m_function_name, m_config_path,
                                              m_queue_size, m_leaky, m_trace);
}

PostprocessStageBuild::Builder PostprocessStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::ai
