#include <stdint.h>
#include <throttling_manager.h>
#include <throttling_manager_types.h>
#include <time.h>
#include <tl/expected.hpp>
#include <iostream>
#include <map>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "media_library_logger.hpp"
#include "throttling_state_monitor.hpp"
#include "common.hpp"
#include "env_vars.hpp"
#include "media_library_types.hpp"

#define MODULE_NAME LoggerType::ThrottlingMonitor

/*!
 * @brief ThrottlingManagerWrapper class implementation
 */

ThrottlingManagerWrapper::ThrottlingManagerWrapper()
    : m_cooling_wait_time_in_minutes(DEFAULT_TOTAL_COOLING_WAIT_TIME_IN_MINUTES)
{

    auto cooling_wait_time = get_env_variable<float>(MEDIALIB_DEFAULT_TOTAL_COOLING_WAIT_TIME_IN_MINUTES_ENV_VAR);
    if (cooling_wait_time.has_value())
    {
        m_cooling_wait_time_in_minutes = cooling_wait_time.value();
        LOGGER__MODULE__INFO(MODULE_NAME, "set ENV override for m_cooling_wait_time_in_minutes to {}",
                             std::to_string(m_cooling_wait_time_in_minutes));
    }
    else
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "set m_cooling_wait_time_in_minutes to default {}",
                              std::to_string(m_cooling_wait_time_in_minutes));
    }
}

ThrottlingStateId ThrottlingManagerWrapper::get_current_state_id() const
{
    return ThrottlingManager::getInstance().getCurrStateId();
}

ThrottlingStateId ThrottlingManagerWrapper::get_previous_state_id() const
{
    return ThrottlingManager::getInstance().getPrevStateId();
}

uint64_t ThrottlingManagerWrapper::get_state_exit_timestamp(ThrottlingStateId state_id) const
{
    return ThrottlingManager::getInstance().getStateExitTimestamp(state_id);
}

void ThrottlingManagerWrapper::register_enterCb(ThrottlingStateId state_id, ThrottlingStateMonitor &monitor)
{
    ThrottlingManager::getInstance().register_enterCb(
        state_id,
        std::bind(&ThrottlingStateMonitor::on_internal_state_change_callback, &monitor, std::placeholders::_1));
}

float ThrottlingManagerWrapper::get_cooling_wait_time_in_minutes() const
{
    return m_cooling_wait_time_in_minutes;
}

void ThrottlingManagerWrapper::start_watch()
{
    ThrottlingManager::getInstance().startWatch();
}

void ThrottlingManagerWrapper::stop_watch()
{
    ThrottlingManager::getInstance().stopWatch();
}

bool ThrottlingManagerWrapper::is_running() const
{
    return ThrottlingManager::getInstance().isRunning();
}

/*!
 * @brief MockThrottlingManagerWrapper class implementation
 */

MockThrottlingManagerWrapper::MockThrottlingManagerWrapper()
    : curr_state(ThrottlingStateId::FULL_PERFORMANCE), prev_state(ThrottlingStateId::S0),
      state_exit_timestamps{{ThrottlingStateId::S0, 0}, {ThrottlingStateId::FULL_PERFORMANCE, 0}}, m_is_running(false)
{
    curr_state = ThrottlingStateId::FULL_PERFORMANCE;
}

ThrottlingStateId MockThrottlingManagerWrapper::get_current_state_id() const
{
    return curr_state;
}

ThrottlingStateId MockThrottlingManagerWrapper::get_previous_state_id() const
{
    return prev_state;
}

void MockThrottlingManagerWrapper::set_cooling_wait_time_in_minutes(float wait_time)
{
    m_cooling_wait_time_in_minutes = wait_time;
}

uint64_t MockThrottlingManagerWrapper::get_state_exit_timestamp(ThrottlingStateId state_id) const
{
    return state_exit_timestamps.at(state_id);
}

void MockThrottlingManagerWrapper::start_watch()
{
    m_is_running = true;
}

void MockThrottlingManagerWrapper::stop_watch()
{
    m_is_running = false;
}

bool MockThrottlingManagerWrapper::is_running() const
{
    return m_is_running;
}

void MockThrottlingManagerWrapper::register_enterCb(ThrottlingStateId state_id, ThrottlingStateMonitor &monitor)
{
    callbacks[state_id] = [state_id, &monitor]() { monitor.on_state_change_callback(state_id); };
}

void MockThrottlingManagerWrapper::simulateStateChange(ThrottlingStateId new_state)
{
    prev_state = curr_state;
    curr_state = new_state;

    // set enter exit timestamp
    state_exit_timestamps[prev_state] = ThrottlingStateMonitor::get_monotonic_time_in_ms();

    if (callbacks.find(new_state) != callbacks.end())
    {
        callbacks[new_state]();
    }
    else
    {
        std::cout << "No callback registered for state: " << static_cast<int>(new_state) << std::endl;
    }
}

std::string ThrottlingStateMonitor::throttling_state_to_string(throttling_state_t id)
{
    std::map<throttling_state_t, std::string> stateMap = {
        {throttling_state_t::THERMAL_UNINITIALIZED, "THERMAL_UNINITIALIZED"},
        {throttling_state_t::FULL_PERFORMANCE, "FULL_PERFORMANCE"},
        {throttling_state_t::FULL_PERFORMANCE_COOLING, "FULL_PERFORMANCE_COOLING"},
        {throttling_state_t::THROTTLING_S0_HEATING, "THROTTLING_S0_HEATING"},
        {throttling_state_t::THROTTLING_S0_COOLING, "THROTTLING_S0_COOLING"},
        {throttling_state_t::THROTTLING_S1_HEATING, "THROTTLING_S1_HEATING"},
        {throttling_state_t::THROTTLING_S1_COOLING, "THROTTLING_S1_COOLING"},
        {throttling_state_t::THROTTLING_S2_HEATING, "THROTTLING_S2_HEATING"},
        {throttling_state_t::THROTTLING_S2_COOLING, "THROTTLING_S2_COOLING"},
        {throttling_state_t::THROTTLING_S3_HEATING, "THROTTLING_S3_HEATING"},
        {throttling_state_t::THROTTLING_S3_COOLING, "THROTTLING_S3_COOLING"},
        {throttling_state_t::THROTTLING_S4_HEATING, "THROTTLING_S4_HEATING"},
        {throttling_state_t::THROTTLING_S4_COOLING, "THROTTLING_S4_COOLING"},
    };

    auto it = stateMap.find(id);
    return (it != stateMap.end()) ? it->second : "UNKNOWN_STATE";
}

std::ostream &operator<<(std::ostream &os, const ThrottlingStateMonitor &state)
{
    return os << ThrottlingStateMonitor::throttling_state_to_string(state.get_active_state());
}

std::string ThrottlingStateMonitor::toString() const
{
    return ThrottlingStateMonitor::throttling_state_to_string(m_state_id);
}

ThrottlingStateMonitor::ThrottlingStateMonitor(std::shared_ptr<ThrottlingManagerWrapper> manager_wrapper)
    : m_manager_wrapper(manager_wrapper ? manager_wrapper : std::make_shared<ThrottlingManagerWrapper>()),
      m_state_id(throttling_state_t::THERMAL_UNINITIALIZED), m_monitoring(false), m_stop_timer_flag(false),
      m_state_update_delay_done(false)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "ThrottlingStateMonitor created");

    std::vector<ThrottlingStateId> callback_states = {ThrottlingStateId::FULL_PERFORMANCE,
                                                      ThrottlingStateId::S0,
                                                      ThrottlingStateId::S1,
                                                      ThrottlingStateId::S2,
                                                      ThrottlingStateId::S3,
                                                      ThrottlingStateId::S4};
    for (auto state : callback_states)
    {
        m_manager_wrapper->register_enterCb(state, *this);
    }
}

ThrottlingStateMonitor::~ThrottlingStateMonitor()
{
    stop_force(); // Force stop regardless of user IDs and reference count
    m_state_callbacks.clear();
    LOGGER__MODULE__INFO(MODULE_NAME, "ThrottlingStateMonitor destroyed");
}

std::shared_ptr<ThrottlingStateMonitor> ThrottlingStateMonitor::create(
    std::shared_ptr<ThrottlingManagerWrapper> manager_wrapper)
{
    static std::shared_ptr<ThrottlingStateMonitor> instance = nullptr;
    if (!instance)
    {
        instance = std::make_shared<ThrottlingStateMonitor>(manager_wrapper);
        return instance;
    }

    if (manager_wrapper)
    {
        instance->m_manager_wrapper = manager_wrapper;
        std::vector<ThrottlingStateId> callback_states = {ThrottlingStateId::FULL_PERFORMANCE,
                                                          ThrottlingStateId::S0,
                                                          ThrottlingStateId::S1,
                                                          ThrottlingStateId::S2,
                                                          ThrottlingStateId::S3,
                                                          ThrottlingStateId::S4};
        for (auto state : callback_states)
        {
            instance->m_manager_wrapper->register_enterCb(state, *instance);
        }
    }
    return instance;
}

void ThrottlingStateMonitor::invoke_callbacks()
{
    if (m_state_update_delay_done)
    {
        // Iterate over all users and their callbacks for the current state
        auto state_it = m_state_callbacks.find(m_state_id);
        if (state_it != m_state_callbacks.end())
        {
            for (auto &user_entry : state_it->second)
            {
                for (auto &callback : user_entry.second)
                {
                    callback();
                }
            }
        }
    }
    else
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "State update not ready - skipping callback invocation");
    }
}

thermal_direction ThrottlingStateMonitor::get_current_thermal_direction()
{
    if (m_manager_wrapper->get_current_state_id() < m_manager_wrapper->get_previous_state_id())
    {
        return thermal_direction::COOLING;
    }

    return thermal_direction::HEATING;
}

uint64_t ThrottlingStateMonitor::get_monotonic_time_in_ms()
{
    // Get monotinic time using clock_gettime(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0)
    {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

media_library_return ThrottlingStateMonitor::wait_for_cooling()
{
    uint64_t state_exit_timestamp = m_manager_wrapper->get_state_exit_timestamp(ThrottlingStateId::S0);
    // convert cooling wait time to mili second from minutes
    uint64_t max_cooling_wait_time = (m_manager_wrapper->get_cooling_wait_time_in_minutes() * 60) * 1000;
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Cooling wait time in minutes: {} - max cooling wait time in mili seconds: {}",
                          std::to_string(m_manager_wrapper->get_cooling_wait_time_in_minutes()),
                          std::to_string(max_cooling_wait_time));
    uint64_t time_now = get_monotonic_time_in_ms();
    if (time_now == 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get monotonic time");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    uint64_t time_passed_mili = time_now - state_exit_timestamp;
    LOGGER__MODULE__DEBUG(
        MODULE_NAME, "enter timestamp probed (mili second): {} - cooling wait time: {}, time passed: {}, time now: {}",
        std::to_string(state_exit_timestamp), std::to_string(max_cooling_wait_time), std::to_string(time_passed_mili),
        std::to_string(time_now));

    // Adding 300 ms to the time passed to avoid rounding errors
    if (time_passed_mili + 300 >= max_cooling_wait_time || state_exit_timestamp == 0)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Cooling time already expired - setting state to FULL_PERFORMANCE");
        m_state_id = throttling_state_t::FULL_PERFORMANCE;
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    uint64_t coolling_time_required = max_cooling_wait_time - time_passed_mili;
    uint64_t cooling_time_in_seconds = coolling_time_required / 1000;
    uint64_t cooling_time_in_minutes = cooling_time_in_seconds / 60;

    if (m_state_id == throttling_state_t::FULL_PERFORMANCE_COOLING && is_cooling())
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME,
                              "Cooling already in progress - returning - wait time left: {} seconds ({} minutes)",
                              std::to_string(cooling_time_in_seconds), std::to_string(cooling_time_in_minutes));
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    m_state_id = throttling_state_t::FULL_PERFORMANCE_COOLING;

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Starting cooling timer for {} seconds ({} mili seconds / {} minutes)",
                          std::to_string(cooling_time_in_seconds), std::to_string(coolling_time_required),
                          std::to_string(cooling_time_in_minutes));

    start_timer((coolling_time_required), std::function<void()>([this]() {
                    ThrottlingStateMonitor &monitor = *this;
                    if (monitor.m_state_id == throttling_state_t::FULL_PERFORMANCE_COOLING &&
                        m_manager_wrapper->get_current_state_id() == ThrottlingStateId::FULL_PERFORMANCE)
                    {
                        LOGGER__MODULE__DEBUG(MODULE_NAME, "Cooling timer expired - setting state to FULL_PERFORMANCE");
                        monitor.handle_throttling_state(ThrottlingStateId::FULL_PERFORMANCE);
                        monitor.invoke_callbacks();
                    }
                    else
                    {
                        LOGGER__MODULE__DEBUG(
                            MODULE_NAME, "Cooling timer disabled due to non peformance state change - continuing...");
                    }
                }));

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

bool ThrottlingStateMonitor::is_cooling()
{
    return m_timer_thread.joinable();
}

void ThrottlingStateMonitor::start_timer(int duration, const std::function<void()> &callback)
{
    stop_timer(); // Stop any existing timer thread before starting a new one

    m_stop_timer_flag = false; // Reset the stop flag

    // Use a promise to signal the timer thread
    m_timer_promise = std::make_shared<std::promise<void>>();
    auto future = m_timer_promise->get_future();

    m_timer_thread = std::thread([this, duration, callback, future = std::move(future)]() mutable {
        // Wait for the duration or until the promise is fulfilled
        if (future.wait_for(std::chrono::milliseconds(duration)) == std::future_status::timeout)
        {
            // Timer expired, invoke the callback
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Cooling timer expired - invoking callback");
            callback();
        }
        else
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Cooling timer stopped before expiration");
        }
    });
}

void ThrottlingStateMonitor::stop_timer()
{
    if (is_cooling())
    {
        m_stop_timer_flag = true; // Signal the thread to stop
        if (m_timer_promise)
        {
            m_timer_promise->set_value(); // Fulfill the promise to stop the timer
        }
        m_timer_thread.join(); // Wait for the thread to finish
    }
}

/**
 * @brief Starts a delay timer before invoking state change callbacks.
 *
 * The thermal engine raises a burst of historical events at startup as it replays
 * its internal state history. To avoid triggering multiple unnecessary callbacks
 * during this initialization phase, we wait 200ms (DEFAULT_STATE_READY_DELAY_TIME_MS)
 * at startup before marking the state as ready and invoking callbacks.
 * This ensures we only react to the final settled state rather than intermediate events.
 */
void ThrottlingStateMonitor::start_state_ready_delay_timer()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Starting state ready delay timer for invoking callbacks");

    m_stop_state_ready_delay_flag = false;

    m_state_ready_delay_thread = std::thread([this]() {
        std::unique_lock<std::mutex> lock(m_state_ready_delay_mutex);
        LOGGER__MODULE__DEBUG(MODULE_NAME, "State ready delay timer started for {} ms",
                              std::to_string(DEFAULT_STATE_READY_DELAY_TIME.count()));

        bool cancelled = m_state_ready_delay_cv.wait_for(lock, DEFAULT_STATE_READY_DELAY_TIME,
                                                         [this]() { return m_stop_state_ready_delay_flag.load(); });

        if (!cancelled)
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME,
                                  "State ready delay timer expired - invoking callbacks if state update is pending");
            // State ready delay timer expired, invoke callbacks with the current state
            m_state_update_delay_done = true;
            LOGGER__MODULE__DEBUG(MODULE_NAME, "State ready delay timer expired - invoking callbacks for state: {}",
                                  throttling_state_to_string(m_state_id));
            invoke_callbacks();
        }
        else
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "State ready delay timer cancelled");
        }
        LOGGER__MODULE__DEBUG(MODULE_NAME, "State ready delay timer thread working");
    });
}

void ThrottlingStateMonitor::stop_state_ready_delay_timer()
{
    {
        std::lock_guard<std::mutex> lock(m_state_ready_delay_mutex);
        m_stop_state_ready_delay_flag = true;
    }
    m_state_ready_delay_cv.notify_one();

    if (m_state_ready_delay_thread.joinable())
    {
        m_state_ready_delay_thread.join(); // Wait for the thread to finish
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "State ready delay timer stopped");
}

media_library_return ThrottlingStateMonitor::handle_cooling_in_progress()
{
    if (is_cooling() && m_state_id != throttling_state_t::FULL_PERFORMANCE_COOLING)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Cooling in progress - but state changed - disabling cooling");
        stop_timer();
    }
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return ThrottlingStateMonitor::handle_throttling_state(ThrottlingStateId state_id)
{
    media_library_return ret = media_library_return::MEDIA_LIBRARY_SUCCESS;
    switch (state_id)
    {
    case ThrottlingStateId::FULL_PERFORMANCE: {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Handling throttling state FULL_PERFORMANCE");
        ret = wait_for_cooling();
        break;
    }
    case ThrottlingStateId::S0: {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Handling throttling state S0");
        if (get_current_thermal_direction() == thermal_direction::HEATING)
            m_state_id = throttling_state_t::THROTTLING_S0_HEATING;
        else
            m_state_id = throttling_state_t::THROTTLING_S0_COOLING;
        break;
    }
    case ThrottlingStateId::S1: {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Handling throttling state S1");
        if (get_current_thermal_direction() == thermal_direction::HEATING)
            m_state_id = throttling_state_t::THROTTLING_S1_HEATING;
        else
            m_state_id = throttling_state_t::THROTTLING_S1_COOLING;
        break;
    }
    case ThrottlingStateId::S2: {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Handling throttling state S2");
        if (get_current_thermal_direction() == thermal_direction::HEATING)
            m_state_id = throttling_state_t::THROTTLING_S2_HEATING;
        else
            m_state_id = throttling_state_t::THROTTLING_S2_COOLING;
        break;
    }
    case ThrottlingStateId::S3: {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Handling throttling state S3");
        if (get_current_thermal_direction() == thermal_direction::HEATING)
            m_state_id = throttling_state_t::THROTTLING_S3_HEATING;
        else
            m_state_id = throttling_state_t::THROTTLING_S3_COOLING;
        break;
    }
    case ThrottlingStateId::S4: {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Handling throttling state S4");
        if (get_current_thermal_direction() == thermal_direction::HEATING)
            m_state_id = throttling_state_t::THROTTLING_S4_HEATING;
        else
            m_state_id = throttling_state_t::THROTTLING_S4_COOLING;
        break;
    }
    default:
        break;
    }

    return ret;
}

media_library_return ThrottlingStateMonitor::determine_initial_state()
{
    // Get the initial state
    m_state_id = throttling_state_t::THERMAL_UNINITIALIZED;
    // Get the current state
    if (handle_throttling_state(m_manager_wrapper->get_current_state_id()) !=
        media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to handle throttling state");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    handle_cooling_in_progress();

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return ThrottlingStateMonitor::stop(throttling_monitor_user_id_t user_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Validate user ID
    if (m_active_users.find(user_id) == m_active_users.end())
    {
        LOGGER__MODULE__WARNING(MODULE_NAME, "stop() called with invalid or already-stopped user_id: {}", user_id);
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    // Remove this user's callbacks from all states
    for (auto &state_entry : m_state_callbacks)
    {
        state_entry.second.erase(user_id);
    }

    // Remove user from active set
    m_active_users.erase(user_id);

    // Decrement ref count
    int ref_count = m_start_ref_count.fetch_sub(1);

    LOGGER__MODULE__DEBUG(MODULE_NAME, "User {} stopped - ref_count: {} -> {}", user_id, ref_count, ref_count - 1);

    if (ref_count <= 0)
    {
        // Should not happen if user_id was valid, but clamp for safety
        m_start_ref_count = 0;
        LOGGER__MODULE__WARNING(MODULE_NAME, "stop() ref_count was already <= 0 (was {})", ref_count);
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    if (!m_active_users.empty())
    {
        // Other users still active
        LOGGER__MODULE__DEBUG(MODULE_NAME, "stop() completed but {} other user(s) still active", m_active_users.size());
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    // No active users remaining - perform actual teardown
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Last user stopped - shutting down throttling state monitor");

    stop_timer();                   // Stop the timer thread
    stop_state_ready_delay_timer(); // Stop the state ready delay thread
    m_manager_wrapper->stop_watch();
    m_monitoring = false;
    m_state_update_delay_done = false;
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return ThrottlingStateMonitor::stop_force()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Force stopping throttling state monitor - clearing all users");

    // Clear all users and callbacks
    m_active_users.clear();
    m_state_callbacks.clear();
    m_start_ref_count = 0;

    // Perform actual teardown
    stop_timer();                   // Stop the timer thread
    stop_state_ready_delay_timer(); // Stop the state ready delay thread
    m_manager_wrapper->stop_watch();
    m_monitoring = false;
    m_state_update_delay_done = false;

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

void ThrottlingStateMonitor::on_internal_state_change_callback(ThrottlingManager &manager)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "throttling state changed: from[{}] -> to[{}]",
                          manager.prevStateToString().c_str(), manager.currStateToString().c_str());

    if (!manager.isRunning())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Throttling manager is not running");
        return;
    }

    ThrottlingStateId state_id = manager.getCurrStateId();
    on_state_change_callback(state_id);
}

void ThrottlingStateMonitor::on_state_change_callback(ThrottlingStateId state_id)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Throttling state changed");
    handle_throttling_state(state_id);
    handle_cooling_in_progress();
    invoke_callbacks();
}

tl::expected<throttling_monitor_user_id_t, media_library_return> ThrottlingStateMonitor::start()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Assign a new user ID
    throttling_monitor_user_id_t user_id = m_next_user_id.fetch_add(1);

    // Increment ref count
    int ref_count = m_start_ref_count.fetch_add(1);

    // Add user to active set
    m_active_users.insert(user_id);

    if (m_monitoring)
    {
        // Already running - new user joined
        LOGGER__MODULE__DEBUG(MODULE_NAME,
                              "Throttling state monitor already running - user {} joined (ref_count: {} -> {})",
                              user_id, ref_count, ref_count + 1);
        return user_id;
    }

    // First user starting (ref_count was 0, now 1)
    LOGGER__MODULE__DEBUG(MODULE_NAME, "First user {} starting throttling state monitor (ref_count: 0 -> 1)", user_id);

    m_monitoring = true;
    m_state_update_delay_done = false;

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Initializing throttling state monitor");

    m_manager_wrapper->start_watch();

    if (determine_initial_state() != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to determine initial state");
        m_start_ref_count.fetch_sub(1); // Rollback ref count on error
        m_active_users.erase(user_id);  // Remove user from active set
        m_monitoring = false;
        return tl::unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }

    stop_state_ready_delay_timer();
    start_state_ready_delay_timer();

    return user_id;
}

throttling_state_t ThrottlingStateMonitor::get_active_state() const
{
    return m_state_id;
}

media_library_return ThrottlingStateMonitor::subscribe(throttling_monitor_user_id_t user_id,
                                                       throttling_state_t state_id, std::function<void()> callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Validate user ID
    if (m_active_users.find(user_id) == m_active_users.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "subscribe() called with invalid user_id: {} - must call start() first",
                              user_id);
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    m_state_callbacks[state_id][user_id].emplace_back(callback);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "User {} subscribed to state {}", user_id, throttling_state_to_string(state_id));
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}
