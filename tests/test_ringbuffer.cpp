#include "LockFreeRingBuffer.h"
#include <iostream>
#include <thread>
#include <cassert>

int main() {
    LockFreeRingBuffer<int, 10> rb;
    
    // Single producer thread
    std::thread producer([&rb]() {
        for (int i = 0; i < 100; ++i) {
            while (!rb.push(i)) {
                std::this_thread::yield(); // Wait if buffer is full
            }
        }
    });

    // Single consumer thread
    std::thread consumer([&rb]() {
        for (int i = 0; i < 100; ++i) {
            int val;
            while (!rb.pop(val)) {
                std::this_thread::yield(); // Wait if buffer is empty
            }
            assert(val == i); // Items must be received in order
        }
    });

    producer.join();
    consumer.join();

    std::cout << "LockFreeRingBuffer test passed successfully!" << std::endl;
    return 0;
}
