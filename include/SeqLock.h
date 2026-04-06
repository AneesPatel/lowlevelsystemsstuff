#pragma once

#include <atomic>
#include <thread>

// A basic SeqLock (Sequential Lock) designed for low-latency read-mostly workloads.
// Readers never block each other and don't take a lock.
// Writers lock out other writers and force concurrent readers to retry.
class SeqLock {
public:
    SeqLock() : sequence_(0) {}

    // --- WRITER API --- 
    
    // Acquire exclusive write access
    void write_lock() {
        size_t expected;
        // Spin until we see an even sequence (no writer active) and successfully atomic-swap to odd
        do {
            expected = sequence_.load(std::memory_order_relaxed);
            // If odd, another writer holds the lock, so spin
            while (expected % 2 != 0) {
                std::this_thread::yield(); // Simple yield, could use pause instructions here
                expected = sequence_.load(std::memory_order_relaxed);
            }
        // Try to atomically move expected (even) to expected + 1 (odd)
        } while (!sequence_.compare_exchange_weak(expected, expected + 1, 
                                                 std::memory_order_acquire, 
                                                 std::memory_order_relaxed));
    }

    // Release exclusive write access
    void write_unlock() {
        // Increment the sequence to an even number, signaling that the write is complete
        sequence_.fetch_add(1, std::memory_order_release);
    }

    // --- READER API ---
    
    // Begin a read transaction. Returns the starting sequence number.
    size_t read_begin() const {
        size_t seq;
        while (true) {
            seq = sequence_.load(std::memory_order_acquire);
            // If sequence is even, no active writer -> safe to attempt reading
            if (seq % 2 == 0) {
                break;
            }
            // If odd, a write is currently in progress. Spin until it completes.
            std::this_thread::yield();
        }
        return seq;
    }

    // Check if the read data might be corrupted by a concurrent write.
    // Returns TRUE if you must discard your read and TRY AGAIN.
    // Returns FALSE if the read is valid and safe to use.
    bool read_retry(size_t begin_seq) const {
        // Ensure memory reads happen before this final check
        std::atomic_thread_fence(std::memory_order_acquire);
        return sequence_.load(std::memory_order_relaxed) != begin_seq;
    }

private:
    // Holds the version counter. Even = unlocked, Odd = locked by writer.
    std::atomic<size_t> sequence_;
};
