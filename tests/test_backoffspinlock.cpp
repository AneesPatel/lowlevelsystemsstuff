#include "BackoffSpinLock.h"
#include <iostream>
#include <thread>
#include <vector>

int main() {
    BackoffSpinLock lock;
    int counter = 0;
    const int NUM_THREADS = 8;
    const int NUM_ITERATIONS = 100000;

    auto worker = [&]() {
        for (int i = 0; i < NUM_ITERATIONS; ++i) {
            lock.lock();
            counter++;
            lock.unlock();
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    int expected = NUM_THREADS * NUM_ITERATIONS;
    if (counter == expected) {
        std::cout << "BackoffSpinLock test passed! Counter: " << counter << std::endl;
        return 0;
    } else {
        std::cout << "BackoffSpinLock test FAILED! Got " << counter
                  << " expected " << expected << std::endl;
        return 1;
    }
}
