#include "../src/lock_free_mpmc_queue.hpp"

#include <thread>

#include "gtest/gtest.h"

class LockFreeMpmcQueueTest : public testing::Test {};

TEST(LockFreeMpmcQueueTest, IsEmptyInitially) {
    jr::lock_free_mpmc_queue<int, 4> q;
    ASSERT_TRUE(q.empty()) << "Queue should be empty on initialisation";
    ASSERT_EQ(q.size(), 0) << "Queue size should be zero when the queue is empty";
    ASSERT_EQ(q.capacity(), 4) << "Queue capacity should be four";
}

TEST(LockFreeMpmcQueueTest, FullQueueCannotTakeExtra) {
    jr::lock_free_mpmc_queue<int, 1> q;
    q.push(1);
    ASSERT_EQ(q.size(), 1) << "Queue should have a single element";

    ASSERT_FALSE(q.push(2)) << "Full queue should not accept extra elements";
    ASSERT_EQ(q.size(), 1) << "Queue should have a single element";

    int i{0};
    q.pop(i);
    ASSERT_EQ(i, 1);
}

TEST(LockFreeMpmcQueueTest, PopOnEmptyQueueIsNoOp) {
    jr::lock_free_mpmc_queue<int, 1> q;
    int i;
    ASSERT_FALSE(q.pop(i)) << "Queue pop should return false if the queue is empty";
    ASSERT_EQ(q.size(), 0) << "Pop on an empty queue should leave it in a consistent state";
}

TEST(LockFreeMpmcQueueTest, PushPopFifoOrdering) {
    jr::lock_free_mpmc_queue<int, 4> q;
    for (int i = 0; i < 4; ++i) {
        q.push(i);
    }
    ASSERT_EQ(q.size(), 4) << "Queue should have four elements";
    for (int i = 0; i < 4; ++i) {
        int j;
        q.pop(j);
        ASSERT_EQ(j, i);
    }
    ASSERT_EQ(q.size(), 0) << "Queue should now be empty";
}

TEST(LockFreeMpmcQueueTest, PopIfTest) {
    jr::lock_free_mpmc_queue<int, 1> q;
    q.push(1);
    ASSERT_EQ(q.size(), 1) << "Queue should have one element";

    int j = -1;
    ASSERT_FALSE(q.pop_if([](const int& k) {return k % 2 == 0;}, j));
    ASSERT_EQ(j, -1);

    ASSERT_TRUE(q.pop_if([](const int& k) {return k % 2 == 1;}, j));
    ASSERT_EQ(j, 1);

    ASSERT_EQ(q.size(), 0) << "Queue should now be empty";
}

TEST(LockFreeMpmcQueueTest, ConsumeTest) {
    jr::lock_free_mpmc_queue<int, 8> q;
    for (int i = 0; i < 8; ++i) {
        q.push(i);
    }
    ASSERT_EQ(q.size(), 8) << "Queue should have eight elements";

    const auto j = q.consume([](const int& a) {return a > 8;});
    ASSERT_EQ(j, 8) << "All elements should have been popped from the queue as they satisfy the predicate in lock_free_mpmc_queue::consume";
    ASSERT_EQ(q.size(), 0) << "Queue should now be empty";

    for (int i = 0; i < 8; ++i) {
        q.push(i);
    }
    ASSERT_EQ(q.size(), 8) << "Queue should have eight elements";
    const auto k = q.consume([](const int& a) {return a > 5;});
    ASSERT_EQ(k, 7) << "Seven elements (from 0 up to and incl. 6) should have been popped";
    ASSERT_EQ(q.size(), 1) << "Only one element should remain in the queue";

    int l;
    q.peek(l);
    ASSERT_EQ(l, 7) << "Value 7 should be the head of the queue";
}

TEST(LockFreeMpmcQueueTest, PeekTest) {
    jr::lock_free_mpmc_queue<int, 1> q;
    int j = -1;
    ASSERT_FALSE(q.peek(j)) << "lock_free_mpmc_queue::peek should return false when the queue is empty";
    ASSERT_EQ(j, -1) << "No value was found, so j should not have changed";

    q.push(5);
    ASSERT_EQ(q.size(), 1) << "Queue should have one element";
    ASSERT_TRUE(q.peek(j)) << "lock_free_mpmc_queue::peek should return true when the queue is populated";
    ASSERT_EQ(j, 5) << "Value from queue should be copy-assigned to variable j";

    ASSERT_EQ(q.size(), 1) << "Size of the queue should not change after using lock_free_mpmc_queue::peek";
}

TEST(LockFreeMpmcQueueTest, PushKeepNTest) {
    jr::lock_free_mpmc_queue<int, 4> q;
    for (int i  = 1; i <= 4; ++i) {
        ASSERT_TRUE(q.push_keep_n(i));
    }
    ASSERT_EQ(q.size(), 4) << "There should be four elements in the queue";
    int head = -1;
    q.peek(head);
    ASSERT_EQ(head, 1) << "The head of the queue should be 1";

    q.push_keep_n(5);
    ASSERT_EQ(q.size(), 4) << "Queue should still have four elements";
    int head2 = -1;
    q.peek(head2);
    ASSERT_EQ(head2, 2) << "The head of the queue should be 2 (1 should have been evicted)";
    for (int i = 2; i <= 5; i++) {
        int h = -1;
        q.peek(h);
        ASSERT_EQ(h, i);
        int j = -1;
        q.pop(j);
    }
}

TEST(LockFreeMpmcQueueTest, ConcurrentTestWithPrimitiveData) {
    jr::lock_free_mpmc_queue<int, 128> q;
    int dequeued[100] = {};
    std::jthread threads[20];

    // Producers
    for (int i = 0; i != 10; ++i) {
        threads[i] = std::jthread([&](int k) {
            for (int j = 0; j != 10; ++j) {
                q.push(k * 10 + j);
            }
        }, i);
    }

    // Consumers
    for (int i = 10; i != 20; ++i) {
        threads[i] = std::jthread([&]() {
            int item;
            for (int j = 0; j != 20; ++j) {
                if (q.pop(item)) {
                    ++dequeued[item];
                }
            }
        });
    }

    for (auto & thread : threads) {
        thread.join();
    }

    // If consumers finish before the producers, then we need a final run to collect any potential leftovers in the queue
    int item;
    while (q.pop(item)) {
        ++dequeued[item];
    }

    for (int i : dequeued) {
        ASSERT_EQ(i, 1);
    }
}

TEST(LockFreeMpmcQueueTest, ConcurrentTestWithNonTrivialData) {
    struct Data {
        long long a = 0;
        int b = -1;
        Data() = default;
        Data(int b) : b(b) {}
    };
    ASSERT_FALSE(std::is_trivial_v<Data>);
    jr::lock_free_mpmc_queue<Data, 128> q;
    int dequeued[100] = {};
    std::jthread threads[20];

    // Producers
    for (int i = 0; i != 10; ++i) {
        threads[i] = std::jthread([&](int k) {
            for (int j = 0; j != 10; ++j) {
                q.push(Data{k * 10 + j});
            }
        }, i);
    }

    // Consumers
    for (int i = 10; i != 20; ++i) {
        threads[i] = std::jthread([&]() {
            Data item;
            for (int j = 0; j != 20; ++j) {
                if (q.pop(item)) {
                    ++dequeued[item.b];
                }
            }
        });
    }

    for (auto & thread : threads) {
        thread.join();
    }

    // If consumers finish before the producers, then we need a final run to collect any potential leftovers in the queue
    Data item;
    while (q.pop(item)) {
        ++dequeued[item.b];
    }

    for (int i : dequeued) {
        ASSERT_EQ(i, 1);
    }
}