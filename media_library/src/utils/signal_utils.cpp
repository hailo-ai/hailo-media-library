#include "signal_utils.hpp"
#include <signal.h>
#include <mutex>
#include <stdexcept>
#include <cstdlib>

namespace signal_utils
{

SignalHandler *SignalHandler::instance = nullptr;

void SignalHandler::on_signal_callback(int signal)
{
    if (!instance)
        return;
    std::lock_guard<std::mutex> lock(instance->signal_mtx);
    if (instance->signal_flag)
    {
        return;
    }
    instance->signal_flag = true;
    if (instance->signal_handler)
    {
        instance->signal_handler(signal);
    }

    if (instance->exit_on_signal)
        exit(0);
}

void SignalHandler::register_signal_handler(hailo_exit_signal_t signal_handler_cb)
{
    std::lock_guard<std::mutex> lock(signal_mtx);
    if (signal_handler != nullptr)
    {
        throw std::invalid_argument("signal handler is already set");
    }
    signal_handler = signal_handler_cb;
    signal_flag = false;

    if (instance != nullptr)
    {
        throw std::runtime_error("SignalHandler instance already set");
    }
    instance = this;

    signal(SIGINT, on_signal_callback);
}

SignalHandler::~SignalHandler()
{
    std::lock_guard<std::mutex> lock(signal_mtx);
    signal_handler = nullptr;
    signal_flag = false;
    instance = nullptr;
    signal(SIGINT, SIG_DFL); // Reset the signal handler to default
}
} // namespace signal_utils
