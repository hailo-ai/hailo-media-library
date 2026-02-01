#pragma once

/**
 * @file freeze_stage.hpp
 * @brief Stage that can freeze the data flow, outputting the last received buffer.
 **/

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::routing
{

/**
 * @brief Stage that can freeze the data flow.
 *
 * When freeze is enabled, the stage will output the last received buffer instead
 * of forwarding new incoming buffers.
 */
class FreezeStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    BufferPtr m_saved_buffer;
    std::atomic<bool> m_freeze;

  public:
    /**
     * @brief Construct a new FreezeStage.
     *
     * @param name Stage name.
     * @param queue_size Input queue size.
     * @param leaky Whether the queue is leaky.
     * @param print_fps Whether to print FPS statistics.
     */
    FreezeStage(std::string name, size_t queue_size, bool leaky = false, bool print_fps = false);

    /**
     * @brief Process a single buffer.
     *
     * If freeze is enabled, this will output the last saved buffer. Otherwise,
     * the incoming buffer is saved and forwarded.
     *
     * @param data Input buffer.
     * @return AppStatus Status code.
     */
    AppStatus process(BufferPtr data) override;

    /**
     * @brief Check whether the stage is currently frozen.
     * @return True if frozen.
     */
    bool is_freeze();

    /**
     * @brief Enable/disable freeze mode.
     * @param freeze True to freeze.
     */
    void set_freeze(bool freeze);

    /**
     * @brief Set the saved buffer that will be output while frozen.
     * @param buffer Buffer to save.
     */
    void set_saved_buffer(BufferPtr buffer);

    /**
     * @brief Get the currently saved buffer.
     * @return Saved buffer.
     */
    BufferPtr get_saved_buffer();

    /**
     * @brief Clear the saved buffer.
     */
    void clear_saved_buffer();
};

/**
 * @brief Builder wrapper for FreezeStage.
 */
class FreezeStageBuild : public FreezeStage
{
  public:
    /**
     * @brief FreezeStage builder.
     */
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = 0;
        bool m_leaky = false;
        bool m_print_fps = false;

      public:
        /**
         * @brief Set the stage name.
         * @param name Stage name.
         * @return Builder reference.
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Set the stage queue size.
         * @param size Queue size.
         * @return Builder reference.
         */
        Builder &set_queue_size(size_t size);

        /**
         * @brief Enable/disable leaky queue behavior.
         * @param activate True to enable.
         * @return Builder reference.
         */
        Builder &set_leaky_opt(bool activate);

        /**
         * @brief Enable/disable FPS printing.
         * @param activate True to enable.
         * @return Builder reference.
         */
        Builder &set_printfps_opt(bool activate);

        /**
         * @brief Build a FreezeStage instance.
         * @return Shared pointer to the stage.
         */
        std::shared_ptr<FreezeStage> buildptr() const;
    };

    /**
     * @brief Create a builder with default values.
     * @return Builder instance.
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::routing
