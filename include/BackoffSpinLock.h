#pragma once

#include <atomic>

// Same pause macro as SpinLock.h — needed here too
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    #include <immintrin.h>
    #define CPU_PAUSE() _mm_pause()
#elif defined(__arm__) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
    #define CPU_PAUSE() __asm__ __volatile__("yield" ::: "memory")
#else
    #define CPU_PAUSE()
#endif

// SpinLock with exponential backoff.
//
// The basic spinlock works fine at low contention, but under heavy contention (8+ threads)
// it turns into a cache line stampede — everyone is hammering the same memory location
// with acquire RMWs and trashing each other's L1 caches.
//
// The fix: when we fail to grab the lock, wait a bit before retrying. Double the wait
// each time up to a cap. This spreads out the attempts and reduces coherence traffic.
//
// Downside: at low contention you pay a small extra delay when someone else just happened
// to hold the lock briefly. Not worth it if your critical sections are tiny and threads
// are few — the plain SpinLock is better there.
class BackoffSpinLock {
    static constexpr int MIN_BACKOFF = 1;
    static constexpr int MAX_BACKOFF = 1024;

public:
    BackoffSpinLock() = default;

    void lock() {
        int backoff = MIN_BACKOFF;

        while (true) {
            // Test first (relaxed) before trying the expensive exchange
            while (locked_.load(std::memory_order_relaxed)) {
                spin_for(backoff);
                backoff = std::min(backoff * 2, MAX_BACKOFF);
            }

            if (!locked_.exchange(true, std::memory_order_acquire)) {
                return; // got it
            }

            // Exchange failed — someone beat us. Keep backing off.
            spin_for(backoff);
            backoff = std::min(backoff * 2, MAX_BACKOFF);
        }
    }

    void unlock() {
        locked_.store(false, std::memory_order_release);
    }

    bool try_lock() {
        return !locked_.load(std::memory_order_relaxed) &&
               !locked_.exchange(true, std::memory_order_acquire);
    }

private:
    void spin_for(int cycles) {
        for (int i = 0; i < cycles; ++i) {
            CPU_PAUSE();
        }
    }

    std::atomic<bool> locked_{false};
};
