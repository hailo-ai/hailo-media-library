#pragma once

#include <queue>
#include <thread>

template <typename T>
class SmartQueue
{
private:
    std::queue<T> queue;
    size_t max_buffers;
    std::function<void(T)> on_full_callback;
    bool m_leaky;
    int m_non_leaky_timeout_in_ms;
    std::mutex m_mutex;
    std::string m_name;
    int drop_count = 0, push_count = 0;

public:
    SmartQueue(std::string name, size_t max_buffers, std::function<void(T)> on_full_callback, bool leaky=true, int non_leaky_timeout_in_ms = 1000)
        : max_buffers(max_buffers), on_full_callback(on_full_callback), m_leaky(leaky), m_non_leaky_timeout_in_ms(non_leaky_timeout_in_ms), m_name(name) {}

    bool push(T buffer)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (queue.size() < max_buffers)
        {
            queue.push(buffer);
            push_count++;
        }
        else
        {
            if (!handle_push_to_full_queue(buffer))
            {
                return false;
            }
        }

        if (true)
        {
            stats();
        }

        return true;
    }

    void stats()
    {
        if (drop_count + push_count == 100)
        {
            if (drop_count > 0)
            {
                std::cout << "--> [SmartQueue -" << m_name << "] Drop: " << drop_count << " / " << push_count + drop_count << " (" << drop_count * 100 / (push_count + drop_count) << "%) " << std::endl;            
            }
            
            drop_count = 0;
            push_count = 0;
        }
    }

    T pop()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        T buffer = queue.front();
        queue.pop();
        return buffer;
    }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return queue.size();
    }

    bool empty()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return queue.empty();
    }

private:
    bool handle_push_to_full_queue(T buffer)
    {
        if (m_leaky)
        {
            auto buffers = queue.front();
            on_full_callback(buffers);
            queue.pop();
            queue.push(buffer);
            drop_count++;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_non_leaky_timeout_in_ms));

            if (queue.size() < max_buffers)
            {
                queue.push(buffer);
                push_count++;
            }
            else
            {
                std::cerr << "Queue is still full after waiting for " << m_non_leaky_timeout_in_ms << "ms" << std::endl;
                return false;
            }
        }

        return true;
    }
};