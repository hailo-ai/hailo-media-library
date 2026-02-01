#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <vector>

#include "hailo_analytics/pipeline/core/queue.hpp"
#include "core_tests_common.hpp"

using namespace hailo_analytics::pipeline;
using ::testing::_;
using ::testing::Return;

// ============================================================================
// Queue Tests
// ============================================================================

TEST_F(QueueTest, Constructor)
{
    ASSERT_NO_THROW({ Queue queue("parent", "test_queue", 5, false); });
}

TEST_F(QueueTest, ConstructorLeaky)
{
    ASSERT_NO_THROW({ Queue queue("parent", "test_queue", 5, true); });
}

TEST_F(QueueTest, GetName)
{
    Queue queue("parent", "test_queue", 5);
    EXPECT_EQ(queue.name(), "test_queue");
}

TEST_F(QueueTest, InitialSize)
{
    Queue queue("parent", "test_queue", 5);
    EXPECT_EQ(queue.size(), 0);
}

TEST_F(QueueTest, PushAndSize)
{
    Queue queue("parent", "test_queue", 5);
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;
    BufferPtr buffer = std::make_shared<Buffer>(mock_buffer);

    queue.push(buffer);
    EXPECT_EQ(queue.size(), 1);
}

TEST_F(QueueTest, PushMultipleAndSize)
{
    Queue queue("parent", "test_queue", 5);
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;

    for (int i = 0; i < 3; i++)
    {
        BufferPtr buffer = std::make_shared<Buffer>(mock_buffer);
        queue.push(buffer);
    }

    EXPECT_EQ(queue.size(), 3);
}

TEST_F(QueueTest, PopFromEmptyQueueBlocks)
{
    Queue queue("parent", "test_queue", 5);
    std::atomic<bool> pop_returned{false};
    BufferPtr result = nullptr;

    // Start a thread that will try to pop from empty queue
    std::thread pop_thread([&]() {
        result = queue.pop();
        pop_returned = true;
    });

    // Give the thread time to block on pop
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(pop_returned);

    // Push a buffer to unblock
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;
    BufferPtr buffer = std::make_shared<Buffer>(mock_buffer);
    queue.push(buffer);

    // Wait for pop to complete
    pop_thread.join();
    EXPECT_TRUE(pop_returned);
    EXPECT_NE(result, nullptr);
}

TEST_F(QueueTest, PopReturnsCorrectBuffer)
{
    Queue queue("parent", "test_queue", 5);
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;
    BufferPtr buffer = std::make_shared<Buffer>(mock_buffer);

    queue.push(buffer);
    BufferPtr result = queue.pop();

    EXPECT_EQ(result, buffer);
    EXPECT_EQ(queue.size(), 0);
}

TEST_F(QueueTest, FIFOOrder)
{
    Queue queue("parent", "test_queue", 5);
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;

    std::vector<BufferPtr> buffers;
    for (int i = 0; i < 3; i++)
    {
        BufferPtr buffer = std::make_shared<Buffer>(mock_buffer);
        buffers.push_back(buffer);
        queue.push(buffer);
    }

    for (int i = 0; i < 3; i++)
    {
        BufferPtr result = queue.pop();
        EXPECT_EQ(result, buffers[i]);
    }
}

TEST_F(QueueTest, NonLeakyQueueBlocksWhenFull)
{
    Queue queue("parent", "test_queue", 2, false);
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;
    std::atomic<bool> push_blocked{false};
    std::atomic<bool> push_completed{false};

    // Fill the queue
    queue.push(std::make_shared<Buffer>(mock_buffer));
    queue.push(std::make_shared<Buffer>(mock_buffer));
    EXPECT_EQ(queue.size(), 2);

    // Try to push when full (should block)
    std::thread push_thread([&]() {
        push_blocked = true;
        queue.push(std::make_shared<Buffer>(mock_buffer));
        push_completed = true;
    });

    // Give time for push to block
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(push_blocked);
    EXPECT_FALSE(push_completed);

    // Pop to make space
    queue.pop();

    // Wait for push to complete
    push_thread.join();
    EXPECT_TRUE(push_completed);
    EXPECT_EQ(queue.size(), 2);
}

TEST_F(QueueTest, LeakyQueueDropsOldestWhenFull)
{
    Queue queue("parent", "test_queue", 2, true);
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;

    BufferPtr buffer1 = std::make_shared<Buffer>(mock_buffer);
    BufferPtr buffer2 = std::make_shared<Buffer>(mock_buffer);
    BufferPtr buffer3 = std::make_shared<Buffer>(mock_buffer);

    queue.push(buffer1);
    queue.push(buffer2);
    EXPECT_EQ(queue.size(), 2);

    // Push third buffer - should drop buffer1
    queue.push(buffer3);
    EXPECT_EQ(queue.size(), 2);

    // First pop should return buffer2 (buffer1 was dropped)
    BufferPtr result1 = queue.pop();
    EXPECT_EQ(result1, buffer2);

    BufferPtr result2 = queue.pop();
    EXPECT_EQ(result2, buffer3);
}

TEST_F(QueueTest, FlushClearsQueue)
{
    Queue queue("parent", "test_queue", 5);
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;

    for (int i = 0; i < 3; i++)
    {
        queue.push(std::make_shared<Buffer>(mock_buffer));
    }
    EXPECT_EQ(queue.size(), 3);

    queue.flush();
    EXPECT_EQ(queue.size(), 0);
}

TEST_F(QueueTest, FlushUnblocksPopWithNullptr)
{
    Queue queue("parent", "test_queue", 5);
    BufferPtr result = nullptr;
    std::atomic<bool> pop_completed{false};

    // Start thread that will block on pop
    std::thread pop_thread([&]() {
        result = queue.pop();
        pop_completed = true;
    });

    // Give time for pop to block
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(pop_completed);

    // Flush should unblock the pop
    queue.flush();

    pop_thread.join();
    EXPECT_TRUE(pop_completed);
    EXPECT_EQ(result, nullptr);
}

TEST_F(QueueTest, PushAfterFlushDoesNothing)
{
    Queue queue("parent", "test_queue", 5);
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;

    queue.flush();

    BufferPtr buffer = std::make_shared<Buffer>(mock_buffer);
    queue.push(buffer);

    EXPECT_EQ(queue.size(), 0);
}

TEST_F(QueueTest, QueuePtrCreation)
{
    QueuePtr queue_ptr = std::make_shared<Queue>("parent", "test_queue", 5);
    ASSERT_NE(queue_ptr, nullptr);
    EXPECT_EQ(queue_ptr->name(), "test_queue");
}

TEST_F(QueueTest, MultipleProducersSingleConsumer)
{
    Queue queue("parent", "test_queue", 100);
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;
    const int num_producers = 3;
    const int items_per_producer = 10;
    std::atomic<int> consumed_count{0};

    // Start multiple producer threads
    std::vector<std::thread> producers;
    for (int p = 0; p < num_producers; p++)
    {
        producers.emplace_back([&]() {
            for (int i = 0; i < items_per_producer; i++)
            {
                queue.push(std::make_shared<Buffer>(mock_buffer));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    // Start consumer thread
    std::thread consumer([&]() {
        for (int i = 0; i < num_producers * items_per_producer; i++)
        {
            BufferPtr buffer = queue.pop();
            if (buffer != nullptr)
            {
                consumed_count++;
            }
        }
    });

    // Wait for all threads
    for (auto &t : producers)
    {
        t.join();
    }
    consumer.join();

    EXPECT_EQ(consumed_count, num_producers * items_per_producer);
    EXPECT_EQ(queue.size(), 0);
}

TEST_F(QueueTest, DestructorFlushesQueue)
{
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;
    BufferPtr result = nullptr;
    std::atomic<bool> pop_completed{false};

    {
        Queue queue("parent", "test_queue", 5);

        // Start thread that will block on pop
        std::thread pop_thread([&]() {
            result = queue.pop();
            pop_completed = true;
        });

        // Give time for pop to block
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_FALSE(pop_completed);

        // Destructor should flush and unblock
        pop_thread.detach();
    } // Queue destructor called here

    // Give time for pop to complete after destructor
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(pop_completed);
}

TEST_F(QueueTest, MaxBuffersEnforcement)
{
    const size_t max_size = 3;
    Queue queue("parent", "test_queue", max_size, true);
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;

    // Push more than max
    for (size_t i = 0; i < max_size + 2; i++)
    {
        queue.push(std::make_shared<Buffer>(mock_buffer));
    }

    // Size should never exceed max for leaky queue
    EXPECT_EQ(queue.size(), max_size);
}

TEST_F(QueueTest, EmptyQueueAfterPopAll)
{
    Queue queue("parent", "test_queue", 5);
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;

    for (int i = 0; i < 3; i++)
    {
        queue.push(std::make_shared<Buffer>(mock_buffer));
    }

    for (int i = 0; i < 3; i++)
    {
        queue.pop();
    }

    EXPECT_EQ(queue.size(), 0);
}
