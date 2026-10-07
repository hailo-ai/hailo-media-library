#include "signal_utils.hpp"

#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <stdexcept>
#include <cstdlib>
#include <thread>

namespace signal_utils
{

SignalHandler *SignalHandler::instance = nullptr;

namespace
{
std::atomic<bool> g_threaded{false};
sem_t g_signal_semaphore;
sem_t g_dispatcher_exited;
std::atomic<int> g_received_signal{0};
std::atomic<bool> g_dispatcher_stopping{false};
std::atomic<std::thread::id> g_dispatcher_thread_id;

void wait_on(sem_t &semaphore)
{
    while (sem_wait(&semaphore) != 0 && errno == EINTR)
    {
    }
}
} // namespace

void SignalHandler::on_signal_callback(int signal)
{
    if (!instance)
        return;

    // set the atomic true to indicate signal was received, if it was already true, return
    bool expected = false;
    if (!instance->signal_flag.compare_exchange_strong(expected, true))
    {
        // A threaded callback may be stuck in a shutdown that never completes, so a repeated signal terminates.
        if (g_threaded.load())
        {
            ::signal(signal, SIG_DFL);
            raise(signal);
        }
        return;
    }

    if (g_threaded.load())
    {
        g_received_signal.store(signal);
        sem_post(&g_signal_semaphore);
        return;
    }

    instance->run_callback(signal);
}

void SignalHandler::run_callback(int signal)
{
    if (signal_handler)
    {
        signal_handler(signal);
    }

    if (exit_on_signal)
        exit(0);
}

void SignalHandler::dispatch_signal()
{
    wait_on(g_signal_semaphore);

    SignalHandler *handler = instance;
    if (!g_dispatcher_stopping.load() && handler)
        handler->run_callback(g_received_signal.load());

    sem_post(&g_dispatcher_exited);
}

void SignalHandler::register_signal_handler(hailo_exit_signal_t signal_handler_cb)
{
    install(std::move(signal_handler_cb), /*threaded=*/false);
}

void SignalHandler::register_threaded_signal_handler(hailo_exit_signal_t signal_handler_cb)
{
    install(std::move(signal_handler_cb), /*threaded=*/true);
}

void SignalHandler::install(hailo_exit_signal_t signal_handler_cb, bool threaded)
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

    g_threaded.store(threaded);
    if (threaded)
    {
        sem_init(&g_signal_semaphore, 0, 0);
        sem_init(&g_dispatcher_exited, 0, 0);
        g_received_signal.store(0);
        g_dispatcher_stopping.store(false);
        std::thread dispatcher(&SignalHandler::dispatch_signal);
        g_dispatcher_thread_id.store(dispatcher.get_id());
        dispatcher.detach();
    }

    signal_flag.store(false);
    signal(SIGINT, on_signal_callback);
}

void SignalHandler::stop_dispatcher()
{
    // exit() called from the callback runs static destructors on the dispatcher thread itself.
    if (g_dispatcher_thread_id.load() == std::this_thread::get_id())
        return;

    g_dispatcher_stopping.store(true);
    sem_post(&g_signal_semaphore);
    wait_on(g_dispatcher_exited);
    sem_destroy(&g_signal_semaphore);
    sem_destroy(&g_dispatcher_exited);
    g_threaded.store(false);
}

SignalHandler::~SignalHandler()
{
    signal(SIGINT, SIG_DFL); // Reset the signal handler to default
    if (instance == this && g_threaded.load())
        stop_dispatcher();
    instance = nullptr;
}
} // namespace signal_utils
