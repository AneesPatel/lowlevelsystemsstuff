#include "LockFreeStack.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>

int main() {
    LockFreeStack<int> stack;
    const int ITEMS_PER_THREAD = 10000;
    const int NUM_PRODUCERS = 4;
    const int NUM_CONSUMERS = 4;
    const int TOTAL = ITEMS_PER_THREAD * NUM_PRODUCERS;

    std::atomic<int> popped_count{0};

    // Producers push ITEMS_PER_THREAD each
    auto producer = [&](int thread_id) {
        for (int i = 0; i < ITEMS_PER_THREAD; ++i) {
            stack.push(thread_id * ITEMS_PER_THREAD + i);
        }
    };

    // Consumers spin until they've collectively drained everything
    auto consumer = [&]() {
        while (popped_count.load(std::memory_order_relaxed) < TOTAL) {
            int val;
            if (stack.pop(val)) {
                popped_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> threads;

    for (int i = 0; i < NUM_PRODUCERS; ++i)
        threads.emplace_back(producer, i);

    for (int i = 0; i < NUM_CONSUMERS; ++i)
        threads.emplace_back(consumer);

    for (auto& t : threads)
        t.join();

    if (popped_count.load() == TOTAL) {
        std::cout << "LockFreeStack test passed! Popped " << popped_count.load() << " items." << std::endl;
        return 0;
    } else {
        std::cout << "LockFreeStack test FAILED! Got " << popped_count.load()
                  << " expected " << TOTAL << std::endl;
        return 1;
    }
}
