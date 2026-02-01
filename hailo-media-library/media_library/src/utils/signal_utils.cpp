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

    // set the atomic true to indicate signal was received, if it was already true, return
    bool expected = false;
    if (!instance->signal_flag.compare_exchange_strong(expected, true))
    {
        return;
    }

    if (instance->signal_handler)
    {
        instance->signal_handler(signal);
    }

    if (instance->exit_on_signal)
        exit(0);
}

void SignalHandler::register_signal_handler(hailo_exit_signal_t signal_handler_cb)
{
    if (signal_flag.load())
    {
        throw std::runtime_error("Cannot register signal handler after signal was already received");
    }

    signal_flag.store(true); // act as mutex
    if (signal_handler != nullptr)
    {
        throw std::invalid_argument("signal handler is already set");
    }
    if (instance != nullptr)
    {
        throw std::runtime_error("SignalHandler instance already set");
    }

    signal_handler = signal_handler_cb;
    instance = this;

    signal_flag.store(false);
    signal(SIGINT, on_signal_callback);
}

SignalHandler::~SignalHandler()
{
    instance = nullptr;
    signal(SIGINT, SIG_DFL); // Reset the signal handler to default
}
} // namespace signal_utils
