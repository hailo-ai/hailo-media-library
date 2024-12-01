#include "signal_utils.hpp"
#include <signal.h>
#include <mutex>

std::mutex signal_mtx;
bool signal_flag = false;
signal_utils::hailo_exit_signal_t signal_handler = nullptr;

inline void on_signal_callback(int signal)
{
    if (signal_flag)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(signal_mtx);
    if (signal_flag)
    {
        return;
    }
    signal_flag = true;
    signal_handler(signal);
    exit(0);
}

void signal_utils::register_signal_handler(signal_utils::hailo_exit_signal_t signal_handler_cb)
{
    if (signal_handler != nullptr)
    {
        throw std::invalid_argument("signal handler is already set");
    }

    signal_handler = signal_handler_cb;
    signal_flag = false;
    signal(SIGINT, on_signal_callback);
}
