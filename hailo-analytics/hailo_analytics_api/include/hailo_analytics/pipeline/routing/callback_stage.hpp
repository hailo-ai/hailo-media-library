#pragma once

/**
 * @file callback_stage.hpp
 * @brief Stage that executes a user-defined callback function on the input data.
 **/

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::routing
{

class CallbackStage : public hailo_analytics::pipeline::ThreadedStage
{
  protected:
    std::function<void(BufferPtr)> m_callback;

  public:
    CallbackStage(std::string name, size_t queue_size, bool leaky = false,
                  std::function<void(BufferPtr)> callback = NULL, bool trace_processing_operations = true);
    AppStatus process(BufferPtr data) override;
    void set_callback(std::function<void(BufferPtr)> callback);
};

class CallbackStageBuild : public CallbackStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = 1;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_trace_opt(bool activate);
        std::shared_ptr<CallbackStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::routing
