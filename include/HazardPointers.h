#pragma once

#include <atomic>
#include <vector>
#include <functional>
#include <unordered_set>
#include <array>

// Hazard pointers — the simplest possible memory reclamation scheme for lock-free structures.
//
// The problem: in a lock-free structure like the Treiber stack, a thread might read a node
// pointer, then get descheduled. Meanwhile, another thread pops that node and calls delete.
// Now our pointer is dangling. We can't just delete on pop.
//
// The fix: before dereferencing a pointer, publish it as a hazard pointer so that other
// threads know not to free it. When retiring a node, check all published hazard pointers
// first — if anyone still holds a reference to it, don't free it yet.
//
// This version is intentionally simple:
// - Fixed max threads (MAX_THREADS)
// - One hazard pointer slot per thread
// - Retire list per thread, scanned when it hits BATCH_SIZE
//
// Production implementations use more slots per thread and fancier reclaim strategies,
// but this is enough to understand the mechanism.

namespace hp {

static constexpr int MAX_THREADS = 16;
static constexpr int BATCH_SIZE  = 32;

struct HazardRecord {
    std::atomic<void*> ptr{nullptr};
    std::atomic<bool>  active{false};
};

// Global table of hazard pointer records.
// Each thread claims one slot for the duration of its lifetime.
inline std::array<HazardRecord, MAX_THREADS> g_hazard_table{};

inline HazardRecord* acquire_record() {
    for (auto& rec : g_hazard_table) {
        bool expected = false;
        if (rec.active.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return &rec;
        }
    }
    // Ran out of slots — increase MAX_THREADS or use a dynamic scheme
    return nullptr;
}

inline void release_record(HazardRecord* rec) {
    rec->ptr.store(nullptr, std::memory_order_release);
    rec->active.store(false, std::memory_order_release);
}

// Per-thread state: one hazard pointer slot + retire list
struct ThreadState {
    HazardRecord* record{nullptr};
    std::vector<std::pair<void*, std::function<void(void*)>>> retire_list;

    ThreadState() {
        record = acquire_record();
    }

    ~ThreadState() {
        // Force-free everything on thread exit — assumes no concurrent access at this point
        for (auto& [ptr, deleter] : retire_list) {
            deleter(ptr);
        }
        if (record) {
            release_record(record);
        }
    }
};

inline thread_local ThreadState tls;

// Publish ptr as "in use" so other threads won't free it.
// Call this before dereferencing any pointer that might be concurrently freed.
template <typename T>
T* protect(std::atomic<T*>& src) {
    T* ptr;
    do {
        ptr = src.load(std::memory_order_relaxed);
        tls.record->ptr.store(ptr, std::memory_order_release);
        // Re-read to make sure ptr hasn't changed between our load and our publish.
        // If it has, the fence ensures we see the new value on the next iteration.
        std::atomic_thread_fence(std::memory_order_seq_cst);
    } while (ptr != src.load(std::memory_order_acquire));
    return ptr;
}

// Clear the hazard pointer — we're done with ptr for now
inline void clear() {
    tls.record->ptr.store(nullptr, std::memory_order_release);
}

// Queue ptr for deferred deletion. Will be freed once no thread holds a hazard to it.
template <typename T>
void retire(T* ptr) {
    tls.retire_list.emplace_back(ptr, [](void* p) { delete static_cast<T*>(p); });

    if (static_cast<int>(tls.retire_list.size()) >= BATCH_SIZE) {
        scan();
    }
}

// Try to reclaim everything in the retire list that isn't currently hazardous.
inline void scan() {
    // Collect all currently published hazard pointers
    std::unordered_set<void*> hazardous;
    for (auto& rec : g_hazard_table) {
        if (rec.active.load(std::memory_order_acquire)) {
            void* p = rec.ptr.load(std::memory_order_acquire);
            if (p) hazardous.insert(p);
        }
    }

    // Free anything that nobody is protecting
    std::vector<std::pair<void*, std::function<void(void*)>>> still_alive;
    for (auto& [ptr, deleter] : tls.retire_list) {
        if (hazardous.count(ptr) == 0) {
            deleter(ptr);
        } else {
            still_alive.emplace_back(ptr, std::move(deleter));
        }
    }
    tls.retire_list = std::move(still_alive);
}

} // namespace hp
