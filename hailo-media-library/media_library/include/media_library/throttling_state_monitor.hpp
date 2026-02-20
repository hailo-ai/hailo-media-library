#pragma once

#include <string>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <atomic>
#include <future>
#include <memory>
#include <tl/expected.hpp>

#include "media_library_types.hpp"
#include "throttling_manager.h"

#define DEFAULT_TOTAL_COOLING_WAIT_TIME_IN_MINUTES 20 // Default cooling wait time in minutes

// Default state ready delay time for callback coalescing at startup
static constexpr std::chrono::milliseconds DEFAULT_STATE_READY_DELAY_TIME{200};

// User ID type for throttling monitor subscribers
using throttling_monitor_user_id_t = uint32_t;
static constexpr throttling_monitor_user_id_t INVALID_THROTTLING_MONITOR_USER_ID = 0;

class ThrottlingStateMonitor;

/*!
 * @brief Enum representing the different throttling states.
 *
 * THROTTLING_S<X>_HEATING:
 * - SoC entered to ThrottlingStateId::S<X> from lower Throttling state.
 * - E.g.: S1 -> S2, S2 -> S3
 * THROTTLING_S<X>_COOLING:
 * - SoC entered to ThrottlingStateId::S<X> from higher Throttling state.
 * - E.g.: S2 -> S1, S3 -> S2, S0 -> FULL_PERFORMANCE
 */
enum class throttling_state_t
{
    THERMAL_UNINITIALIZED = 0,
    FULL_PERFORMANCE,
    FULL_PERFORMANCE_COOLING,
    THROTTLING_S0_HEATING,
    THROTTLING_S0_COOLING,
    THROTTLING_S1_HEATING,
    THROTTLING_S1_COOLING,
    THROTTLING_S2_HEATING,
    THROTTLING_S2_COOLING,
    THROTTLING_S3_HEATING,
    THROTTLING_S3_COOLING,
    THROTTLING_S4_HEATING,
    THROTTLING_S4_COOLING
};

enum class thermal_direction
{
    COOLING,
    HEATING
};

class ThrottlingManagerWrapper
{
  protected:
    float m_cooling_wait_time_in_minutes;

  public:
    float get_cooling_wait_time_in_minutes() const;
    virtual ~ThrottlingManagerWrapper() = default;
    ThrottlingManagerWrapper();

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
    std::map<throttling_state_t, std::map<throttling_monitor_user_id_t, std::vector<std::function<void()>>>>
        m_state_callbacks;
    std::mutex m_mutex;
    std::atomic<throttling_state_t> m_state_id;
    bool m_monitoring;
    std::atomic<int> m_start_ref_count{0};                 // Reference counter for start/stop calls
    std::atomic<uint32_t> m_next_user_id{1};               // Next user ID to assign
    std::set<throttling_monitor_user_id_t> m_active_users; // Set of active user IDs
    std::thread m_timer_thread;                            // Managed timer thread
    std::atomic<bool> m_stop_timer_flag;                   // Flag to signal the timer thread to stop
    std::shared_ptr<std::promise<void>> m_timer_promise;   // Promise to signal the timer thread

    // State ready delay mechanism for callback coalescing
    std::atomic<bool>
        m_state_update_delay_done; // Flag indicating that delay timer has elapsed - means callbacks can be invoked
    std::thread m_state_ready_delay_thread;
    std::mutex m_state_ready_delay_mutex;
    std::condition_variable m_state_ready_delay_cv;
    std::atomic<bool> m_stop_state_ready_delay_flag{false};

    media_library_return handle_throttling_state(ThrottlingStateId state_id);
    media_library_return wait_for_cooling();
    thermal_direction get_current_thermal_direction();
    void invoke_callbacks();
    void start_timer(int duration, const std::function<void()> &callback);
    media_library_return determine_initial_state();
    media_library_return handle_cooling_in_progress();
    void stop_timer();
    bool is_cooling();
    void start_state_ready_delay_timer();
    void stop_state_ready_delay_timer();

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

    /**
     * @brief Start monitoring and register a new user.
     *
     * @return tl::expected containing the assigned user_id on success, or error code on failure.
     */
    tl::expected<throttling_monitor_user_id_t, media_library_return> start();

    /**
     * @brief Stop monitoring for a specific user.
     *
     * @param user_id The user ID returned from start().
     * @return media_library_return The result of the stop operation.
     */
    media_library_return stop(throttling_monitor_user_id_t user_id);

    /**
     * @brief Force stop all monitoring, ignoring user IDs and ref count.
     *
     * @return media_library_return The result of the stop operation.
     */
    media_library_return stop_force();

    /**
     * @brief Subscribe to a thermal state change.
     *
     * @param user_id The user ID returned from start().
     * @param state_id The thermal state to subscribe to.
     * @param callback The callback function to be called when the state changes.
     * @return media_library_return The result of the subscription.
     */
    media_library_return subscribe(throttling_monitor_user_id_t user_id, throttling_state_t state_id,
                                   std::function<void()> callback);

    /**
     * @brief  Get the current active thermal state.
     *
     * @return The current active thermal state.
     */
    throttling_state_t get_active_state() const;

    static std::string throttling_state_to_string(throttling_state_t id);

    /*!
     * @brief Convert Throttling state to string
     */
    std::string toString() const;

    /*!
     * @brief Overload the << operator
     *
     * @param os: Output stream
     * @param state: Throttling state
     */
    friend std::ostream &operator<<(std::ostream &os, const ThrottlingStateMonitor &state);
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
