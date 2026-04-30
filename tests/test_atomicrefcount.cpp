#include "AtomicRefCount.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>

static std::atomic<int> instance_count{0};

struct Widget : public RefCounted<Widget> {
    int value;
    Widget(int v) : value(v) { instance_count.fetch_add(1, std::memory_order_relaxed); }
    ~Widget()                { instance_count.fetch_sub(1, std::memory_order_relaxed); }
};

int main() {
    const int NUM_THREADS = 8;
    bool all_passed = true;

    // Test 1: destructor called exactly once
    {
        Widget* w = make_ref_counted<Widget>(42);
        // refcount is 1 at this point

        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            w->add_ref();
        }
        // refcount is now NUM_THREADS + 1

        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back([w]() {
                // do some "work" with the object
                (void)w->value;
                w->release();
            });
        }

        for (auto& t : threads) t.join();

        // release our own initial ref
        w->release();
        // w is deleted now, instance_count should be 0
    }

    if (instance_count.load() != 0) {
        std::cout << "Test 1 FAILED: instance_count = " << instance_count.load()
                  << " (expected 0)" << std::endl;
        all_passed = false;
    }

    // Test 2: single-threaded refcounting sanity check
    {
        Widget* a = make_ref_counted<Widget>(1);
        if (a->use_count() != 1) { std::cout << "Test 2a FAILED\n"; all_passed = false; }

        a->add_ref();
        if (a->use_count() != 2) { std::cout << "Test 2b FAILED\n"; all_passed = false; }

        a->release();
        if (a->use_count() != 1) { std::cout << "Test 2c FAILED\n"; all_passed = false; }

        a->release(); // deletes
        if (instance_count.load() != 0) { std::cout << "Test 2d FAILED\n"; all_passed = false; }
    }

    if (all_passed) {
        std::cout << "AtomicRefCount tests passed!" << std::endl;
        return 0;
    }
    return 1;
}
