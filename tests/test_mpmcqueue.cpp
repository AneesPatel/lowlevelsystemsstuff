#include "MPMCQueue.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>

int main() {
    // 4 producers, 4 consumers, 1M total items
    const int NUM_PRODUCERS = 4;
    const int NUM_CONSUMERS = 4;
    const int ITEMS_PER_PRODUCER = 250000;
    const int TOTAL = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    MPMCQueue<int, 1024> queue;
    std::atomic<long long> sum_produced{0};
    std::atomic<long long> sum_consumed{0};
    std::atomic<int> done_producers{0};

    auto producer = [&](int id) {
        long long local_sum = 0;
        for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
            int val = id * ITEMS_PER_PRODUCER + i;
            local_sum += val;
            // Spin until we can push (queue might be momentarily full)
            while (!queue.push(val)) {
                std::this_thread::yield();
            }
        }
        sum_produced.fetch_add(local_sum, std::memory_order_relaxed);
        done_producers.fetch_add(1, std::memory_order_release);
    };

    auto consumer = [&]() {
        long long local_sum = 0;
        int consumed = 0;
        while (true) {
            auto val = queue.pop();
            if (val) {
                local_sum += *val;
                ++consumed;
                if (consumed == TOTAL / NUM_CONSUMERS) break;
            } else {
                // Queue empty, either producers are still running or we're done
                if (done_producers.load(std::memory_order_acquire) == NUM_PRODUCERS && queue.empty()) {
                    break;
                }
                std::this_thread::yield();
            }
        }
        sum_consumed.fetch_add(local_sum, std::memory_order_relaxed);
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_PRODUCERS; ++i)
        threads.emplace_back(producer, i);
    for (int i = 0; i < NUM_CONSUMERS; ++i)
        threads.emplace_back(consumer);

    for (auto& t : threads)
        t.join();

    // Drain any remaining items (edge case with uneven distribution)
    auto val = queue.pop();
    while (val) {
        sum_consumed.fetch_add(*val, std::memory_order_relaxed);
        val = queue.pop();
    }

    if (sum_produced.load() == sum_consumed.load()) {
        std::cout << "MPMCQueue test passed! Sum: " << sum_produced.load() << std::endl;
        return 0;
    } else {
        std::cout << "MPMCQueue test FAILED! Produced sum: " << sum_produced.load()
                  << " Consumed sum: " << sum_consumed.load() << std::endl;
        return 1;
    }
}
