#include "SpinLock.h"
#include <iostream>
#include <thread>
#include <vector>

int main() {
    SpinLock lock;
    int counter = 0;
    const int NUM_ITERATIONS = 100000;
    const int NUM_THREADS = 4;

    // Worker function: increments the shared counter under lock
    auto worker = [&]() {
        for (int i = 0; i < NUM_ITERATIONS; ++i) {
            lock.lock();
            counter++;
            lock.unlock(); //check spinlock test;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    // Verify that the counter reached the expected value
    if (counter == NUM_ITERATIONS * NUM_THREADS) {
        std::cout << "SpinLock test passed successfully! Final counter: " << counter << std::endl;
        return 0;
    } else {
        std::cout << "SpinLock test failed! Final counter: " << counter 
                  << " (Expected: " << (NUM_ITERATIONS * NUM_THREADS) << ")" << std::endl;
        return 1;
    }
}
