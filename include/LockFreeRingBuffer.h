#pragma once

#include <atomic>
#include <cstddef>
#include <utility>

// A very basic Single-Producer Single-Consumer (SPSC) lock-free ring buffer
template <typename T, size_t Size>
class LockFreeRingBuffer {
public:
    LockFreeRingBuffer() : head_(0), tail_(0) {}

    // Try to push an item to the buffer. Returns true if successful, false if the buffer is full.
    bool push(const T& item) {
        size_t current_tail = tail_.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) % Size;

        // If the next tail position reaches the current head, the buffer is full.
        // We use memory_order_acquire to ensure we see the most recent head value updated by pop().
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[current_tail] = item;
        
        // memory_order_release ensures that the item is written before the tail index is updated.
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Try to pop an item from the buffer. Returns true if successful, false if the buffer is empty.
    bool pop(T& item) {
        size_t current_head = head_.load(std::memory_order_relaxed);

        // If head equals tail, the buffer is empty.
        // We use memory_order_acquire to ensure we see the most recent tail value updated by push().
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        item = std::move(buffer_[current_head]);
        
        // memory_order_release ensures that the item is read before the head index is updated.
        head_.store((current_head + 1) % Size, std::memory_order_release);
        return true;
    }

    // Check if the buffer is completely empty
    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

private:
    T buffer_[Size];
    // Cache line padding could be added here to prevent false sharing between head_ and tail_ in more advanced implementations,
    // but we'll keep it as basic as possible as requested!
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};
