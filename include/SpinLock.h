#pragma once

#include <atomic>

// Architecture-specific pause instruction to prevent spinning from hogging CPU resources
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    #include <immintrin.h>
    #define CPU_PAUSE() _mm_pause()
#elif defined(__arm__) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
    #define CPU_PAUSE() __asm__ __volatile__("yield" ::: "memory")
#else
    #define CPU_PAUSE()
#endif

// A simple Test-and-Test-and-Set (TTAS) SpinLock for extremely low latency synchronization.
class SpinLock {
public:
    SpinLock() = default;

    // Grants exclusive access
    void lock() {
        while (true) {
            // Step 1: "Test" - Read the lock state without attempting to modify it (cache-friendly).
            // This prevents generating excessive cache coherence traffic when the lock is held.
            while (locked_.load(std::memory_order_relaxed)) {
                CPU_PAUSE(); // Hint to the CPU that we are in a spin-wait loop
            }

            // Step 2: "Test-and-Set" - Try to acquire the lock.
            // exchange() atomically sets the flag to true and returns its previous value.
            // If it returns false, it means we successfully grabbed the free lock.
            if (!locked_.exchange(true, std::memory_order_acquire)) {
                break; // Lock acquired successfully
            }
        }
    }

    // Releases the lock
    void unlock() {
        // We use memory_order_release to ensure all previous writes protected 
        // by the lock are visible to the next thread that acquires it.
        locked_.store(false, std::memory_order_release);
    }

    // Attempt to acquire the lock without spinning
    bool try_lock() {
        // First check with relaxed memory order, then try to exchange if it looks free.
        return !locked_.load(std::memory_order_relaxed) &&
               !locked_.exchange(true, std::memory_order_acquire);
    }

private:
    std::atomic<bool> locked_{false};
};
