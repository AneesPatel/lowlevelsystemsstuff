#pragma once

#include <atomic>
#include <cstddef>
#include <optional>

// Multi-Producer Multi-Consumer queue based on Dmitry Vyukov's design.
// Each slot has its own sequence counter so producers and consumers can
// coordinate without a single shared lock.
//
// The capacity must be a power of two (not enforced, but things will break if it isn't).
// Push/pop spin on their respective slots if the queue is full/empty — this is
// intentional for low-latency use, not a bug.
template <typename T, size_t Capacity>
class MPMCQueue {
private:
    struct Slot {
        T data;
        // seq == index means the slot is empty and ready to write
        // seq == index + 1 means the slot has data ready to read
        std::atomic<size_t> seq;
    };

public:
    MPMCQueue() {
        for (size_t i = 0; i < Capacity; ++i) {
            slots_[i].seq.store(i, std::memory_order_relaxed);
        }
    }

    // Try to enqueue. Returns false immediately if the queue is full (non-blocking).
    bool push(const T& val) {
        size_t pos = head_.load(std::memory_order_relaxed);

        while (true) {
            Slot& slot = slots_[pos % Capacity];
            size_t seq = slot.seq.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                // Slot is ready. Try to claim it by advancing head.
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    slot.data = val;
                    // Signal to consumers that data is ready
                    slot.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
                // Another producer grabbed it, retry with updated pos
            } else if (diff < 0) {
                // Queue is full
                return false;
            } else {
                // Another producer is mid-write on this slot, reload head and retry
                pos = head_.load(std::memory_order_relaxed);
            }
        }
    }

    // Try to dequeue. Returns nullopt if the queue is empty (non-blocking).
    std::optional<T> pop() {
        size_t pos = tail_.load(std::memory_order_relaxed);

        while (true) {
            Slot& slot = slots_[pos % Capacity];
            size_t seq = slot.seq.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

            if (diff == 0) {
                // Data is here. Try to claim this slot by advancing tail.
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    T val = std::move(slot.data);
                    // Reset the slot for the next lap around the ring
                    slot.seq.store(pos + Capacity, std::memory_order_release);
                    return val;
                }
                // Another consumer grabbed it, retry
            } else if (diff < 0) {
                // Queue is empty
                return std::nullopt;
            } else {
                pos = tail_.load(std::memory_order_relaxed);
            }
        }
    }

    bool empty() const {
        size_t h = head_.load(std::memory_order_acquire);
        size_t t = tail_.load(std::memory_order_acquire);
        return h == t;
    }

private:
    // head_ and tail_ are on separate cache lines to avoid false sharing.
    // The SPSC ring buffer doesn't bother with this, but with multiple threads
    // hammering both ends it starts to matter.
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    Slot slots_[Capacity];
};
