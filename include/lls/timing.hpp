#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <vector>

namespace lls {

// rdtsc — not portable, but unbeatable for micro-benchmarks on x86.
// use rdtscp (serializing variant) when you need to be sure prior
// instructions have retired before reading the counter.

inline uint64_t rdtsc() noexcept {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

inline uint64_t rdtscp() noexcept {
    uint32_t lo, hi, aux;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// RAII timer — prints elapsed on scope exit.
// i use this constantly for quick-and-dirty profiling.
class ScopedTimer {
public:
    explicit ScopedTimer(const char* label)
        : label_(label)
        , start_(std::chrono::high_resolution_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
        std::printf("[%s] %ld ns (%.3f ms)\n", label_, ns, ns / 1e6);
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    const char* label_;
    std::chrono::high_resolution_clock::time_point start_;
};

// returns median of N runs in nanoseconds
template <typename Fn>
double measure_ns(Fn&& fn, int runs = 100) {
    std::vector<double> samples;
    samples.reserve(runs);

    for (int i = 0; i < runs; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        fn();
        auto t1 = std::chrono::high_resolution_clock::now();
        samples.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }

    std::sort(samples.begin(), samples.end());
    return samples[runs / 2];
}

} // namespace lls
