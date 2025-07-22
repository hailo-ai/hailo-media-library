#pragma once

#include <functional>
#include <iostream>
#include <vector>
#include <mutex>
#include <optional>

template <typename T> class ConcurrentQueue
{
  public:
    explicit ConcurrentQueue(size_t capacity) : m_buffer(capacity), m_head(0), m_tail(0), m_size(0)
    {
    }

    void enqueue(const T &item)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_size == m_buffer.capacity())
        {
            // Overwrite the oldest element (leaky behavior)
            m_head = (m_head + 1) % m_buffer.capacity(); // Update to use m_buffer.capacity()
            --m_size;
        }
        m_buffer[m_tail] = item;
        m_tail = (m_tail + 1) % m_buffer.capacity();
        ++m_size;
    }

    void enqueue_many(const std::vector<T> &items)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        for (const auto &item : items)
        {
            m_buffer[m_tail] = item;
            m_tail = (m_tail + 1) % m_buffer.capacity();
        }

        if (m_size + items.size() > m_buffer.capacity())
        {
            // If adding these items exceeds capacity, we need to drop the oldest items
            size_t excess = (m_size + items.size()) - m_buffer.capacity();
            m_head = (m_head + excess) % m_buffer.capacity(); // Move head forward by the number of excess items
            m_size = m_buffer.capacity();
        }
        else
        {
            m_size += items.size();
        }
    }

    std::optional<T> dequeue()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_size == 0)
        {
            return std::nullopt; // Queue is empty
        }

        T item = m_buffer[m_head];
        m_head = (m_head + 1) % m_buffer.capacity();
        --m_size;
        return item;
    }

    std::vector<T> dequeue_many(const std::function<bool(const T &)> &predicate)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        std::vector<T> items;

        for (T item = m_buffer[m_head]; m_size > 0 && predicate(item); item = m_buffer[m_head])
        {
            items.push_back(item);
            m_head = (m_head + 1) % m_buffer.capacity();
            --m_size;
        }

        return items;
    }

    std::optional<T> find_first(const std::function<bool(const T &)> &predicate,
                                const std::function<bool(const T &)> &continue_predicate = nullptr)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        size_t current_head = m_head;
        size_t count = m_size;
        while (count > 0)
        {
            if (predicate(m_buffer[current_head]))
            {
                return m_buffer[current_head];
            }
            if (continue_predicate && !continue_predicate(m_buffer[current_head]))
            {
                break;
            }
            current_head = (current_head + 1) % m_buffer.capacity(); // Move forward
            --count;
        }
        return std::nullopt; // No matching item found
    }

    std::optional<T> find_last(const std::function<bool(const T &)> &predicate,
                               const std::function<bool(const T &)> &continue_predicate = nullptr)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        size_t current_tail = (m_tail + m_buffer.capacity() - 1) % m_buffer.capacity(); // Start from the last item
        size_t count = m_size;
        while (count > 0)
        {
            if (predicate(m_buffer[current_tail]))
            {
                return m_buffer[current_tail];
            }
            if (continue_predicate && !continue_predicate(m_buffer[current_tail]))
            {
                break;
            }
            current_tail = (current_tail + m_buffer.capacity() - 1) % m_buffer.capacity(); // Move backward
            --count;
        }
        return std::nullopt; // No matching item found
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_size;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_size == 0;
    }

    std::optional<T> peek() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_size == 0)
        {
            return std::nullopt;
        }
        return m_buffer[m_head];
    }

  public:
    std::vector<T> m_buffer;
    size_t m_head;
    size_t m_tail;
    size_t m_size;
    mutable std::mutex m_mutex;
};
