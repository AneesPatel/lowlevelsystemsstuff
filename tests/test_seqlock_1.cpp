#include "SeqLock.h"
#include <iostream>
#include <cassert>

// Represents some shared POD data we want to protect
struct SharedData {
    int x;
    int y;
};

int main() {
    SeqLock lock;
    SharedData data = {0, 0};

    // Simulate a write
    lock.write_lock();
    data.x = 42;
    data.y = 100;
    lock.write_unlock();

    // Simulate a read
    int read_x = 0;
    int read_y = 0;
    
    // Read retry loop
    size_t seq;
    do {
        seq = lock.read_begin();
        // Do the read
        read_x = data.x;
        read_y = data.y;
    } while (lock.read_retry(seq));

    assert(read_x == 42);
    assert(read_y == 100);

    std::cout << "SeqLock test 1 (Single Thread Basic) passed successfully!" << std::endl;
    return 0;
}
