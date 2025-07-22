#pragma once

#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <atomic>
#include <future>
#include <memory> // For shared_ptr

#include "media_library_types.hpp"
#include "throttling_manager.h"

#define DEFAULT_TOTAL_COOLING_WAIT_TIME_IN_MINUTES 20 // Default cooling wait time in minutes

class ThrottlingStateMonitor;

enum class throttling_state_t
{
    THERMAL_UNINITIALIZED = 0,
    FULL_PERFORMANCE = 1,
    FULL_PERFORMANCE_COOLING = 2,
    THROTTLING_S0_HEATING = 3,
    THROTTLING_S0_COOLING = 4,
    THROTTLING_S1_HEATING = 5,
    THROTTLING_S1_COOLING = 6,
    THROTTLING_S2_HEATING = 7,
    THROTTLING_S2_COOLING = 8,
    THROTTLING_S3_HEATING = 5,
    THROTTLING_S3_COOLING = 6,
    THROTTLING_S4_HEATING = 7,
    THROTTLING_S4_COOLING = 8
};

enum class thermal_direction
{
    COOLING,
    HEATING
};

class ThrottlingManagerWrapper
{
  protected:
    float m_cooling_wait_time_in_minutes = DEFAULT_TOTAL_COOLING_WAIT_TIME_IN_MINUTES;

  public:
    float get_cooling_wait_time_in_minutes() const;
    virtual ~ThrottlingManagerWrapper() = default;

    virtual ThrottlingStateId get_current_state_id() const;
    virtual ThrottlingStateId get_previous_state_id() const;
    virtual uint64_t get_state_exit_timestamp(ThrottlingStateId state_id) const;
    virtual void register_enterCb(ThrottlingStateId state_id, ThrottlingStateMonitor &monitor);
    virtual void start_watch();
    virtual void stop_watch();
    virtual bool is_running() const;
};

class ThrottlingStateMonitor
{
  private:
    std::shared_ptr<ThrottlingManagerWrapper> m_manager_wrapper; // Dependency injection for ThrottlingManagerWrapper
    std::map<throttling_state_t, std::vector<std::function<void()>>> m_state_callbacks;
    std::mutex m_mutex;
    std::atomic<throttling_state_t> m_state_id;
    bool m_monitoring;
    std::thread m_timer_thread;                          // Managed timer thread
    std::atomic<bool> m_stop_timer_flag;                 // Flag to signal the timer thread to stop
    std::shared_ptr<std::promise<void>> m_timer_promise; // Promise to signal the timer thread

    media_library_return handle_throttling_state(ThrottlingStateId state_id);
    media_library_return wait_for_cooling();
    thermal_direction get_current_thermal_direction();
    void invoke_callbacks(throttling_state_t state_id);
    void start_timer(int duration, const std::function<void()> &callback);
    media_library_return determine_initial_state();
    media_library_return handle_cooling_in_progress();
    void stop_timer();
    bool is_cooling();

  protected:
    void on_internal_state_change_callback(ThrottlingManager &manager);
    void on_state_change_callback(ThrottlingStateId state_id);
    friend class ThrottlingManagerWrapper;
    friend class MockThrottlingManagerWrapper;

  public:
    ThrottlingStateMonitor(std::shared_ptr<ThrottlingManagerWrapper> manager_wrapper);
    ~ThrottlingStateMonitor();
    static uint64_t get_monotonic_time_in_ms();
    static std::shared_ptr<ThrottlingStateMonitor> create(
        std::shared_ptr<ThrottlingManagerWrapper> manager_wrapper = nullptr);
    ThrottlingStateMonitor &operator=(const ThrottlingStateMonitor &) = delete;
    media_library_return start();
    media_library_return stop();

    /**
     * @brief Subscribe to a thermal state change.
     *
     * @param state_id The thermal state to subscribe to.
     * @param callback The callback function to be called when the state changes.
     * @return media_library_return The result of the subscription.
     */
    media_library_return subscribe(throttling_state_t state_id, std::function<void()> callback);

    /**
     * @brief  Get the current active thermal state.
     *
     * @return The current active thermal state.
     */
    throttling_state_t get_active_state();
};

class MockThrottlingManagerWrapper : public ThrottlingManagerWrapper
{
  private:
    ThrottlingStateId curr_state;
    ThrottlingStateId prev_state;
    std::map<ThrottlingStateId, uint64_t> state_exit_timestamps;
    std::map<ThrottlingStateId, std::function<void()>> callbacks;
    bool m_is_running;

  public:
    MockThrottlingManagerWrapper();
    ThrottlingStateId get_current_state_id() const override;
    ThrottlingStateId get_previous_state_id() const override;
    void set_cooling_wait_time_in_minutes(float wait_time);
    uint64_t get_state_exit_timestamp(ThrottlingStateId state_id) const override;
    void start_watch() override;
    void stop_watch() override;
    bool is_running() const override;
    void register_enterCb(ThrottlingStateId state_id, ThrottlingStateMonitor &monitor) override;
    void simulateStateChange(ThrottlingStateId new_state);
};
