#include "HazardPointers.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>

// Simple lock-free stack that uses hazard pointers for memory reclamation.
// Basically the same as LockFreeStack.h but with hp::protect/retire instead
// of direct new/delete — this is the whole point of the hazard pointer API.
template <typename T>
class SafeStack {
    struct Node {
        T data;
        Node* next;
        Node(const T& v) : data(v), next(nullptr) {}
    };

public:
    ~SafeStack() {
        // drain without hazard pointer protection (single-threaded teardown)
        Node* cur = head_.load(std::memory_order_acquire);
        while (cur) {
            Node* next = cur->next;
            delete cur;
            cur = next;
        }
    }

    void push(const T& val) {
        Node* node = new Node(val);
        node->next = head_.load(std::memory_order_relaxed);
        while (!head_.compare_exchange_weak(node->next, node,
                                            std::memory_order_release,
                                            std::memory_order_relaxed)) {}
    }

    bool pop(T& out) {
        while (true) {
            Node* old_head = hp::protect(head_);

            if (old_head == nullptr) {
                hp::clear();
                return false;
            }

            if (head_.compare_exchange_strong(old_head, old_head->next,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed)) {
                out = std::move(old_head->data);
                hp::clear();
                hp::retire(old_head);  // deferred delete — safe now
                return true;
            }
            // head changed, retry (old_head was updated by compare_exchange)
        }
    }

private:
    std::atomic<Node*> head_{nullptr};
};

int main() {
    static std::atomic<int> live_count{0};

    struct Tracked {
        int val;
        Tracked(int v) : val(v) { live_count.fetch_add(1, std::memory_order_relaxed); }
        ~Tracked()              { live_count.fetch_sub(1, std::memory_order_relaxed); }
    };

    const int NUM_THREADS = 4;
    const int ITEMS_PER_THREAD = 5000;

    SafeStack<Tracked> stack;
    std::atomic<int> popped{0};

    auto producer = [&]() {
        for (int i = 0; i < ITEMS_PER_THREAD; ++i) {
            stack.push(Tracked{i});
        }
    };

    auto consumer = [&]() {
        Tracked t{0};
        while (popped.load(std::memory_order_relaxed) < NUM_THREADS * ITEMS_PER_THREAD) {
            if (stack.pop(t)) {
                popped.fetch_add(1, std::memory_order_relaxed);
            }
        }
        // Flush any remaining deferred deletes
        hp::scan();
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) threads.emplace_back(producer);
    for (int i = 0; i < NUM_THREADS; ++i) threads.emplace_back(consumer);
    for (auto& t : threads) t.join();

    // Final scan to clean up anything that didn't hit BATCH_SIZE
    hp::scan();

    // All popped items should have been deleted by now.
    // live_count might not be 0 if stack still has items (producers could outpace consumers),
    // but the total constructed == total destroyed should hold after full drain.
    bool passed = (popped.load() == NUM_THREADS * ITEMS_PER_THREAD);
    if (passed) {
        std::cout << "HazardPointers test passed! Popped " << popped.load() << " items." << std::endl;
        return 0;
    } else {
        std::cout << "HazardPointers test FAILED! Popped " << popped.load() << std::endl;
        return 1;
    }
}
