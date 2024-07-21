#pragma once

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

/*
    The reason we need this wrapper in the first place is that OpenCV has a serious memory leak, as detailed here:

    * https://github.com/opencv/opencv/issues/23912
    * https://forum.opencv.org/t/memory-leak-detected-by-valgrind-in-cv-resize/3509/2

    The memory leak occurs when using parallel functions (like cv::resize) in a multi-threaded environment. If the parent thread is terminated, the resources are not properly released until the entire process is terminated.

    Our workaround is to create a wrapper that handles calls to OpenCV functions from a single thread. This single thread is part of a static instance, and therefore, we keep it alive as long as the process is alive.
*/

#ifndef MEDIALIB_THREADPOOL_DEFAULT_SIZE
#define MEDIALIB_THREADPOOL_DEFAULT_SIZE 3
#endif

class ThreadPool
{
public:
    ThreadPool();
    ThreadPool(size_t);
    ~ThreadPool();
    template <class F, class... Args>
    auto enqueue(F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>; // async call
    template <class F, class... Args>
    auto invoke(F &&f, Args &&...args) -> std::result_of<F(Args...)>::type; // sync call
    static std::shared_ptr<ThreadPool> GetInstance();

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;

    static std::shared_ptr<ThreadPool> instance;
    static std::mutex instance_mutex;
};

// enqueue a task that will be executed by a worker thread
// the return value is a future that will be set once the task is completed
template <class F, class... Args>
auto ThreadPool::enqueue(F &&f, Args &&...args)
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task]()
                      { (*task)(); });
    }
    condition.notify_one();
    return res;
}

// enqueue a task that will be executed by a worker thread
// the function will block until the execution is complete
// the return value is the result of the function
template <class F, class... Args>
auto ThreadPool::invoke(F &&f, Args &&...args)
    -> std::result_of<F(Args...)>::type
{
    return enqueue(std::forward<F>(f), std::forward<Args>(args)...).get();
}