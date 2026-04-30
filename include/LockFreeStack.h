#pragma once

#include <atomic>
#include <optional>

// Lock-free stack using the Treiber algorithm (1986).
// Push/pop both use compare_exchange on the head pointer, so no locks needed.
//
// WARNING: this has the ABA problem. If thread A reads head = X, gets preempted,
// and by the time it resumes X has been popped and pushed back, the CAS succeeds
// even though the stack changed underneath us. For now this is fine for single-owner
// use cases. Fix it with hazard pointers or tagged pointers if you need it in production.
template <typename T>
class LockFreeStack {
private:
    struct Node {
        T data;
        Node* next;
        Node(const T& val) : data(val), next(nullptr) {}
    };

public:
    LockFreeStack() : head_(nullptr) {}

    ~LockFreeStack() {
        // drain the stack to free all nodes
        T ignored;
        while (pop(ignored)) {}
    }

    void push(const T& val) {
        Node* node = new Node(val);

        // Keep trying until our CAS succeeds. Other threads may be pushing concurrently
        // so the head could change between our load and our store.
        node->next = head_.load(std::memory_order_relaxed);
        while (!head_.compare_exchange_weak(node->next, node,
                                            std::memory_order_release,
                                            std::memory_order_relaxed)) {
            // compare_exchange_weak already updates node->next on failure, so just retry
        }
    }

    // Returns the top element if the stack isn't empty, otherwise returns nullopt.
    std::optional<T> pop() {
        Node* old_head = head_.load(std::memory_order_relaxed);

        while (true) {
            if (old_head == nullptr) {
                return std::nullopt;
            }

            // Try to swing head to the next node
            if (head_.compare_exchange_weak(old_head, old_head->next,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
                T val = std::move(old_head->data);
                delete old_head;
                return val;
            }
            // CAS failed - old_head was updated to the current head, retry
        }
    }

    // Convenience overload for output-param style
    bool pop(T& out) {
        auto result = pop();
        if (result) {
            out = std::move(*result);
            return true;
        }
        return false;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) == nullptr;
    }

private:
    std::atomic<Node*> head_;
};
