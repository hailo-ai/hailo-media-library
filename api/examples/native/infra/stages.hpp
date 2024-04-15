#pragma once

#include "media_library/dsp_utils.hpp"
#include "hailo/hailort.hpp"
#include "buffer_utils.hpp"
#include "base.hpp"
#include "metadata.hpp"
#include "utils.hpp"
#include "smart_queue.hpp"

#include <queue>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>


class IStage
{
protected:
    bool m_end_of_stream = false;
public:
    virtual void set_end_of_stream(bool end_of_stream)
    {
        m_end_of_stream = end_of_stream;
    }

    virtual void loop(){};
    virtual ~IStage() = default;
};

template <typename T>
class Stage : public IStage
{
private:
    std::condition_variable m_cv;
    std::mutex m_mutex;
    SmartQueue<T> m_queue;
    std::string m_stage_name;

    // FPS Related memebers
    bool m_print_fps = false;
    bool m_first_fps_measured = false;
    std::chrono::steady_clock::time_point m_start_time;
    int m_counter = 0;

public:

    Stage(std::string name, size_t queue_size, std::function<void(T)> on_queue_release, bool leaky=true, int non_leaky_timeout_in_ms = 1000) :
          m_queue(name, queue_size, on_queue_release, leaky, non_leaky_timeout_in_ms), m_stage_name(name) {}

    virtual ~Stage() = default;

    virtual int init()
    {
        return SUCCESS;
    }

    virtual int deinit()
    {
        return SUCCESS;
    }

    virtual int process(T data)
    {
        return SUCCESS;
    }

    void push(T data)
    {
        m_mutex.lock();
        m_queue.push(data);
        m_mutex.unlock();
        m_cv.notify_one();
    }

    void set_end_of_stream(bool end_of_stream) override
    {
        m_end_of_stream = end_of_stream;
        m_cv.notify_all();
    }

    void loop() override
    {
        init();

        while (true)
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            m_cv.wait(lock, [&]() {
                return !m_queue.empty() || m_end_of_stream;
            });

            if (m_queue.empty() || m_end_of_stream) {
                break;
            }

            T data = m_queue.pop();
            lock.unlock();

            if (m_print_fps && !m_first_fps_measured)
            {
                m_start_time = std::chrono::steady_clock::now();
                m_first_fps_measured = true;
            }

            process(data);

            if (m_print_fps)
            {
                m_counter++;
                print_fps();
            }
        }

        deinit();
    }

    void set_print_fps(bool print_fps)
    {
        m_print_fps = print_fps;
    }

    void print_fps()
    {
        std::chrono::steady_clock::time_point current_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed_seconds = current_time - m_start_time;

        if (elapsed_seconds.count() >= 1.0) {
            std::cout << "[ " << m_stage_name << " ] Buffers processed per second: " << m_counter << std::endl;
            m_counter = 0;
            m_start_time = current_time;
        }
    }
};

template <typename T, typename U>
class ProducableStage : public Stage<T>
{
    private:
        std::vector<std::shared_ptr<Stage<U>>> m_subscribers;

    public:
        ProducableStage(std::string name, size_t queue_size, std::function<void(T)> on_queue_release, bool leaky=true, int non_leaky_timeout_in_ms = 1000) :
            Stage<T>(name, queue_size, on_queue_release, leaky, non_leaky_timeout_in_ms) {}


        void add_subscriber(std::shared_ptr<Stage<U>> subscriber)
        {
            m_subscribers.push_back(subscriber);
        }

        void send_to_subscribers(U data)
        {
            for (auto &subscriber : m_subscribers)
            {
                subscriber->push(data);
            }
        }
};

class ProducableBufferStage : public ProducableStage<BufferPtr, BufferPtr>
{
    public:
        ProducableBufferStage(std::string name, size_t queue_size, bool leaky=true, int non_leaky_timeout_in_ms = 1000) :
            ProducableStage<BufferPtr, BufferPtr>(name, queue_size, drop_buffer, leaky, non_leaky_timeout_in_ms) {}
};